"""Experimental phone-local 1:1 Zalo UDP/SRTP/Opus audio transport.

Protocol adapted from VN-Linux-Family/zalo-for-linux (BSD-3-Clause).
Does not record audio, save keys, or depend on a desktop sound server.
"""
import json
import os
import socket
import struct
import time
try:
    import audioop
except ImportError:
    audioop=None
from q10media import Audio, Opus, SRTP, VoiceDSP, audio_header, control_header, init_packet, u16string, unwrap_relay, wrap_audio
from videowire import VideoAssembler,packetize


def endpoint(value):
    if not isinstance(value,str) or len(value)>255 or value.count(':')!=1:
        raise ValueError("Unsupported relay address")
    host,port=value.rsplit(':',1)
    port=int(port)
    if not host or not 0<port<65536:
        raise ValueError("Invalid relay endpoint")
    return socket.gethostbyname(host),port


def twcc_sequence(rtp):
    if len(rtp)<16 or not rtp[0]&16:
        return None
    offset=12+4*(rtp[0]&15)
    if offset+4>len(rtp) or rtp[offset:offset+2]!=b'\xbe\xde':
        return None
    end=offset+4+4*struct.unpack_from("!H",rtp,offset+2)[0]
    offset+=4
    if end>len(rtp):
        return None
    while offset<end:
        byte=rtp[offset];offset+=1
        if not byte:
            continue
        length=(byte&15)+1
        if byte>>4==15 or offset+length>end:
            return None
        if byte>>4==5 and length==2:
            return struct.unpack_from("!H",rtp,offset)[0]
        offset+=length
    return None


class Feedback:
    def __init__(self,local_uid,peer_uid):
        self.local_uid,self.peer_uid=local_uid,peer_uid
        self.arrivals={};self.tw_high=None;self.tw_next=None;self.fbcount=0
        self.base=None;self.highest=0;self.received=0;self.jitter=0;self.transit=None
        self.lsr=0;self.lsr_at=0

    def received_rtp(self,packet,index,now):
        if self.base is None:
            self.base=index
        self.highest=max(self.highest,index);self.received+=1
        timestamp=struct.unpack_from("!I",packet,4)[0]
        transit=now*16000-timestamp
        if self.transit is not None:
            self.jitter+=(abs(transit-self.transit)-self.jitter)/16
        self.transit=transit
        self.transport_received(packet,now)

    def transport_received(self,packet,now):
        seq=twcc_sequence(packet)
        if seq is None:
            return
        extended=seq
        if self.tw_high is not None:
            extended=(self.tw_high&~65535)+seq
            if extended-self.tw_high>32768: extended-=65536
            elif self.tw_high-extended>32768: extended+=65536
        if self.tw_next is None:
            self.tw_next=extended
        if extended<self.tw_next:
            return
        self.tw_high=extended if self.tw_high is None else max(self.tw_high,extended)
        if self.tw_high-self.tw_next>511:
            self.arrivals.clear();self.tw_next=extended
        self.arrivals[extended]=int(round(now*4000))

    def twcc(self):
        if not self.arrivals:
            return None
        base=self.tw_next;end=min(self.tw_high,base+199)
        entries=[(seq,self.arrivals[seq]) for seq in sorted(self.arrivals) if base<=seq<=end]
        if not entries:
            self.tw_next=end+1
            return None
        reference=entries[0][1]//256
        previous=reference*256
        symbols=[];delta=[]
        for seq in range(base,end+1):
            now=self.arrivals.pop(seq,None)
            if now is None:
                symbols.append(0)
                continue
            diff=now-previous;previous=now
            if 0<=diff<=255:
                symbols.append(1);delta.append(struct.pack('!B',diff))
            else:
                symbols.append(2);delta.append(struct.pack('!h',max(-32768,min(32767,diff))))
        chunks=[]
        for i in range(0,len(symbols),7):
            value=0xc000
            for k,symbol in enumerate(symbols[i:i+7]):
                value|=symbol<<(12-2*k)
            chunks.append(struct.pack('!H',value))
        body=struct.pack('!IIHH',self.local_uid,self.peer_uid,base&65535,len(symbols))
        body+=(reference&0xffffff).to_bytes(3,'big')+struct.pack('!B',self.fbcount&255)
        body+=b''.join(chunks)+b''.join(delta)
        body+=bytes((-len(body))%4)
        self.fbcount+=1;self.tw_next=end+1
        return struct.pack('!BBH',0x8f,205,len(body)//4)+body

    def received_rtcp(self,data,now):
        offset=0
        while offset+4<=len(data):
            length=4*(struct.unpack_from('!H',data,offset+2)[0]+1)
            if length<4 or offset+length>len(data) or data[offset]>>6!=2:
                return
            if data[offset+1]==200 and length>=28 and struct.unpack_from('!I',data,offset+4)[0]==self.peer_uid:
                seconds,fraction=struct.unpack_from('!II',data,offset+8)
                self.lsr=((seconds&65535)<<16)|(fraction>>16);self.lsr_at=now
            offset+=length

    def report(self,now,rtp_timestamp,sent_packets,sent_octets):
        blocks=b''
        if self.base is not None:
            lost=max(0,min(0x7fffff,self.highest-self.base+1-self.received))
            delay=min(0xffffffff,int(max(0,now-self.lsr_at)*65536)) if self.lsr else 0
            blocks=struct.pack('!I',self.peer_uid)+struct.pack('!I',lost)
            blocks+=struct.pack('!IIII',self.highest&0xffffffff,int(self.jitter)&0xffffffff,self.lsr,delay)
        seconds=int(now)+2208988800;fraction=int((now-int(now))*4294967296)
        body=struct.pack('!IIIIII',self.local_uid,seconds&0xffffffff,fraction&0xffffffff,
                         rtp_timestamp&0xffffffff,sent_packets&0xffffffff,sent_octets&0xffffffff)+blocks
        return struct.pack('!BBH',0x80+(1 if blocks else 0),200,len(body)//4)+body


class RelayMedia:
    def __init__(self,ready,early,failed,peer_end,clock=time.time,root=None,video_frame=None):
        self.ready,self.early,self.failed,self.peer_end=ready,early,failed,peer_end
        self.clock=clock;self.root=root or os.path.dirname(os.path.abspath(__file__))
        self.sock=None;self.capture=self.playback=self.codec=self.dsp=None
        self.call=None;self.active=False;self.candidates={};self.replies={};self.chosen=None
        self.video_frame=video_frame or (lambda key,data:None)
        self.muted=False
        self.tuning_path=None;self.cfg={'dsp':True,'mic_gain':'auto','mic_target':1800.0,'mic_max_gain':8.0,'far_max_gain':6.0,'ns_db':-10,'gate':120.0,'play_gain':1.0,'audio_type':None,'cushion':0}
        self.mic_gain=1.5;self.next_write=None;self.late_run=0;self.waiting=False;self.expected=None;self.play_at=None
        self.far_until=0.0;self.dsp_w=0.0;self.last_loop=None;self.applied_gain=1.0
        self.cushion_cap=0;self.aec_ref=b'';self.pb_restore=0;self.pb_pending=0;self.pb_total=0;self.pb_inject_at=0.0
        self.stats={'video_input':0,'video_key_input':0,'video_no_key':0,'video_queue_drop':0,'video_sent':0,'udp_errors':0,"sent_audio_packets":0,"received_audio_packets":0,"dropped_auth_packets":0,"concealed_frames":0,"dropped_capture_frames":0,"play_silence":0,"play_full":0,"mic_peak":0,"play_peak":0,"mic_raw_peak":0,"mic_speech_frames":0,"mic_dsp_muted":0,"play_voice_frames":0,"resyncs":0,"skips":0,"play_wait":0,"late_packets":0}

    def prepare(self,call,servers):
        if self.sock:
            self.stop()
        self.replies={};self.chosen=None;self.active=False
        self.call=call
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.bind(('0.0.0.0',0));self.sock.setblocking(False)
        self.candidates={}
        for server in servers[:16]:
            if not isinstance(server,dict): continue
            try:
                address=endpoint(server.get('rtpaddr') or server.get('rtp') or server.get('rtpAddress'))
                raw=dict(server)
                raw['rtpaddr']=server.get('rtpaddr') or server.get('rtp') or server.get('rtpAddress')
                raw['rtcpaddr']=server.get('rtcpaddr') or server.get('rtcp') or server.get('rtcpAddress')
                if not isinstance(raw['rtcpaddr'],str): continue
                self.candidates[address]=raw
            except (ValueError,OSError,socket.error):
                continue
        if not self.candidates:
            self.failed('no_supported_ipv4_relay');return
        master=call['session'].encode('utf-8')[:30]
        self.nonces={} if call.get('video') else None
        self.tx,self.rx=SRTP(master,call['local_uid'],nonce_guard=self.nonces),SRTP(master,call['peer_uid'])
        self.vtx=SRTP(master,call['local_uid'],payload_types=(97,98),nonce_guard=self.nonces,stream='video')
        self.vrx=SRTP(master,call['peer_uid'],payload_types=(97,98))
        self.video_assembler=VideoAssembler(self.video_frame,self.request_keyframe,self.clock)
        self.video_queue=[];self.video_sequence=0;self.video_roc=0;self.next_video=0;self.last_pli=0;self.video_key_sent=False
        self.feedback=Feedback(call['local_uid'],call['peer_uid'])
        self.packet=init_packet(call['local_uid'],call['peer_uid'],call['id'],call['session'],call['role']=='caller')
        self.attempts=0;self.next_init=0;self.ready_at=None
        self.capture_buffer=b'';self.playback_buffer=b'';self.jitter={};self.expected=None;self.play_at=None;self.late_run=0;self.waiting=False
        self.rx_frames=0;self.rx_interval=0;self.last_rx=self.clock();self.last_stats=self.clock()
        self.sent_octets=0;self.next_ping=self.next_status=self.next_report=self.next_twcc=0
        self.sequence=(49152+struct.unpack('!H',os.urandom(2))[0]%4096) if call.get('video') else struct.unpack('!H',os.urandom(2))[0]
        self.roc=0;self.twseq=struct.unpack('!H',os.urandom(2))[0]
        self.timestamp=struct.unpack('!I',os.urandom(4))[0];self.control_sequence=0
        self.tick()

    def send(self,packet,address=None):
        if self.sock:
            try:
                self.sock.sendto(packet,address or self.chosen)
            except socket.error:
                self.stats['udp_errors']+=1
                return False
            return True

    def handle_udp(self):
        for _ in range(12):
            try:
                packet,address=self.sock.recvfrom(8192)
            except socket.error:
                break
            p2p=False
            if len(packet)>=9 and packet[0]==127 and packet[7] in (6,7) and struct.unpack_from('<I',packet,3)[0]==self.call['id']:
                # Some phones send direct audio even when sP2P is zero. Accept
                # no untrusted signalling here: SRTP authenticates the payload
                # against this call and the fixed expected peer SSRC below.
                parsed={'kind':4 if packet[7]==6 else 14,'payload':packet[9:]};p2p=True
            else:
                if address not in self.candidates:
                    continue
                try:
                    parsed=unwrap_relay(packet)
                except (ValueError,UnicodeError,struct.error):
                    continue
            kind=parsed['kind']
            if kind==2 and parsed.get('cmd') in (11,12) and parsed.get('result')==0:
                if parsed.get('call_id')!=self.call['id'] or not parsed.get('server_token'):
                    continue
                if address not in self.replies:
                    self.replies[address]=parsed['server_token']
                    if self.ready_at is None:
                        self.chosen=address;self.ready_at=self.clock()+0.6
                continue
            if self.chosen!=address and not p2p:
                continue
            if kind==4:
                rtp=parsed['payload'];plain=self.rx.unprotect(rtp)
                if plain is None:
                    self.stats['dropped_auth_packets']+=1;continue
                now=self.clock();self.last_rx=now;self.rx_frames+=1;self.rx_interval+=1
                self.stats['received_audio_packets']+=1
                self.feedback.received_rtp(rtp,self.rx.last_index,now)
                self.queue_audio(self.rx.last_index,plain,now)
                self.early(self.rx_frames)
                if not self.sock:
                    return
            elif kind==14 and self.call.get('video'):
                rtp=parsed['payload'];plain=self.vrx.unprotect(rtp)
                if plain is None:
                    self.stats['dropped_auth_packets']+=1;continue
                now=self.clock();self.feedback.transport_received(rtp,now)
                self.video_assembler.push(self.vrx.last_index,struct.unpack_from('!I',rtp,4)[0],rtp[1]&127,bool(rtp[1]&128),plain)
            elif kind in (5,7,15):
                self.feedback.received_rtcp(parsed['payload'],self.clock())
            elif kind==2 and parsed.get('cmd')==3 and self.active:
                data=parsed['payload']
                if len(data)>=4 and struct.unpack_from('<I',data)[0]==self.call['id']:
                    self.peer_end()
                    return

    def start(self):
        if self.active:
            return
        if not self.chosen or self.chosen not in self.replies:
            raise RuntimeError('No negotiated media server')
        try:
            self.codec=Opus(os.path.join(self.root,'libq10opus.so'))
            self.dsp=VoiceDSP(os.path.join(self.root,'libq10dsp.so'))
            self.load_tuning()
            self.stats['ns_applied']=int(self.dsp.tune(self.cfg['ns_db']))
            self.playback=Audio(os.path.join(self.root,'libq10audio.so'),False,audio_type=self.cfg['audio_type'])
            self.capture=Audio(os.path.join(self.root,'libq10audio.so'),True,audio_type=self.cfg['audio_type'])
        except Exception:
            self.close_audio()
            raise
        self.active=True;now=self.clock();self.last_rx=now
        self.next_ping=now;self.next_status=now;self.next_report=now+1;self.next_twcc=now+0.05
        self.send(control_header(32,self.call['local_uid'],self.replies[self.chosen],subcmd=8,magic=1)+struct.pack('<II',4,0))
        for address,token in self.replies.items():
            if address!=self.chosen:
                self.send(control_header(3,self.call['local_uid'],token)+struct.pack('<I',self.call['id']),address)

    def load_tuning(self):
        """Optional <data>/voice-tuning.json (all keys optional):
        dsp (true = echo canceller only while the far end is audible, false = off, "always"), mic_gain ("auto" or 0.1-16), mic_target (rms), mic_max_gain, gate (rms),
        play_gain (0.1-8), audio_type (int: audio-manager type used for both directions)."""
        cfg={'dsp':True,'mic_gain':'auto','mic_target':1800.0,'mic_max_gain':8.0,'far_max_gain':6.0,'ns_db':-10,'gate':120.0,'play_gain':1.0,'audio_type':None,'cushion':0}
        try:
            with open(self.tuning_path) as handle:data=json.load(handle)
        except (IOError,OSError,ValueError,TypeError):
            data={}
        if isinstance(data,dict):
            def number(value,low,high):
                return isinstance(value,(int,float)) and not isinstance(value,bool) and low<=value<=high
            for key,low,high in (('mic_target',500,12000),('mic_max_gain',1,16),('far_max_gain',1,16),('gate',0,2000),('play_gain',0.1,8)):
                if number(data.get(key),low,high):cfg[key]=float(data[key])
            if data.get('dsp') in (True,False):cfg['dsp']=bool(data['dsp'])
            elif data.get('dsp')=='always':cfg['dsp']='always'
            gain=data.get('mic_gain')
            if gain=='auto':cfg['mic_gain']='auto'
            elif number(gain,0.1,16):cfg['mic_gain']=float(gain)
            if number(data.get('ns_db'),-30,-1):cfg['ns_db']=int(data['ns_db'])
            if number(data.get('cushion'),0,4):cfg['cushion']=int(data['cushion'])
            if data.get('ns_db')=='stock':cfg['ns_db']=None
            kind=data.get('audio_type')
            if isinstance(kind,int) and not isinstance(kind,bool) and 0<=kind<=32:cfg['audio_type']=kind
        self.cfg=cfg;self.mic_gain=1.5;self.next_write=None
        self.cushion_cap=cfg['cushion'];self.aec_ref=b'';self.pb_restore=0;self.pb_pending=0;self.pb_total=0;self.pb_inject_at=0.0
        self.stats['cfg_dsp']=2 if cfg['dsp']=='always' else int(bool(cfg['dsp']));self.stats['cfg_type']=-1 if cfg['audio_type'] is None else cfg['audio_type']
        self.stats['cfg_play_x100']=int(cfg['play_gain']*100)
        self.stats['cfg_ns']=-18 if cfg['ns_db'] is None else cfg['ns_db']
        self.stats['cfg_mic']=-1 if cfg['mic_gain']=='auto' else int(cfg['mic_gain']*100)

    def device_messages(self,data):
        """Count the warnings written to this process's stderr during a call.

        "discarded first playback frame", "No playback frame available", "Auto-filling the buffer"
        and "Had to discard a playback frame" are printed by the Speex echo canceller inside
        libq10dsp, not by the speaker: its reference (speaker) stream was fed out of step with the
        microphone stream. process_mic now feeds it strictly one reference frame per mic frame, so
        after the first few frames of a call these should stop; dev_autofill/dev_discard staying
        high means that pairing is broken again. The optional `cushion` (default 0 = off) adds
        silent frames to the speaker queue when they appear; it does not help the canceller."""
        if not self.active:
            return
        stats=self.stats
        first=data.count(b'discarded first playback frame')
        noref=data.count(b'No playback frame available')
        auto=data.count(b'Auto-filling the buffer')
        dropped=data.count(b'Had to discard a playback frame')
        if first:stats['dev_first']=stats.get('dev_first',0)+first
        if noref:stats['dev_noref']=stats.get('dev_noref',0)+noref
        if auto:stats['dev_autofill']=stats.get('dev_autofill',0)+auto
        if dropped:stats['dev_discard']=stats.get('dev_discard',0)+dropped
        if not self.cushion_cap:
            return
        if first and stats['dev_first']<=8:self.pb_restore=min(3,self.pb_restore+first)
        if auto and self.pb_total+self.pb_pending<self.cushion_cap:self.pb_pending+=1
        if dropped:
            self.pb_total=max(0,self.pb_total-1)
            self.cushion_cap=min(self.cushion_cap,self.pb_total)
            self.pb_pending=0;self.pb_restore=0

    def gap(self,now):
        """Called once per engine loop iteration: how long the loop went without running."""
        if self.last_loop is not None:
            gap=now-self.last_loop;ms=int(gap*1000)
            if gap>0.04:self.stats['gap_gt40']=self.stats.get('gap_gt40',0)+1
            if gap>0.06:self.stats['gap_gt60']=self.stats.get('gap_gt60',0)+1
            if gap>0.10:self.stats['gap_gt100']=self.stats.get('gap_gt100',0)+1
            if ms>self.stats.get('gap_max',0):self.stats['gap_max']=ms
        self.last_loop=now

    def lap(self,name,since):
        """Time one section of the loop; returns the new mark. Counts sections slower than 30 ms."""
        now=time.time();spent=now-since
        if spent>0.03:self.stats['slow_'+name]=self.stats.get('slow_'+name,0)+1
        ms=int(spent*1000)
        if ms>self.stats.get('max_'+name,0):self.stats['max_'+name]=ms
        return now

    def note(self,key,value,alpha=0.02):
        old=self.stats.get(key)
        self.stats[key]=int(value) if old is None else int(old+(value-old)*alpha)

    def process_mic(self,frame,now=None):
        if now is None:now=time.time()
        raw=audioop.rms(frame,2) if audioop else 0
        if audioop:
            self.stats['mic_raw_peak']=max(self.stats['mic_raw_peak'],audioop.max(frame,2))
            if raw>=self.cfg['gate']:
                self.note('mic_raw_speech_rms',raw,0.05);self.stats['mic_speech_frames']+=1
            elif audioop:
                # Frames below the gate: the room/handset noise floor. Together with the speech level
                # above it gives the microphone's signal-to-noise ratio, which the digital gain cannot improve.
                self.note('mic_raw_noise_rms',raw,0.02);self.stats['mic_noise_frames']=self.stats.get('mic_noise_frames',0)+1
        used=False
        if self.cfg['dsp']:
            # Always run the canceller so its filter keeps adapting, but only use its output while
            # the far end is audible (echo is possible). With the far end silent there is nothing to
            # cancel, and the noise suppressor was cutting our own speech by ~13 dB. Weight changes
            # are ramped over two frames so switching does not click.
            ref=self.aec_ref[:640];self.aec_ref=self.aec_ref[640:]
            if len(ref)<640:ref+=b'\x00'*(640-len(ref))      # nothing was played since the last mic frame
            self.dsp.playback(ref)                           # exactly one reference frame per mic frame
            processed=self.dsp.capture(frame)
            far=self.cfg['dsp']=='always' or not audioop or now<self.far_until
            target=1.0 if far else 0.0
            if self.dsp_w<target:self.dsp_w=min(target,self.dsp_w+0.5)
            elif self.dsp_w>target:self.dsp_w=max(target,self.dsp_w-0.5)
            if self.dsp_w>=1.0:frame=processed;used=True
            elif self.dsp_w>0.0:
                frame=audioop.add(audioop.mul(frame,2,1.0-self.dsp_w),audioop.mul(processed,2,self.dsp_w),2);used=True
        if self.muted:
            return b'\x00'*640
        if not audioop:
            return frame
        if used:self.stats['mic_dsp_frames']=self.stats.get('mic_dsp_frames',0)+1
        rms=audioop.rms(frame,2);peak=audioop.max(frame,2)
        if used and raw>=self.cfg['gate'] and rms<self.cfg['gate']*0.5:
            self.stats['mic_dsp_muted']+=1                # echo canceller swallowed real speech
        far_now=now<self.far_until
        gain=self.cfg['mic_gain']
        if gain=='auto':
            # Slow-rise / fast-fall AGC toward a speech level. Frames below the gate (room noise,
            # pauses) hold the gain so the noise floor is not chased. While the far end is audible
            # the ceiling drops and the gain never rises, so residual echo is not amplified.
            ceiling=self.cfg['mic_max_gain']
            if far_now:ceiling=min(ceiling,self.cfg['far_max_gain'])
            if rms>=self.cfg['gate']:
                want=max(1.0,min(ceiling,self.cfg['mic_target']/float(rms)))
                if want<self.mic_gain:self.mic_gain=want
                elif not far_now:self.mic_gain+=(want-self.mic_gain)*0.04
            if far_now and self.mic_gain>ceiling:self.mic_gain=max(ceiling,self.mic_gain*0.9)
            gain=self.mic_gain
        # Apply the gain as a short ramp (4 x 5 ms) from last frame's value so level changes do not
        # click at frame boundaries, and limit each block so it can never clip.
        previous=self.applied_gain
        if gain!=1.0 or previous!=1.0:
            blocks=[]
            for i in range(4):
                step=previous+(gain-previous)*(i+1)/4.0
                block=frame[i*160:(i+1)*160]
                top=audioop.max(block,2)
                if step>1.0 and top>0:step=min(step,32000.0/top)
                blocks.append(audioop.mul(block,2,step) if step!=1.0 else block)
            frame=b''.join(blocks)
        self.applied_gain=gain
        sent=audioop.rms(frame,2)
        if sent>=self.cfg['gate']:self.note('mic_sent_speech_rms',sent,0.05)
        elif raw<self.cfg['gate']:self.note('mic_sent_noise_rms',sent,0.02)
        self.note('mic_gain_x100',gain*100)
        self.stats['mic_peak']=max(self.stats['mic_peak'],audioop.max(frame,2))
        return frame

    def aec_feed(self,pcm):
        """Remember audio handed to the speaker; process_mic pairs it 1:1 with mic frames."""
        if not self.cfg['dsp']:return
        self.aec_ref+=pcm
        if len(self.aec_ref)>640*4:self.aec_ref=self.aec_ref[-640*3:]     # never let the reference run far ahead

    def pace_playback(self,now):
        """Feed the speaker one 20 ms frame per 20 ms. The device queue holds only 4 x 640 bytes,
        so bursts underrun it ("No playback frame") and then overflow it ("discard a playback
        frame"); both are audible as crackle."""
        if self.next_write is None:
            prime=b'\x00'*1920                        # 3 fragments (60 ms): enough cushion, keeps the echo path short
            written=self.playback.write(prime)
            if written>0:self.aec_feed(prime[:written])
            if written<0 and written!=-11:
                self.failed('speaker_io_error_%d' % written);return
            self.next_write=now+0.02
            return
        gain=self.cfg['play_gain']
        if (self.pb_restore>0 or self.pb_pending>0) and now>=self.pb_inject_at:
            silence=b'\x00'*640
            written=self.playback.write(silence)
            if written>0:
                self.aec_feed(silence[:written])
                if self.pb_restore>0:self.pb_restore-=1
                else:
                    self.pb_pending-=1;self.pb_total+=1;self.stats['cush_total']=self.pb_total
                self.pb_inject_at=now+0.02
            elif written in (0,-11):
                self.pb_restore=0;self.pb_pending=0       # queue is full: the cushion is already as deep as it gets
            else:
                self.failed('speaker_io_error_%d' % written);return
        for _ in range(4):
            if now<self.next_write:break
            real=len(self.playback_buffer)>=640
            chunk=self.playback_buffer[:640] if real else b'\x00'*640
            out=audioop.mul(chunk,2,gain) if (real and gain!=1.0 and audioop) else chunk
            written=self.playback.write(out)
            if written>0:
                if real:self.playback_buffer=self.playback_buffer[written:]
                else:self.stats['play_silence']+=1
                self.aec_feed(out[:written])
                if real and audioop:
                    level=audioop.rms(out[:written],2)
                    if level>=100:
                        self.note('play_voice_rms',level,0.05);self.stats['play_voice_frames']+=1
                    if level>=80:self.far_until=now+0.6        # far end audible: echo possible
                    self.stats['play_peak']=max(self.stats['play_peak'],audioop.max(out[:written],2))
                self.next_write+=0.02
                if written<len(out):break
            elif written in (0,-11):
                self.stats['play_full']+=1;break
            else:
                self.failed('speaker_io_error_%d' % written);return
        if now-self.next_write>0.1:
            self.next_write=now                      # stalled process: resync instead of bursting

    def queue_audio(self,index,plain,now):
        """Put a received frame (index = SRTP packet index) into the jitter buffer."""
        if self.expected is None:
            self.expected=index;self.play_at=now+0.06;self.late_run=0
        if index<self.expected:
            # Behind the playout pointer. A lone straggler was already concealed; a run of them
            # means the pointer got ahead of the stream, so rejoin the stream where it is now.
            self.stats['late_packets']+=1;self.late_run+=1
            if self.late_run>=5:
                self.jitter.clear();self.expected=index;self.play_at=now+0.04
                self.late_run=0;self.waiting=False;self.stats['resyncs']+=1
                self.jitter[index]=plain
            return
        self.late_run=0
        if self.waiting:
            self.waiting=False;self.play_at=now+0.04       # stream resumed: re-buffer ~40 ms
        if len(self.jitter)>=64:
            self.jitter.clear();self.expected=index;self.play_at=now+0.06;self.stats['resyncs']+=1
        self.jitter[index]=plain

    def schedule_playout(self,now):
        """Decode one frame per 20 ms into playback_buffer, never running ahead of the stream."""
        if self.play_at is None or self.expected is None:
            return
        if self.rx.highest-self.expected>8:                # bound latency to ~160 ms
            self.expected=self.rx.highest-3
            self.jitter=dict((index,value) for index,value in self.jitter.items() if index>=self.expected)
            self.stats['skips']+=1
        if now-self.play_at>0.12:
            self.play_at=now-0.04                          # delayed process: resume pacing, no backlog replay
        for _ in range(3):
            if now<self.play_at:
                break
            packet=self.jitter.pop(self.expected,None)
            if packet is None and self.rx.highest<=self.expected:
                # Nothing newer has arrived: the stream is late or paused (silence suppression).
                # Hold the pointer. Advancing it here made every later packet "late" and dropped it.
                self.stats['play_wait']+=1;self.waiting=True
                self.play_at=now+0.02
                break
            if packet is None:
                self.stats['concealed_frames']+=1
            try:
                if packet is None and self.expected+1 in self.jitter:
                    self.playback_buffer+=self.codec.decode(self.jitter[self.expected+1],recover_previous=True)
                else:
                    self.playback_buffer+=self.codec.decode(packet)
            except (RuntimeError,ValueError):
                self.playback_buffer+=self.codec.decode(None)
            self.expected+=1;self.play_at+=0.02
            if len(self.playback_buffer)>640*6:
                self.playback_buffer=self.playback_buffer[-640*4:]

    def audio_tick(self,now):
        mark=time.time()
        for _ in range(8):
            data=self.capture.read()
            if isinstance(data,bytes) and data:
                self.capture_buffer+=data
            elif data in (0,-11) or data==b'':
                break
            else:
                self.failed('microphone_io_error_%d' % data);return
        mark=self.lap('cap',mark)
        # Keep normal scheduler bursts (up to 120 ms) intact; discard only
        # genuinely stale speech. Each 640-byte unit is one 20-ms Opus frame.
        if len(self.capture_buffer)>640*6:
            dropped=(len(self.capture_buffer)//640-6)*640
            self.capture_buffer=self.capture_buffer[dropped:]
            self.stats['dropped_capture_frames']+=dropped//640
            self.timestamp=(self.timestamp+(dropped//640)*320)&0xffffffff
        for _ in range(min(3,len(self.capture_buffer)//640)):
            frame=self.capture_buffer[:640];self.capture_buffer=self.capture_buffer[640:]
            frame=self.process_mic(frame,now)
            opus=self.codec.encode(frame)
            header=audio_header(self.sequence,self.timestamp,self.call['local_uid'],self.twseq)
            packet=self.tx.protect(header,opus,self.roc)
            self.twseq=(self.twseq+1)&65535
            self.send(wrap_audio(packet,self.replies[self.chosen]))
            self.stats['sent_audio_packets']+=1;self.sent_octets+=len(opus)
            self.sequence=(self.sequence+1)&65535
            if self.sequence==0:self.roc+=1
            self.timestamp=(self.timestamp+320)&0xffffffff
        mark=self.lap('enc',mark)
        self.schedule_playout(now)
        self.pace_playback(now)
        self.lap('play',mark)

    def tick(self):
        if not self.sock or not self.call:
            return
        now=self.clock()
        if self.ready_at is None and now>=self.next_init:
            # A locked handset can take several seconds to resume its Wi-Fi route.
            # Keep callee setup inside the 20-second answer deadline.
            limit=12 if self.call['role']=='callee' else 3
            if self.attempts>=limit:
                self.failed('relay_init_timeout');return
            for address in self.candidates:
                self.send(self.packet,address)
            self.attempts+=1;self.next_init=now+1
        if self.ready_at is not None and now>=self.ready_at:
            self.ready_at=float('inf')
            self.ready(self.candidates[self.chosen])
        if not self.active:
            return
        token=self.replies[self.chosen];uid=self.call['local_uid']
        if now>=self.next_ping:
            self.send(control_header(2,uid,token,subcmd=1,magic=1)+u16string(self.call['session']))
            self.next_ping=now+6
        if now>=self.next_status:
            self.control_sequence=(self.control_sequence+1)&0xffffffff
            self.send(control_header(32,uid,token,seq=self.control_sequence,subcmd=10,magic=1)+struct.pack('<6I',20,0,1,1,self.rx_interval,0))
            self.rx_interval=0;self.next_status=now+1
        if now>=self.next_twcc:
            packet=self.feedback.twcc()
            if packet:self.send(struct.pack('<BI',5,token)+packet)
            self.next_twcc=now+0.05
        if now>=self.next_report:
            packet=self.feedback.report(now,self.timestamp,self.stats['sent_audio_packets'],self.sent_octets)
            self.send(struct.pack('<BI',5,token)+packet);self.next_report=now+1
        if now-self.last_rx>20:
            self.failed('peer_audio_timeout');return
        self.audio_tick(now)
        if self.sock and self.call and self.call.get('video'):
            self.video_tick(now)

    def request_keyframe(self):
        now=self.clock()
        if not self.chosen or now-self.last_pli<1:return
        self.last_pli=now
        pli=struct.pack('!BBHII',0x81,206,2,self.call['local_uid'],self.call['peer_uid'])
        self.send(struct.pack('<BI',15,self.replies[self.chosen])+pli)

    def send_video_frame(self,key,data):
        if not self.active or not self.call.get('video'):return
        self.stats['video_input']+=1
        if key:self.stats['video_key_input']+=1
        if not key and not self.video_key_sent:
            self.stats['video_no_key']+=1;return
        if key and len(self.video_queue)>40:
            self.video_queue=[]
        if len(self.video_queue)>40:
            self.stats['video_queue_drop']+=1;return
        chunks=packetize(data)
        if len(chunks)>80:return
        if key:self.video_key_sent=True
        timestamp=int(self.clock()*90000)&0xffffffff
        self.video_queue.extend((part,timestamp,key,i==len(chunks)-1) for i,part in enumerate(chunks))

    def video_tick(self,now):
        self.video_assembler.flush()
        if not self.video_assembler.started:self.request_keyframe()
        # Service speech first and send at most three video packets per tick.
        for _ in range(3 if now>=self.next_video else 0):
            if not self.video_queue:break
            part,timestamp,key,marker=self.video_queue.pop(0)
            header=audio_header(self.video_sequence,timestamp,self.call['local_uid'],self.twseq,marker,98 if key else 97)
            try:packet=self.vtx.protect(header,part,self.video_roc)
            except ValueError:
                self.failed('media_nonce_limit');return
            if self.send(struct.pack('<BI',13,self.replies[self.chosen])+packet):self.stats['video_sent']+=1
            self.video_sequence=(self.video_sequence+1)&65535
            if not self.video_sequence:self.video_roc+=1
            self.twseq=(self.twseq+1)&65535;self.next_video=now+.012

    def close_audio(self):
        for name,key in (('playback','play_xruns'),('capture','cap_xruns')):
            value=getattr(self,name,None)
            if value:
                try:self.stats[key]=int(value.xruns())
                except Exception:pass
        for name in ('capture','playback','codec','dsp'):
            value=getattr(self,name,None)
            if value:
                value.close();setattr(self,name,None)

    def stop(self):
        self.active=False;self.close_audio()
        if self.sock:
            if self.call:
                for address,token in self.replies.items():
                    self.send(control_header(3,self.call['local_uid'],token)+struct.pack('<I',self.call['id']),address)
            self.sock.close();self.sock=None
        self.call=None

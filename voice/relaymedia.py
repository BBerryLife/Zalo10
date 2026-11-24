"""Experimental phone-local 1:1 Zalo UDP/SRTP/Opus audio transport.

Protocol adapted from VN-Linux-Family/zalo-for-linux (BSD-3-Clause).
Does not record audio, save keys, or depend on a desktop sound server.
"""
import os
import socket
import struct
import time
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
        self.stats={'video_input':0,'video_key_input':0,'video_no_key':0,'video_queue_drop':0,'video_sent':0,'udp_errors':0,"sent_audio_packets":0,"received_audio_packets":0,"dropped_auth_packets":0,"concealed_frames":0,"dropped_capture_frames":0}

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
        self.capture_buffer=b'';self.playback_buffer=b'';self.jitter={};self.expected=None;self.play_at=None
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
                if self.expected is None:
                    self.expected=self.rx.last_index;self.play_at=now+0.10
                if self.rx.last_index>=self.expected:
                    if len(self.jitter)>=64:
                        self.jitter.clear();self.expected=self.rx.last_index;self.play_at=now+0.10
                    self.jitter[self.rx.last_index]=plain
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
            self.playback=Audio(os.path.join(self.root,'libq10audio.so'),False)
            self.capture=Audio(os.path.join(self.root,'libq10audio.so'),True)
        except Exception:
            self.close_audio()
            raise
        self.active=True;now=self.clock();self.last_rx=now
        self.next_ping=now;self.next_status=now;self.next_report=now+1;self.next_twcc=now+0.05
        self.send(control_header(32,self.call['local_uid'],self.replies[self.chosen],subcmd=8,magic=1)+struct.pack('<II',4,0))
        for address,token in self.replies.items():
            if address!=self.chosen:
                self.send(control_header(3,self.call['local_uid'],token)+struct.pack('<I',self.call['id']),address)

    def audio_tick(self,now):
        for _ in range(8):
            data=self.capture.read()
            if isinstance(data,bytes) and data:
                self.capture_buffer+=data
            elif data in (0,-11) or data==b'':
                break
            else:
                self.failed('microphone_io_error_%d' % data);return
        # Keep normal scheduler bursts (up to 120 ms) intact; discard only
        # genuinely stale speech. Each 640-byte unit is one 20-ms Opus frame.
        if len(self.capture_buffer)>640*6:
            dropped=(len(self.capture_buffer)//640-6)*640
            self.capture_buffer=self.capture_buffer[dropped:]
            self.stats['dropped_capture_frames']+=dropped//640
            self.timestamp=(self.timestamp+(dropped//640)*320)&0xffffffff
        for _ in range(min(3,len(self.capture_buffer)//640)):
            frame=self.capture_buffer[:640];self.capture_buffer=self.capture_buffer[640:]
            frame=self.dsp.capture(frame)
            if self.muted:frame=b'\x00'*640
            opus=self.codec.encode(frame)
            header=audio_header(self.sequence,self.timestamp,self.call['local_uid'],self.twseq)
            packet=self.tx.protect(header,opus,self.roc)
            self.twseq=(self.twseq+1)&65535
            self.send(wrap_audio(packet,self.replies[self.chosen]))
            self.stats['sent_audio_packets']+=1;self.sent_octets+=len(opus)
            self.sequence=(self.sequence+1)&65535
            if self.sequence==0:self.roc+=1
            self.timestamp=(self.timestamp+320)&0xffffffff
        if self.play_at is not None and now-self.play_at>0.12:
            # A delayed process must catch up to current speech, not play an
            # ever-growing tail of old packets after a scheduling pause.
            self.expected=max(self.expected,self.rx.highest-3)
            self.jitter=dict((index,value) for index,value in self.jitter.items() if index>=self.expected)
            self.play_at=now-0.04
        for _ in range(3):
            if self.play_at is None or now<self.play_at:
                break
            packet=self.jitter.pop(self.expected,None)
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
        if self.playback_buffer:
            written=self.playback.write(self.playback_buffer[:3840])
            if written>0:
                self.dsp.playback(self.playback_buffer[:written])
                self.playback_buffer=self.playback_buffer[written:]
            elif written not in (0,-11):
                self.failed('speaker_io_error_%d' % written)

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

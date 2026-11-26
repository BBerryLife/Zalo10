"""Phone-local voice primitives for Python 3.2 on rooted BB10.

Protocol portions adapted from VN-Linux-Family/zalo-for-linux (BSD-3-Clause).
See THIRD_PARTY.md and LICENSES. No Node, desktop IPC, or remote bridge.
This module alone is NOT a working Zalo client.
"""
import ctypes as C
import hashlib
import hmac
import os
import struct


def equal_tag(left, right):
    if len(left) != len(right):
        return False
    mismatch = 0
    for a, b in zip(bytearray(left), bytearray(right)):
        mismatch |= a ^ b
    return mismatch == 0


def xor_bytes(left, right):
    return bytes(bytearray(a ^ b for a, b in zip(bytearray(left), bytearray(right))))


class AESKey(C.Structure):
    _fields_ = [("round_keys", C.c_uint32 * 60), ("rounds", C.c_int)]


class AES:
    def __init__(self, key, library=None):
        if len(key) != 16:
            raise ValueError("AES-128 requires 16 bytes")
        self.lib = C.CDLL(library or ("/usr/lib/libcrypto.so.2" if os.name != "nt" else "libcrypto-3-x64.dll"))
        self.lib.AES_set_encrypt_key.argtypes = [C.c_void_p, C.c_int, C.POINTER(AESKey)]
        self.lib.AES_set_encrypt_key.restype = C.c_int
        self.lib.AES_encrypt.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(AESKey)]
        self.lib.AES_encrypt.restype = None
        self.ctr = getattr(self.lib, 'AES_ctr128_encrypt', None)
        if self.ctr:
            self.ctr.argtypes = [C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(AESKey),C.c_void_p,C.c_void_p,C.POINTER(C.c_uint)]
            self.ctr.restype = None
        self.key = AESKey()
        if self.lib.AES_set_encrypt_key(key, 128, C.byref(self.key)):
            raise RuntimeError("AES key setup failed")

    def transform(self, iv, value):
        if not self.ctr:
            return xor_bytes(value, self.stream(iv, len(value)))
        output = C.create_string_buffer(len(value))
        counter = C.create_string_buffer(iv, 16)
        scratch = C.create_string_buffer(16)
        offset = C.c_uint(0)
        self.ctr(value, output, len(value), C.byref(self.key), counter, scratch, C.byref(offset))
        return output.raw

    def block(self, value):
        if len(value) != 16:
            raise ValueError("AES block must contain 16 bytes")
        output = C.create_string_buffer(16)
        self.lib.AES_encrypt(value, output, C.byref(self.key))
        return output.raw

    def stream(self, iv, length):
        if len(iv) != 16 or not 0 <= length <= 65536:
            raise ValueError("Invalid CTR request")
        initial = int.from_bytes(iv, "big")
        return b"".join(self.block(((initial+i) % (1 << 128)).to_bytes(16,"big"))
                        for i in range((length+15)//16))[:length]


def derive_keys(master, library=None):
    if len(master) != 30:
        raise ValueError("SRTP master key and salt must contain 30 bytes")
    aes = AES(master[:16], library)
    keys = []
    for label, size in ((0, 16), (1, 20), (2, 14)):
        value = bytearray(master[16:] + b"\x00\x00")
        value[7] ^= label
        keys.append(aes.stream(bytes(value), size))
    return tuple(keys)


def audio_header(seq, timestamp, ssrc, twseq, marker=False, payload_type=112):
    if not 0 <= seq <= 65535 or not 0 <= ssrc <= 0xffffffff:
        raise ValueError("Invalid RTP identifier")
    return struct.pack("!BBHIIHHBH B", 0x90, payload_type | (0x80 if marker else 0), seq,
        timestamp & 0xffffffff, ssrc, 0xbede, 1, 0x51, twseq & 65535, 0)


def parse_rtp(packet):
    if len(packet) < 12 or packet[0] >> 6 != 2:
        raise ValueError("Invalid RTP header")
    offset = 12+4*(packet[0] & 15)
    if offset > len(packet):
        raise ValueError("Truncated RTP CSRC")
    if packet[0] & 16:
        if offset+4 > len(packet):
            raise ValueError("Truncated RTP extension")
        offset += 4+4*struct.unpack_from("!H",packet,offset+2)[0]
    if offset > len(packet):
        raise ValueError("Truncated RTP extension body")
    return struct.unpack_from("!H",packet,2)[0], struct.unpack_from("!I",packet,8)[0], offset


class SRTP:
    """One fixed SSRC per context; 128-packet anti-replay window.

    Do not reuse a sender context/key/SSRC after resetting its sequence.
    Receiver state changes only after successful authentication.
    """
    def __init__(self, master, ssrc, library=None, payload_types=(112,), nonce_guard=None, stream='audio'):
        enc, self.auth, self.salt = derive_keys(master, library)
        self.aes = AES(enc, library)
        self.ssrc = ssrc
        self.sent_index = -1
        self.highest = -1
        self.window = 0
        self.last_index = None
        self.payload_types=tuple(payload_types);self.nonce_guard=nonce_guard;self.stream=stream

    def crypt(self, value, index):
        iv_number = (int.from_bytes(self.salt, "big") << 16) ^ (self.ssrc << 64) ^ (index << 16)
        return self.aes.transform(iv_number.to_bytes(16,"big"), value)

    def protect(self, header, payload, roc=0):
        seq, ssrc, size = parse_rtp(header)
        if size != len(header) or ssrc != self.ssrc or not 0 <= roc <= 0xffffffff:
            raise ValueError("Invalid sender RTP context")
        index = roc*65536+seq
        if index <= self.sent_index:
            raise ValueError("Refusing SRTP index/key reuse")
        if self.nonce_guard is not None:
            for stream,interval in self.nonce_guard.items():
                if stream!=self.stream and interval[0]<=index<=interval[1]:
                    raise ValueError('Refusing cross-stream SRTP nonce reuse')
        if not payload or len(payload) > 4096:
            raise ValueError("Invalid audio payload")
        body = header + self.crypt(payload,index)
        tag = hmac.new(self.auth,body+struct.pack("!I",roc),hashlib.sha1).digest()[:10]
        self.sent_index = index
        if self.nonce_guard is not None:
            low=self.nonce_guard.get(self.stream,(index,index))[0]
            self.nonce_guard[self.stream]=(low,index)
        return body+tag

    def unprotect(self, packet):
        if len(packet) > 8192:
            return None
        try:
            seq, ssrc, offset = parse_rtp(packet)
        except (ValueError, struct.error):
            return None
        if ssrc != self.ssrc or len(packet)-offset <= 10 or packet[1] & 127 not in self.payload_types:
            return None
        roc = 0 if self.highest < 0 else self.highest//65536
        if self.highest >= 0:
            last = self.highest & 65535
            if last < 32768 and seq-last > 32768:
                roc -= 1
            elif last >= 32768 and last-seq > 32768:
                roc += 1
        if not 0 <= roc <= 0xffffffff:
            return None
        index = roc*65536+seq
        distance = self.highest-index
        if self.highest >= 0 and (distance >= 128 or (distance >= 0 and self.window & (1 << distance))):
            return None
        body, tag = packet[:-10], packet[-10:]
        expected = hmac.new(self.auth, body+struct.pack("!I",roc),hashlib.sha1).digest()[:10]
        if not equal_tag(tag, expected):
            return None
        payload = self.crypt(packet[offset:-10],index)
        if packet[0] & 32:
            pad = payload[-1]
            if not 0 < pad <= len(payload):
                return None
            payload = payload[:-pad]
        if index > self.highest:
            self.window = ((self.window << min(index-self.highest,128)) | 1) & ((1 << 128)-1)
            self.highest = index
        else:
            self.window |= 1 << distance
        self.last_index = index
        return payload


def control_header(cmd, uid=0, token=0, seq=0, subcmd=0, magic=0x7e):
    return struct.pack("<BBIIIIHB",1,magic,0,seq,uid,token,cmd,subcmd)


def u16string(value):
    value = value.encode("utf-8") if isinstance(value,str) else value
    if len(value) > 65535:
        raise ValueError("ZRTP string too long")
    return struct.pack("<H",len(value))+value


def init_packet(uid, peer_uid, call_id, session, caller=True):
    if not 0 < call_id < 0x80000000:
        raise ValueError("Zalo callId must fit positive int32")
    return control_header(11 if caller else 12,uid,subcmd=2)+struct.pack("<II",call_id,peer_uid)+u16string(session)


def wrap_audio(packet, token):
    return struct.pack("<BI",3,token)+packet


def unwrap_relay(packet):
    if not packet:
        raise ValueError("Empty relay datagram")
    kind = packet[0]
    if kind in (4,14):
        if len(packet) < 13:
            raise ValueError("Truncated downlink")
        return {"kind":kind,"payload":packet[1:]}
    if kind in (3,5,7,13,15):
        if len(packet) < 5:
            raise ValueError("Truncated media prefix")
        return {"kind":kind,"token":struct.unpack_from("<I",packet,1)[0],"payload":packet[5:]}
    if kind not in (1,2) or len(packet) < 21:
        raise ValueError("Unsupported or truncated relay packet")
    fields = struct.unpack_from("<BBIIIIHB",packet)
    if fields[1] not in (1,0x7e):
        raise ValueError("Invalid control magic")
    result = dict(zip(("kind","magic","field02","seq","uid","token","cmd","subcmd"),fields))
    data = packet[21:]
    result["payload"] = data
    if kind == 2 and result["cmd"] in (11,12):
        if len(data) < 4:
            raise ValueError("Truncated init result")
        result["result"] = struct.unpack_from("<I",data)[0]
        if result["result"] == 0:
            if len(data) < 14:
                raise ValueError("Truncated 1:1 init success")
            result["call_id"], result["server_token"], length = struct.unpack_from("<IIH",data,4)
            if len(data) != 14+length:
                raise ValueError("Invalid public address length")
            result["public_address"] = data[14:].decode("ascii")
    return result


DECODER_GAIN_Z4=-1536   # Z4: -6 dB


class Opus:
    """Use a phone-local libopus, without bundling another app's binary."""
    def __init__(self, path):
        self.lib = C.CDLL(path)
        self.lib.opus_encoder_create.argtypes = [C.c_int,C.c_int,C.c_int,C.POINTER(C.c_int)]
        self.lib.opus_encoder_create.restype = C.c_void_p
        self.lib.opus_decoder_create.argtypes = [C.c_int,C.c_int,C.POINTER(C.c_int)]
        self.lib.opus_decoder_create.restype = C.c_void_p
        self.lib.opus_encode.argtypes = [C.c_void_p,C.c_void_p,C.c_int,C.c_void_p,C.c_int32]
        self.lib.opus_encode.restype = C.c_int32
        self.lib.opus_decode.argtypes = [C.c_void_p,C.c_void_p,C.c_int32,C.c_void_p,C.c_int,C.c_int]
        self.lib.opus_decode.restype = C.c_int
        self.lib.opus_encoder_destroy.argtypes = [C.c_void_p]
        self.lib.opus_decoder_destroy.argtypes = [C.c_void_p]
        self.lib.opus_encoder_ctl.argtypes = [C.c_void_p,C.c_int]
        self.lib.opus_encoder_ctl.restype = C.c_int
        self.encoder = self.decoder = None
        error = C.c_int()
        self.encoder = self.lib.opus_encoder_create(16000,1,2048,C.byref(error))
        if not self.encoder or error.value:
            self.close()
            raise RuntimeError("Opus encoder creation failed")
        self.decoder = self.lib.opus_decoder_create(16000,1,C.byref(error))
        if not self.decoder or error.value:
            self.close()
            raise RuntimeError("Opus decoder creation failed")
        # Z4: the call log showed the received speech at full scale (32767) almost all the time, i.e. clipped:
        # play it 6 dB quieter (OPUS_SET_GAIN, in 1/256 dB). Not fatal if the library refuses.
        try:
            self.lib.opus_decoder_ctl.argtypes=[C.c_void_p,C.c_int,C.c_int]
            self.lib.opus_decoder_ctl.restype=C.c_int
            self.lib.opus_decoder_ctl(self.decoder,4034,DECODER_GAIN_Z4)
        except Exception:
            pass
        # Stable 20ms voice, with spare recovery information for packet loss.
        for request,value in ((4002,24000),(4010,5),(4012,1),(4014,5),(4016,0)):
            if self.lib.opus_encoder_ctl(self.encoder,request,C.c_int(value)) < 0:
                self.close()
                raise RuntimeError("Opus voice configuration failed")

    def encode(self, pcm):
        if len(pcm) != 640:
            raise ValueError("Expected 20ms PCM16LE, 16000Hz mono")
        output = C.create_string_buffer(1275)
        count = self.lib.opus_encode(self.encoder,pcm,320,output,1275)
        if count < 0:
            raise RuntimeError("Opus encode failed: %d" % count)
        return output.raw[:count]

    def decode(self, packet, recover_previous=False):
        if packet is not None and not 0 < len(packet) <= 1275:
            raise ValueError("Invalid Opus frame")
        output = C.create_string_buffer(3840)
        count = self.lib.opus_decode(self.decoder,packet,len(packet) if packet else 0,output,320 if recover_previous or not packet else 1920,int(recover_previous))
        if count < 0:
            raise RuntimeError("Opus decode failed: %d" % count)
        return output.raw[:count*2]

    def close(self):
        if self.encoder:
            self.lib.opus_encoder_destroy(self.encoder)
            self.encoder = None
        if self.decoder:
            self.lib.opus_decoder_destroy(self.decoder)
            self.decoder = None


# Z20: earpiece devices, opened as a voice call at 16 kHz (48 kHz on this route stalls: Z19 log 24/11)
# earpiece test 24/11: voicep -6 dB best, pcmC1D0p -6 dB next, handsetp crackled
EAR_Z20=[('pcmC1D0p',0),('voicep',0),('handsetp',0)]
RING_Z25='ring48'

class AudioOpenError(RuntimeError):
    def __init__(self, code, stage):
        self.code,self.stage=code,stage
        RuntimeError.__init__(self,"QNX PCM open failed: %d, stage %d" % (code,stage))


class VoiceDSP:
    def __init__(self,path):
        self.lib=C.CDLL(path)
        self.lib.q10_dsp_create.restype=C.c_void_p
        self.lib.q10_dsp_destroy.argtypes=[C.c_void_p]
        self.lib.q10_dsp_playback.argtypes=[C.c_void_p,C.c_void_p,C.c_int]
        self.lib.q10_dsp_capture.argtypes=[C.c_void_p,C.c_void_p,C.c_void_p,C.c_int]
        self.state=self.lib.q10_dsp_create()
        if not self.state:raise RuntimeError('Voice processor allocation failed')
        self.reference=b''

    def playback(self,pcm):
        self.reference+=pcm
        while len(self.reference)>=640:
            frame=self.reference[:640];self.reference=self.reference[640:]
            self.lib.q10_dsp_playback(self.state,frame,320)

    def capture(self,pcm):
        if len(pcm)!=640:raise ValueError('Expected 20ms voice frame')
        output=C.create_string_buffer(640)
        if self.lib.q10_dsp_capture(self.state,pcm,output,320):
            raise RuntimeError('Voice processing failed')
        return output.raw

    def close(self):
        if self.state:
            self.lib.q10_dsp_destroy(self.state);self.state=None


class Audio:
    # Z2: video=True opens the speaker/microphone as a video chat (loudspeaker); a voice call opens them as a
    # voice call (earpiece) - needs the Z2 libq10audio.so (q10_audio_open_type); older library: as before.
    def __init__(self, path, capture=False, ringtone=False, video=False, speaker=False, ring_mode=None):
        self.lib = C.CDLL(path)
        self.lib.q10_audio_open.argtypes = [C.POINTER(C.c_void_p),C.c_int]
        self.lib.q10_audio_open.restype = C.c_int
        self.lib.q10_audio_transfer.argtypes = [C.c_void_p,C.c_void_p,C.c_int]
        self.lib.q10_audio_transfer.restype = C.c_int
        self.lib.q10_audio_fd.argtypes = [C.c_void_p]
        self.lib.q10_audio_fd.restype = C.c_int
        self.lib.q10_audio_close.argtypes = [C.c_void_p]
        self.handle = C.c_void_p()
        if ringtone:
            # Z25: the ringtone crackled (Z24, anh 24/11): it went to the loudspeaker at 16 kHz, the path that crackled
            # in the speaker tests. Play it at 48 kHz through our own filter, like the clean Loa button.
            # RING_Z25 'ring48' = phone ringtone route at 48 kHz; 'spk' = the Loa button device (handsetp 48 kHz).
            # Override in Documents/zalo10/ring_device.txt: ring48, spk or off (= Z24 way, 16 kHz).
            mode=RING_Z25
            try:
                with open('/accounts/1000/shared/documents/zalo10/ring_device.txt') as f:
                    v=f.read().strip()
                if v in ('ring48','spk','off'):mode=v
            except Exception:
                pass
            if ring_mode in ('ring48','spk','off'):mode=ring_mode   # C1: the caller can pick the route (ringback)
            result=-1
            if mode=='spk' and hasattr(self.lib,'q10_audio_open_name_rate'):
                self.lib.q10_audio_open_name_rate.argtypes=[C.POINTER(C.c_void_p),C.c_int,C.c_char_p,C.c_int]
                self.lib.q10_audio_open_name_rate.restype=C.c_int
                result=self.lib.q10_audio_open_name_rate(C.byref(self.handle),0,b'/dev/snd/handsetp',1)
                if result>=0:self.audio_type='ring:spk'
            if result<0 and mode!='off' and hasattr(self.lib,'q10_audio_open_type_rate'):
                self.lib.q10_audio_open_type_rate.argtypes=[C.POINTER(C.c_void_p),C.c_int,C.c_int,C.c_int]
                self.lib.q10_audio_open_type_rate.restype=C.c_int
                result=self.lib.q10_audio_open_type_rate(C.byref(self.handle),0,1,1)
                if result>=0:self.audio_type='ring:48'
            if result<0:
                self.lib.q10_audio_open_ringtone.argtypes=[C.POINTER(C.c_void_p)]
                self.lib.q10_audio_open_ringtone.restype=C.c_int
                result=self.lib.q10_audio_open_ringtone(C.byref(self.handle))
                self.audio_type='ring:16'
        elif hasattr(self.lib,'q10_audio_open_type'):
            self.lib.q10_audio_open_type.argtypes=[C.POINTER(C.c_void_p),C.c_int,C.c_int]
            self.lib.q10_audio_open_type.restype=C.c_int
            self.audio_type=4 if video else 0
            result=-1
            if capture and not video:
                # Z12: the microphone opened through audio_manager is gated by the phone's voice processing
                # (~10 ms holes every ~20 ms, mic test 24/11). Open the hardware capture device directly instead.
                # Device name from Documents/zalo10/mic_device.txt (default pcmC0D0c; "off" = old way).
                # Z14: mic test 24/11: voicec and pcmC1D0c give clean sound (no 16-sample block errors, like
                # Voice Notes); pcmC0D0c (Z12/Z13) has them. Try voicec first, then pcmC1D0c, then pcmC0D0c.
                names=['voicec','pcmC1D0c','pcmC0D0c']
                try:
                    with open('/accounts/1000/shared/documents/zalo10/mic_device.txt') as f:
                        v=f.read().strip()
                    if v:names=[v]
                except Exception:
                    pass
                if names!=['off'] and hasattr(self.lib,'q10_audio_open_name'):
                    self.lib.q10_audio_open_name.argtypes=[C.POINTER(C.c_void_p),C.c_int,C.c_char_p]
                    self.lib.q10_audio_open_name.restype=C.c_int
                    for name in names:
                        result=self.lib.q10_audio_open_name(C.byref(self.handle),1,('/dev/snd/'+name).encode())
                        if result>=0:
                            self.audio_type='dev:'+name
                            break
            if not capture and not video and not speaker and hasattr(self.lib,'q10_audio_open_voice_name_rate'):
                # Z19: earpiece by default. The same clean device as Z17b (handsetp, 48 kHz with our filter), but
                # registered with the audio manager as a voice call, which the phone routes to the earpiece.
                # Z17b opened it without the audio manager and it played on the loudspeaker (anh, 24/11).
                # If none opens, the old voice route below (Z14, also earpiece) is used.
                self.lib.q10_audio_open_voice_name_rate.argtypes=[C.POINTER(C.c_void_p),C.c_char_p,C.c_int]
                self.lib.q10_audio_open_voice_name_rate.restype=C.c_int
                for name,t48 in EAR_Z20:
                    result=self.lib.q10_audio_open_voice_name_rate(C.byref(self.handle),('/dev/snd/'+name).encode(),t48)
                    if result>=0:
                        self.audio_type='ear:'+name
                        break
            if not capture and not video and speaker and hasattr(self.lib,'q10_audio_open_name_rate'):
                # Z19: loudspeaker button = the Z17b way (device opened directly)
                # Z15: speaker test 24/11: pcmC1D0p (48 kHz, our own filter) and voicep (16 kHz) sounded best.
                # Override in Documents/zalo10/spk_device.txt: "name:48", "name:16" or "off" (old way).
                # Z16: handsetp = earpiece (Z15b voicep went to the loudspeaker)
                spk=[('handsetp',1),('handsetp',0),('voicep',0)]
                try:
                    with open('/accounts/1000/shared/documents/zalo10/spk_device.txt') as f:
                        v=f.read().strip()
                    if v=='off':spk=[]
                    elif v:
                        n,_,r=v.partition(':');spk=[(n,0 if r=='16' else 1)]
                except Exception:
                    pass
                self.lib.q10_audio_open_name_rate.argtypes=[C.POINTER(C.c_void_p),C.c_int,C.c_char_p,C.c_int]
                self.lib.q10_audio_open_name_rate.restype=C.c_int
                for name,t48 in spk:
                    result=self.lib.q10_audio_open_name_rate(C.byref(self.handle),0,('/dev/snd/'+name).encode(),t48)
                    if result>=0:
                        self.audio_type='spk:'+name
                        break
            if result<0:
                result=self.lib.q10_audio_open_type(C.byref(self.handle),int(capture),self.audio_type if isinstance(self.audio_type,int) else 0)
        else:result = self.lib.q10_audio_open(C.byref(self.handle),int(capture))
        if result < 0:
            raise AudioOpenError(result,self.lib.q10_audio_last_stage())
        if not capture and not video and not ringtone and hasattr(self.lib,'q10_audio_set_eq'):
            # Z17: warm EQ for the earpiece (low-mid lift, treble cut). Level 0 = off, 1 light, 2 medium, 3 strong.
            # Override in Documents/zalo10/eq_level.txt
            lv=2
            try:
                with open('/accounts/1000/shared/documents/zalo10/eq_level.txt') as f:
                    lv=int(f.read().strip())
            except Exception:
                pass
            self.lib.q10_audio_set_eq.argtypes=[C.c_void_p,C.c_int]
            self.lib.q10_audio_set_eq.restype=C.c_int
            if self.lib.q10_audio_set_eq(self.handle,lv)==0:
                self.audio_type='%s+eq%d'%(self.audio_type,lv)
            if not speaker and hasattr(self.lib,'q10_audio_set_gain'):
                # Z20: earpiece 6 dB lower (earpiece test 24/11: the earpiece path crackles on loud speech)
                self.lib.q10_audio_set_gain.argtypes=[C.c_void_p,C.c_int]
                self.lib.q10_audio_set_gain.restype=C.c_int
                if self.lib.q10_audio_set_gain(self.handle,8192)==0:
                    self.audio_type+='-6dB'

    def fd(self):
        return self.lib.q10_audio_fd(self.handle)

    def xruns(self):
        self.lib.q10_audio_xruns.argtypes=[C.c_void_p]
        self.lib.q10_audio_xruns.restype=C.c_uint
        return self.lib.q10_audio_xruns(self.handle)

    def state(self):
        self.lib.q10_audio_state.argtypes=[C.c_void_p]
        self.lib.q10_audio_state.restype=C.c_int
        return self.lib.q10_audio_state(self.handle)

    def diag(self,what):
        # Z13: counts from the audio library (4 bad microphone block runs bridged, 5 lone jumps smoothed)
        if not hasattr(self.lib,'q10_audio_diag'):return -1
        self.lib.q10_audio_diag.argtypes=[C.c_void_p,C.c_int]
        self.lib.q10_audio_diag.restype=C.c_int
        return self.lib.q10_audio_diag(self.handle,what)

    def rate(self):
        # Z15: sample rate the device runs at now (48000 while our own filter is used)
        if not hasattr(self.lib,'q10_audio_rate'):return 16000
        self.lib.q10_audio_rate.argtypes=[C.c_void_p]
        self.lib.q10_audio_rate.restype=C.c_int
        return self.lib.q10_audio_rate(self.handle)

    def queued(self):
        # Z3: bytes waiting in the device buffer; None with an older library
        if not hasattr(self.lib,'q10_audio_queued'):return None
        self.lib.q10_audio_queued.argtypes=[C.c_void_p]
        self.lib.q10_audio_queued.restype=C.c_int
        value=self.lib.q10_audio_queued(self.handle)
        return value if value>=0 else None

    def read(self):
        output = C.create_string_buffer(640)
        count = self.lib.q10_audio_transfer(self.handle,output,640)
        if count < 0:
            return count
        return output.raw[:count]

    def write(self, pcm):
        return self.lib.q10_audio_transfer(self.handle,pcm,len(pcm))

    def close(self):
        if self.handle:
            self.lib.q10_audio_close(self.handle)
            self.handle = C.c_void_p()

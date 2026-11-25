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

    def tune(self,noise_db):
        """Soften the noise suppressor that q10_dsp_create hard-wires at -18 dB (it thins out quiet
        speech and adds a watery artefact). q10_dsp_create allocates {SpeexEchoState*,
        SpeexPreprocessState*} as two consecutive pointers; speex_preprocess_ctl is exported by the
        same library. Returns True if applied; any doubt means leave the stock settings alone."""
        try:
            if noise_db is None or not self.state:return False
            pointers=(C.c_void_p*2).from_address(self.state)
            if not pointers[0] or not pointers[1]:return False
            control=self.lib.speex_preprocess_ctl
            control.argtypes=[C.c_void_p,C.c_int,C.c_void_p]
            control.restype=C.c_int
            value=C.c_int(int(noise_db))
            return control(pointers[1],18,C.byref(value))==0     # SPEEX_PREPROCESS_SET_NOISE_SUPPRESS
        except Exception:
            return False

    def close(self):
        if self.state:
            self.lib.q10_dsp_destroy(self.state);self.state=None


class Audio:
    def __init__(self, path, capture=False, ringtone=False, audio_type=None):
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
            self.lib.q10_audio_open_ringtone.argtypes=[C.POINTER(C.c_void_p)]
            self.lib.q10_audio_open_ringtone.restype=C.c_int
            result=self.lib.q10_audio_open_ringtone(C.byref(self.handle))
        elif audio_type is not None:
            result=self.open_typed(capture,audio_type)
        else:result = self.lib.q10_audio_open(C.byref(self.handle),int(capture))
        if result < 0:
            raise AudioOpenError(result,self.lib.q10_audio_last_stage())

    def open_typed(self,capture,audio_type):
        """Open with an explicit audio-manager type (this selects the output route).

        The shipped ARM build of q10_audio_open is exactly `mov r2,#4; b common_open`, and
        common_open(out, capture, type) is the next instruction. Verify those 8 bytes, then call
        common_open directly; on any mismatch fall back to the stock open (type 4).
        """
        stock=lambda:self.lib.q10_audio_open(C.byref(self.handle),int(capture))
        try:
            if C.sizeof(C.c_void_p)!=4:return stock()
            base=C.cast(self.lib.q10_audio_open,C.c_void_p).value
            if C.string_at(base,8)!=bytes(bytearray((0x04,0x20,0xa0,0xe3,0xff,0xff,0xff,0xea))):return stock()
            opener=C.CFUNCTYPE(C.c_int,C.POINTER(C.c_void_p),C.c_int,C.c_int)(base+8)
            return opener(C.byref(self.handle),int(capture),int(audio_type))
        except Exception:
            return stock()

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

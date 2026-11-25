"""Phone-local system call UI + ringtone. Requires this rooted Q10's service.

The lock-screen popup exposes only call actions, never account/message data.
All PCM is an original generated ringtone, not recorded audio.
"""
import ctypes as C,os,threading,time,wave
try:import audioop
except ImportError:audioop=None
from q10media import Audio,AudioOpenError

RING_RATE=16000      # q10_audio_open_ringtone opens the speaker as 16 kHz mono S16_LE, 640-byte fragments

def load_ringtone(path):
    """Return the ringtone as raw PCM16LE mono 16 kHz, whatever the WAV looks like.

    The old loader skipped a fixed 44-byte header and fed the rest to the speaker unchanged. A WAV
    with extra chunks (LIST/INFO) or a different rate/channel count then played as noise: the file
    was 44.1 kHz stereo, i.e. ~5x too fast with the channels interleaved as consecutive samples."""
    reader=wave.open(path,'rb')
    try:
        channels,width,rate=reader.getnchannels(),reader.getsampwidth(),reader.getframerate()
        data=reader.readframes(reader.getnframes())
    finally:
        reader.close()
    if width!=2 or channels not in (1,2) or not data:raise ValueError('Invalid bundled ringtone')
    if channels==2 or rate!=RING_RATE:
        if not audioop:raise ValueError('Ringtone needs conversion but audioop is missing')
        if channels==2:data=audioop.tomono(data,2,0.5,0.5)
        if rate!=RING_RATE:data=audioop.ratecv(data,2,1,rate,RING_RATE,None)[0]
    data=data[:len(data)//640*640]          # whole 20 ms fragments so the loop wraps cleanly
    if not data:raise ValueError('Invalid bundled ringtone')
    return data

def log_line(text):
    """One line on stderr: the app log shows it as `[Voice] engine: ...`."""
    try:os.write(2,(text+'\n').encode('ascii','replace'))
    except OSError:pass


class RingPump(threading.Thread):
    """Feeds the ringtone to the speaker from its own thread.

    The speaker queue is only 4 x 20 ms. When the ringtone was written from the engine loop, every
    dialog call or other stall of 80 ms or more (the loop logged several >100 ms while ringing)
    ran the queue dry, and a pure tone turns each gap into a loud click. ctypes releases the GIL
    during the native write, so this thread keeps the queue full while the loop is busy."""
    def __init__(self,audio,tone):
        threading.Thread.__init__(self)
        self.daemon=True
        self.audio,self.tone,self.position=audio,tone,0
        self.halt=threading.Event()
        self.bytes=0;self.error=None;self.slow=0;self.worst=0.0

    def run(self):
        last=time.time()
        while not self.halt.is_set():
            for _ in range(4):
                n=self.audio.write(self.tone[self.position:self.position+640])
                if n>0:
                    self.position=(self.position+n)%len(self.tone);self.bytes+=n
                elif n in (0,-11):break
                else:
                    self.error=n;return
            now=time.time();gap=now-last;last=now
            if gap>self.worst:self.worst=gap
            if gap>0.06:self.slow+=1              # this thread itself was held up for more than 60 ms
            self.halt.wait(0.01)


class CallAlert:
    def __init__(self,root,command,report):
        self.root,self.command,self.report=root,command,report
        self.lib=C.CDLL('libbps.so.3');self.lib.bps_initialize();self.lib.dialog_request_events(0)
        self.dialog=None;self.audio=None;self.pump=None;self.state='idle';self.name='';self.video=False
        self.test_until=None;self.foreground=False;self.popup_kind=None
        self.reopen_at=None
        self.lib.dialog_create_alert.argtypes=[C.POINTER(C.c_void_p)]
        for name in ('dialog_show','dialog_cancel','dialog_destroy','dialog_event_get_selected_index'):
            getattr(self.lib,name).argtypes=[C.c_void_p]
        self.lib.dialog_event_get_dialog_instance.argtypes=[C.c_void_p]
        self.lib.dialog_event_get_dialog_instance.restype=C.c_void_p
        self.lib.dialog_event_get_error.argtypes=[C.c_void_p];self.lib.dialog_event_get_error.restype=C.c_char_p
        self.lib.bps_event_get_domain.argtypes=[C.c_void_p]
        self.lib.bps_get_event.argtypes=[C.POINTER(C.c_void_p),C.c_int]
        for name in ('dialog_set_system','dialog_set_priority'):
            getattr(self.lib,name).argtypes=[C.c_void_p,C.c_bool]
        self.lib.dialog_set_cancel_required.argtypes=[C.c_void_p,C.c_bool]
        for name in ('dialog_set_title_text','dialog_set_alert_message_text'):
            getattr(self.lib,name).argtypes=[C.c_void_p,C.c_char_p]
        self.lib.dialog_add_button.argtypes=[C.c_void_p,C.c_char_p,C.c_bool,C.c_char_p,C.c_bool]
        self.tone=load_ringtone(os.path.join(root,'ringtone.wav'))

    def close_popup(self):
        if self.dialog:
            self.lib.dialog_cancel(self.dialog);self.lib.dialog_destroy(self.dialog);self.dialog=None
        self.popup_kind=None

    def popup(self,kind):
        if self.dialog and self.popup_kind==kind:return
        if kind=='incoming' and self.reopen_at and time.time()<self.reopen_at:return
        self.close_popup();d=C.c_void_p()
        created=self.lib.dialog_create_alert(C.byref(d))
        if created:
            if kind=='incoming':self.reopen_at=time.time()+1
            self.report({'event':'alert_status','create':created});return
        self.dialog=d;self.popup_kind=kind
        system=self.lib.dialog_set_system(d,True);priority=self.lib.dialog_set_priority(d,True)
        self.lib.dialog_set_cancel_required(d,True)
        if kind=='test':
            title='Zalo 1.0 — Kiểm tra chuông';body='Kiểm tra cục bộ trên Q10, không phải cuộc gọi thật.'
            buttons=[('Đóng','close')]
        elif kind=='incoming':
            title='Zalo 1.0 — Cuộc gọi video' if self.video else 'Zalo 1.0 — Cuộc gọi đến'
            body=self.name or 'Zalo';buttons=[('Từ chối','reject'),('Nhận','answer')]
        else:
            title='Zalo 1.0 — Đang trong cuộc gọi';body=self.name or 'Zalo'
            buttons=[('Kết thúc','end'),('Mở Zalo','open')]
        self.lib.dialog_set_title_text(d,title.encode('utf-8'));self.lib.dialog_set_alert_message_text(d,body.encode('utf-8'))
        for label,context in buttons:self.lib.dialog_add_button(d,label.encode('utf-8'),True,context.encode(),True)
        show=self.lib.dialog_show(d)
        if show:
            self.close_popup()
            if self.state=='incoming':self.reopen_at=time.time()+1
        self.report({'event':'alert_status','create':created,'system':system,'priority':priority,'show':show,'test':kind=='test','ring_audio':bool(self.audio)})

    def ringing(self,on):
        if on and not self.audio:
            try:
                self.audio=Audio(os.path.join(self.root,'libq10audio.so'),ringtone=True)
            except AudioOpenError as error:
                self.report({'event':'alert_status','ring_audio':False,'audio_error':error.code,'audio_stage':error.stage})
                return
            self.pump=RingPump(self.audio,self.tone);self.pump.start()
        elif not on and self.audio:
            pump,audio=self.pump,self.audio
            self.pump=None;self.audio=None
            pump.halt.set();pump.join(2.0)
            xruns=audio.xruns()
            self.report({'event':'alert_status','ring_finished':True,'ring_bytes':pump.bytes,'ring_xruns':xruns})
            log_line('ring: bytes=%d xruns=%d pump_slow=%d pump_worst=%dms'%(pump.bytes,xruns,pump.slow,int(pump.worst*1000)))
            if pump.is_alive():log_line('ring: pump did not stop, speaker left open')
            else:audio.close()

    def update(self,event,native_alert=False):
        previous=self.state;self.state=event.get('state','idle');self.video=bool(event.get('video'))
        if self.state!=previous or self.state=='idle':self.reopen_at=None
        if self.state=='incoming':self.name=event.get('name') or 'Zalo'
        elif self.state=='idle':self.name=''
        self.test_until=None
        self.ringing(self.state=='incoming' and not native_alert)
        if self.state=='incoming' and not native_alert:self.popup('incoming')
        elif self.state=='connected' and not self.foreground:self.popup('connected')
        else:self.close_popup()

    def set_foreground(self,value):
        if self.foreground==bool(value):return
        self.foreground=bool(value)
        if self.state=='incoming':return
        if value:self.reopen_at=None
        if self.state=='connected':
            if value:self.close_popup()
            else:self.popup(self.state)

    def test(self):
        if self.state!='idle':return
        self.ringing(True);self.test_until=time.time()+8;self.popup('test')

    def open_ui(self):
        # Navigator invocations need an application identity. The app's
        # native headless controller opens its GUI and retains the reply.
        self.reopen_at=time.time()+3;self.report({'event':'open_ui'})

    def tick(self):
        if self.reopen_at and time.time()>=self.reopen_at:
            self.reopen_at=None
            if self.state=='incoming':self.popup('incoming')
            elif self.state=='connected' and not self.foreground:self.popup('connected')
        if self.pump and self.pump.error is not None:
            error=self.pump.error;self.ringing(False)
            self.report({'event':'alert_status','ring_audio':False,'audio_error':error})
        if self.test_until and time.time()>=self.test_until:
            self.test_until=None;self.close_popup();self.ringing(False)
        for _ in range(8):
            event=C.c_void_p();self.lib.bps_get_event(C.byref(event),0)
            if not event.value:break
            if self.lib.bps_event_get_domain(event)!=self.lib.dialog_get_domain() or not self.dialog:continue
            if self.lib.dialog_event_get_dialog_instance(event)!=self.dialog.value:continue
            error=self.lib.dialog_event_get_error(event);selected=self.lib.dialog_event_get_selected_index(event)
            kind=self.popup_kind;self.close_popup()
            if error:
                if self.state=='incoming':self.reopen_at=time.time()+1
                continue
            if kind=='test':self.test_until=None;self.ringing(False)
            elif kind=='incoming':
                if selected==1:
                    self.command({'action':'answer'})
                    if self.video:self.open_ui()
                elif selected==0:self.command({'action':'hangup'})
                elif self.state=='incoming':self.reopen_at=time.time()+1
            elif kind=='connected':
                if selected==0:self.command({'action':'hangup'})
                elif selected==1:self.open_ui()

    def close(self):
        self.ringing(False);self.close_popup();self.lib.bps_shutdown()

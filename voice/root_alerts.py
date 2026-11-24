"""Phone-local system call UI + ringtone. Requires this rooted Q10's service.

The lock-screen popup exposes only call actions, never account/message data.
All PCM is an original generated ringtone, not recorded audio.
"""
import ctypes as C,os,time
from q10media import Audio,AudioOpenError

class CallAlert:
    def __init__(self,root,command,report):
        self.root,self.command,self.report=root,command,report
        self.lib=C.CDLL('libbps.so.3');self.lib.bps_initialize();self.lib.dialog_request_events(0)
        self.dialog=None;self.audio=None;self.position=0;self.state='idle';self.name='';self.video=False
        self.test_until=None;self.foreground=False;self.popup_kind=None
        self.reopen_at=None
        self.ring_bytes=0
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
        with open(os.path.join(root,'ringtone.wav'),'rb') as f:self.tone=f.read()[44:]
        if not self.tone or len(self.tone)%2:raise ValueError('Invalid bundled ringtone')

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
                self.audio=Audio(os.path.join(self.root,'libq10audio.so'),ringtone=True);self.position=0;self.ring_bytes=0
            except AudioOpenError as error:
                self.report({'event':'alert_status','ring_audio':False,'audio_error':error.code,'audio_stage':error.stage})
        elif not on and self.audio:
            self.report({'event':'alert_status','ring_finished':True,'ring_bytes':self.ring_bytes,'ring_xruns':self.audio.xruns()})
            self.audio.close();self.audio=None

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
        if self.audio:
            for _ in range(4):
                part=self.tone[self.position:self.position+640]
                n=self.audio.write(part)
                if n>0:self.position=(self.position+n)%len(self.tone);self.ring_bytes+=n
                elif n in (0,-11):break
                else:
                    self.ringing(False);self.report({'event':'alert_status','ring_audio':False,'audio_error':n});break
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

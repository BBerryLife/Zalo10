"""Phone-local system call UI + ringtone. Requires this rooted Q10's service.

The lock-screen popup exposes only call actions, never account/message data.
All PCM is an original generated ringtone, not recorded audio.
"""
import ctypes as C,os,time,socket
from q10media import Audio,AudioOpenError

WAKE_LOG='/accounts/1000/shared/documents/zalo10/wake_log.txt'
WAKE_SOCK='/var/tmp/zalo-wake.sock'   # Z28: root helper (wake_helper.py)

class ScreenWake:
    """Z27 (test): light the screen on an incoming call. The engine runs as root, which may open the QNX Screen
    power-manager context and switch the display power mode on. Every step is written to wake_log.txt
    (only numbers: no names, no call data). Documents/zalo10/wake.txt containing off = do nothing."""
    PROP_DISPLAY_COUNT=59;PROP_DISPLAYS=60;PROP_POWER_MODE=88;POWER_ON=0x7683
    def __init__(self):
        self.lib=None;self.error=''
        try:self.lib=C.CDLL('libscreen.so.1',use_errno=True)
        except OSError as e:self.error='load:'+str(e.errno)
    def log(self,text):
        try:
            if not os.path.isdir(os.path.dirname(WAKE_LOG)):os.makedirs(os.path.dirname(WAKE_LOG))
            with open(WAKE_LOG,'a') as f:f.write(time.strftime('%d/%m %H:%M:%S ')+text+'\n')
            os.chmod(WAKE_LOG,0o666)
        except (OSError,IOError):pass
    def enabled(self):
        try:
            with open('/accounts/1000/shared/documents/zalo10/wake.txt') as f:return f.read().strip()!='off'
        except (OSError,IOError):return True
    def wake(self,why):
        if not self.enabled():self.log(why+' wake=off');return
        out=[why,'euid=%d'%os.geteuid()]
        # Z28: a helper started with root rights (u_root) may be listening; ask it first
        if os.path.exists(WAKE_SOCK):
            try:
                c=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);c.settimeout(1.5)
                c.connect(WAKE_SOCK);c.sendall(b'wake\n');reply=c.recv(200).decode('ascii','replace').strip();c.close()
                out.append('helper='+reply[:120])
            except (OSError,IOError,socket.error) as e:
                out.append('helper_err=%s'%getattr(e,'errno',''))
        if os.geteuid()!=0:self.log(' '.join(out));return
        L=self.lib
        if not L:out.append('no libscreen '+self.error);self.log(' '.join(out));return
        for kind in (4,8):   # power manager, then display manager context
            ctx=C.c_void_p()
            r=L.screen_create_context(C.byref(ctx),kind)
            out.append('ctx%d=%d/%d'%(kind,r,C.get_errno()))
            if r:continue
            try:
                n=C.c_int(0)
                r=L.screen_get_context_property_iv(ctx,self.PROP_DISPLAY_COUNT,C.byref(n));out.append('n=%d/%d'%(r,n.value))
                if r or n.value<1 or n.value>8:continue
                arr=(C.c_void_p*n.value)()
                r=L.screen_get_context_property_pv(ctx,self.PROP_DISPLAYS,C.cast(arr,C.POINTER(C.c_void_p)));out.append('d=%d'%r)
                if r:continue
                disp=C.c_void_p(arr[0]);m=C.c_int(-1)
                L.screen_get_display_property_iv(disp,self.PROP_POWER_MODE,C.byref(m));out.append('before=%x'%m.value)
                on=C.c_int(self.POWER_ON)
                r=L.screen_set_display_property_iv(disp,self.PROP_POWER_MODE,C.byref(on));out.append('set=%d/%d'%(r,C.get_errno()))
                r=L.screen_flush_context(ctx,0);out.append('flush=%d'%r)
                m=C.c_int(-1);L.screen_get_display_property_iv(disp,self.PROP_POWER_MODE,C.byref(m));out.append('after=%x'%m.value)
                if m.value==self.POWER_ON:break
            finally:
                L.screen_destroy_context(ctx)
        self.log(' '.join(out))

class CallNotice:
    """Z28: a normal Zalo notification (Hub item) while a call rings. With Settings > Lock Screen >
    Turn On Screen for New Notifications, BB10 lights the screen for such notifications. Removed when the
    call stops ringing. Results go to wake_log.txt (numbers only)."""
    ITEM=b'zaloone-call'
    def __init__(self,lib,log):
        self.lib=lib;self.log=log;self.shown=False
        self.ok=all(hasattr(lib,n) for n in ('notification_message_create','notification_message_set_title',
            'notification_message_set_item_id','notification_notify','notification_delete','notification_message_destroy'))
        if self.ok:
            for n in ('notification_message_create','notification_message_destroy'):
                getattr(lib,n).argtypes=[C.POINTER(C.c_void_p)]
            for n in ('notification_message_set_title','notification_message_set_subtitle','notification_message_set_item_id',
                      'notification_message_set_invoke_target','notification_message_set_invoke_action'):
                if hasattr(lib,n):getattr(lib,n).argtypes=[C.c_void_p,C.c_char_p]
            for n in ('notification_notify','notification_delete'):
                getattr(lib,n).argtypes=[C.c_void_p,C.POINTER(C.c_int)]
    def _msg(self):
        m=C.c_void_p();r=self.lib.notification_message_create(C.byref(m))
        if r or not m.value:return None,r
        self.lib.notification_message_set_item_id(m,self.ITEM)
        return m,0
    def show(self,title,body):
        if not self.ok:self.log('notice missing_api');return
        m,r=self._msg()
        if m is None:self.log('notice create=%d'%r);return
        try:
            self.lib.notification_message_set_title(m,title.encode('utf-8'))
            if hasattr(self.lib,'notification_message_set_subtitle'):self.lib.notification_message_set_subtitle(m,body.encode('utf-8'))
            if hasattr(self.lib,'notification_message_set_invoke_target'):
                self.lib.notification_message_set_invoke_target(m,b'com.BerryLife.Zalo10.invoke')
            rid=C.c_int(0);r=self.lib.notification_notify(m,C.byref(rid))
            self.shown=(r==0);self.log('notice notify=%d id=%d'%(r,rid.value))
        finally:
            self.lib.notification_message_destroy(C.byref(m))
    def clear(self):
        if not self.shown:return
        self.shown=False
        m,r=self._msg()
        if m is None:return
        try:
            rid=C.c_int(0);r=self.lib.notification_delete(m,C.byref(rid));self.log('notice delete=%d'%r)
        finally:
            self.lib.notification_message_destroy(C.byref(m))

class CallAlert:
    def __init__(self,root,command,report):
        self.root,self.command,self.report=root,command,report
        self.lib=C.CDLL('libbps.so.3');self.lib.bps_initialize();self.lib.dialog_request_events(0)
        self.dialog=None;self.audio=None;self.position=0;self.state='idle';self.name='';self.video=False
        self.test_until=None;self.foreground=False;self.popup_kind=None
        self.reopen_at=None
        self.ring_bytes=0
        self.waker=ScreenWake()   # Z27
        self.notice=CallNotice(self.lib,self.waker.log)   # Z28
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
        # C1: Zola10's ringback tone while the other phone is ringing (from Zola10 1.0.0.29u)
        self.ringtone_bytes=self.tone;self.ring_back=False
        try:
            with open(os.path.join(root,'ringback.wav'),'rb') as f:self.ringback_bytes=f.read()[44:]
        except (IOError,OSError):self.ringback_bytes=b''
        if len(self.ringback_bytes)%2:self.ringback_bytes=self.ringback_bytes[:-1]

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
            title='Zalo — Cuộc gọi video' if self.video else 'Zalo — Cuộc gọi đến'
            body=self.name or 'Zalo';buttons=[('Từ chối','reject'),('Nhận','answer')]
        else:
            title='Zalo — Đang trong cuộc gọi';body=self.name or 'Zalo'
            buttons=[('Kết thúc','end'),('Mở Zalo','open')]
        self.lib.dialog_set_title_text(d,title.encode('utf-8'));self.lib.dialog_set_alert_message_text(d,body.encode('utf-8'))
        for label,context in buttons:self.lib.dialog_add_button(d,label.encode('utf-8'),True,context.encode(),True)
        show=self.lib.dialog_show(d)
        if show:
            self.close_popup()
            if self.state=='incoming':self.reopen_at=time.time()+1
        self.report({'event':'alert_status','create':created,'system':system,'priority':priority,'show':show,'test':kind=='test','ring_audio':bool(self.audio)})

    def ringing(self,on,back=False):
        if on and self.audio and back!=self.ring_back:self.ringing(False)   # C1: ringback and ringtone use different routes
        if on and not self.audio:
            try:
                self.ring_back=back
                # C1: ringback goes the way Zola10 29u played it (phone ringtone route, 16 kHz); the incoming ringtone keeps Z25
                self.audio=Audio(os.path.join(self.root,'libq10audio.so'),ringtone=True,ring_mode='off' if back else None);self.position=0;self.ring_bytes=0
            except AudioOpenError as error:
                self.report({'event':'alert_status','ring_audio':False,'audio_error':error.code,'audio_stage':error.stage})
        elif not on and self.audio:
            self.report({'event':'alert_status','ring_finished':True,'ring_bytes':self.ring_bytes,'ring_xruns':self.audio.xruns(),'ring_route':getattr(self.audio,'audio_type',''),'ring_rate':self.audio.rate()})   # Z25
            self.audio.close();self.audio=None

    def update(self,event,native_alert=False):
        previous=self.state;self.state=event.get('state','idle');self.video=bool(event.get('video'))
        if self.state!=previous or self.state=='idle':self.reopen_at=None
        if self.state=='incoming':self.name=event.get('name') or 'Zalo'
        if self.state=='incoming' and previous!='incoming':   # Z27/Z28
            if self.waker.enabled():
              try:self.notice.show('Cuộc gọi video Zalo' if self.video else 'Cuộc gọi Zalo đến',self.name or 'Zalo')
              except Exception as error:self.waker.log('notice error '+error.__class__.__name__)
            try:self.waker.wake('incoming')
            except Exception as error:self.waker.log('incoming error '+error.__class__.__name__)
        elif self.state!='incoming' and previous=='incoming':
            try:self.notice.clear()
            except Exception as error:self.waker.log('notice error '+error.__class__.__name__)
        if self.state=='idle':self.name=''
        self.test_until=None
        back=self.state in ('calling','ringing') and bool(self.ringback_bytes)   # C1
        self.tone=self.ringback_bytes if back else self.ringtone_bytes
        if self.position>=len(self.tone):self.position=0
        self.ringing((self.state=='incoming' and not native_alert) or back,back)
        if self.state=='incoming' and not native_alert:
            if self.foreground:self.close_popup()   # Z24: the Zalo call screen is showing
            else:self.popup('incoming')
        elif self.state=='connected' and not self.foreground:self.popup('connected')
        else:self.close_popup()

    def set_foreground(self,value):
        if self.foreground==bool(value):return
        self.foreground=bool(value)
        if self.state=='incoming':   # Z24: popup only while the Zalo screen is not in front
            if value:self.close_popup()
            else:self.reopen_at=None;self.popup('incoming')
            return
        if value:self.reopen_at=None
        if self.state=='connected':
            if value:self.close_popup()
            else:self.popup(self.state)

    def test(self):
        if self.state!='idle':return
        self.tone=self.ringtone_bytes;self.position=0
        self.ringing(True);self.test_until=time.time()+8;self.popup('test')

    def open_ui(self):
        # Navigator invocations need an application identity. The app's
        # native headless controller opens its GUI and retains the reply.
        self.reopen_at=time.time()+3;self.report({'event':'open_ui'})

    def tick(self):
        if self.reopen_at and time.time()>=self.reopen_at:
            self.reopen_at=None
            if self.state=='incoming' and not self.foreground:self.popup('incoming')
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

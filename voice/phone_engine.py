"""Runs entirely on rooted Q10: direct TLS API child + QNX audio + UDP media.

Normal UI integration uses JSON lines on stdin/stdout. Never log native API
responses: they contain media session material. This process emits only UI
events and packet counts. It never sends chat messages.
"""
import argparse
import base64
import errno
import json
import os
import select
import signal
import socket
import subprocess
import sys
import time
from callcore import CallCore
from relaymedia import RelayMedia
from q10media import AudioOpenError
from root_alerts import CallAlert

def record_health(socket_path,stage):
    if not socket_path:return
    path=os.path.join(os.path.dirname(socket_path),'zaloone-call-health')
    try:
        with open(path+'.tmp','w') as out:out.write(stage+'\n')
        os.rename(path+'.tmp',path)
    except OSError:pass


class PhoneEngine:
    def __init__(self,args):
        self.args=args;self.root=os.path.dirname(os.path.abspath(__file__))
        self.media=None;self.api=None;self.sequence=0;self.pending={};self.ready=False;self.muted=False
        self.api_buffer=b'';self.input_buffer=b'';self.running=True;self.test_started=None
        self.server=None;self.client=None;self.last_state={'event':'state','state':'idle'}
        self.out_buffer=b'';self.video_out_dropped=0
        self.pending_open_ui=False
        self.broker_alert_id=''
        self.speaker=False;self.spk_server=None
        if args.socket:
            # Z19: the call screen's loudspeaker button (QML) asks here: GET http://127.0.0.1:47819/spk?on=1|0
            try:
                self.spk_server=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
                self.spk_server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
                self.spk_server.bind(('127.0.0.1',47819));self.spk_server.listen(4);self.spk_server.setblocking(False)
            except (OSError,socket.error):
                try:self.spk_server.close()
                except Exception:pass
                self.spk_server=None
        if args.socket:
            args.socket=os.path.join(os.path.realpath(os.path.dirname(args.socket)),os.path.basename(args.socket))
            if not args.socket.startswith('/accounts/1000/appdata/com.BerryLife.Zalo10.') or not args.socket.endswith('/data/zaloone-engine.sock'):
                raise ValueError('Unexpected private engine socket')
            import fcntl
            self.worker_lock=open(args.socket+'.lock','a')
            fcntl.lockf(self.worker_lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
            if os.path.exists(args.socket): os.unlink(args.socket)
            self.server=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
            self.server.bind(args.socket);os.chmod(args.socket,0o660)
            owner=os.stat(os.path.dirname(args.socket))
            if os.geteuid()==0:os.chown(args.socket,owner.st_uid,owner.st_gid)
            self.server.listen(1);self.server.setblocking(False)
        self.core=CallCore(self.send,self.emit,self.prepare,self.start,self.stop)
        self.alert=None
        self.devnull=open(os.devnull,'wb')
        if not args.external_api:
            api_env=dict(os.environ)
            api_env['LD_LIBRARY_PATH']='/usr/lib/qt4/lib:'+api_env.get('LD_LIBRARY_PATH','/base/usr/lib:/base/lib')
            self.api=subprocess.Popen([os.path.join(self.root,'session_probe'),args.settings,'--serve'],
                stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.devnull,bufsize=0,env=api_env)
        if args.socket:
            try:self.alert=CallAlert(self.root,self.input,self.emit)
            except (OSError,AttributeError):
                self.emit({'event':'error','reason':'call_alert_unavailable'})

    def emit(self,event):
        if self.args.socket and event.get('event') in ('state','api_result','media_preparing','error','audio_open_error','call_control','local_choice','open_ui'):
            trace=dict((k,event[k]) for k in ('event','state','video','reason','command','ok','error_code','candidate_count','kind','stage','code') if k in event)
            trace['time']=int(time.time())
            try:
                path=os.path.join(os.path.dirname(self.args.socket),'zaloone-call-events')
                if os.path.exists(path) and os.stat(path).st_size>32768:os.rename(path,path+'.previous')
                with open(path,'a') as f:f.write(json.dumps(trace)+'\n')
            except (OSError,IOError):pass
        if event.get('event')=='state': self.last_state=dict(event)
        if event.get('event')=='state' and event.get('state')=='idle':
            self.muted=False
            self.speaker=False   # Z19: every call starts on the earpiece
            self.pending_open_ui=False
        if event.get('event')=='state' and self.alert:
            native_alert=event.get('state')=='incoming' and self.broker_alert_id and event.get('callId')==self.broker_alert_id
            self.alert.update(event,bool(native_alert))
        if event.get('event')=='state' and event.get('state')=='idle':
            if self.media:
                event=dict(event);event['media_counts']=dict(self.media.stats)
                event['media_counts']['video_out_dropped']=self.video_out_dropped
            self.video_out_dropped=0
            if self.args.test_peer_file and self.test_started:
                self.running=False
        if self.args.socket:
            if event.get('event')=='open_ui':
                self.pending_open_ui=True
            if self.client:
                # Buffered, non-blocking: the app's UI thread can be busy for a moment (opening the
                # camera takes ~0.7 s). A blocking send with a 0.1 s timeout dropped the app mid-call.
                # Video frames are skipped while the app is behind; everything else is always queued.
                video=event.get('event')=='remote_video'
                if video and len(self.out_buffer)>262144:
                    self.video_out_dropped+=1
                else:
                    self.out_buffer+=(json.dumps(event,sort_keys=True)+'\n').encode('utf-8')
                    if event.get('event')=='open_ui':self.pending_open_ui=False
                    self.flush_out()
        else:
            sys.stdout.write(json.dumps(event,sort_keys=True)+'\n');sys.stdout.flush()

    def drop_client(self):
        try:self.client.close()
        except (OSError,socket.error):pass
        self.client=None;self.input_buffer=b'';self.out_buffer=b''

    def flush_out(self):
        while self.client and self.out_buffer:
            try:
                sent=self.client.send(self.out_buffer[:65536])
            except socket.error as error:
                if getattr(error,'errno',None) in (errno.EAGAIN,errno.EWOULDBLOCK) or isinstance(error,socket.timeout):
                    break
                self.drop_client();return
            if sent<=0:break
            self.out_buffer=self.out_buffer[sent:]
        if self.client and len(self.out_buffer)>4194304:
            self.drop_client()          # the app stopped reading altogether; it reconnects

    def send(self,command,data):
        if not self.args.external_api and (not self.api or self.api.poll() is not None):
            raise RuntimeError('Native API process unavailable')
        self.sequence+=1
        call_id=self.core.call['id'] if self.core.call else None
        self.pending[self.sequence]=(command,call_id)
        frame={'id':self.sequence,'command':command,'data':data}
        if self.args.external_api:
            # Private anonymous pipe to the on-phone native broker. Never log
            # or forward this frame to QML/local-socket clients.
            self.emit({'event':'native_request','request':frame});return
        self.api.stdin.write((json.dumps(frame,separators=(',',':'))+'\n').encode('utf-8'))
        self.api.stdin.flush()

    def prepare(self,call,servers):
        self.emit({'event':'media_preparing','candidate_count':len(servers)})
        if self.media:
            self.media.stop()
        self.media=RelayMedia(self.core.media_ready,self.core.early_audio,
            lambda reason:self.core.finish(reason),lambda:self.core.finish('peer_ended',False),root=self.root,
            video_frame=lambda key,data:self.emit({'event':'remote_video','key':key,'data':base64.b64encode(data).decode('ascii')}))
        self.media.muted=self.muted
        self.media.speaker_z19=self.speaker
        self.media.prepare(call,servers)

    def start(self):
        if not self.media:
            raise RuntimeError('No media transport')
        self.media.start()

    def stop(self):
        if self.media:
            if self.media.call:
                try:
                    with open('/var/tmp/zaloone-last-media.json','w') as out:
                        json.dump(self.media.stats,out)
                except IOError:
                    pass
            self.media.stop()

    def api_event(self,response):
        if response.get('event')=='ready':
            self.ready=bool(response.get('voice_service'))
            record_health(self.args.socket,'voice_ready' if self.ready else 'voice_unavailable')
            self.emit({'event':'ready','voice_service':self.ready,'runs_on_q10':True})
            if self.args.lookup:
                self.send('lookup',{'phone':self.args.lookup})
            elif self.args.test_peer_file:
                if not self.ready:
                    self.running=False;return
                with open(self.args.test_peer_file) as source:
                    target=json.load(source)
                self.test_started=time.time()
                self.core.make_call(target['uid'])
            return
        request_id=response.get('id')
        try:
            request_id=int(request_id)
        except (TypeError,ValueError):
            self.emit({'event':'error','reason':'native_account_setup_failed'});self.running=False;return
        request=self.pending.pop(request_id,None)
        if not request:
            return
        command,call_id=request
        data=response.get('data')
        if command=='lookup':
            data=data if isinstance(data,dict) else {}
            uid=str(data.get('uid',''))
            success=response.get('ok') and uid.isdigit() and 1<=len(uid)<=24
            if success:
                target={'uid':uid,'name':data.get('display_name') or data.get('zalo_name') or ''}
                path=self.args.peer_output
                fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)
                try:
                    os.write(fd,json.dumps(target).encode('utf-8'))
                finally:
                    os.close(fd)
            self.emit({'event':'lookup_result','matched':bool(success),'error_code':response.get('error_code',0)})
            if self.args.lookup:
                self.running=False
            return
        if not self.core.call or self.core.call['id']!=call_id:
            return
        self.emit({'event':'api_result','command':command,'ok':bool(response.get('ok')),'error_code':response.get('error_code',0)})
        if command==401 and isinstance(data,dict):
            self.emit({'event':'call_setup_reply','fields':sorted(data.keys()),'server_count':len(data.get('servers',[])),
                       'status':data.get('status',0),'session_present':bool(data.get('sessId'))})
        self.core.api_reply(int(command),data if isinstance(data,dict) else {},bool(response.get('ok')),response.get('error_code',0))

    def input(self,frame):
        action=frame.get('action')
        if action=='api_response' and self.args.external_api:
            self.api_event(frame.get('response',{}))
        elif action=='call' and self.ready:
            if self.alert:self.alert.name=str(frame.get('name') or 'Zalo')
            self.core.make_call(str(frame.get('peer','')),bool(frame.get('video')))
        elif action=='video_frame' and self.media and self.media.active:
            value=frame.get('data','')
            if isinstance(value,str) and len(value)<=350000:
                self.media.send_video_frame(bool(frame.get('key')),base64.b64decode(value.encode('ascii')))
                if frame.get('key') and self.media.video_key_sent:self.core.camera_state(True)
        elif action=='camera_off':
            self.core.camera_state(False)
        elif action=='video_keyframe' and self.media:
            self.media.request_keyframe()
        elif action=='mute':
            self.muted=bool(frame.get('muted'))
            if self.media:self.media.muted=self.muted
        elif action=='test_alert' and self.alert and not self.core.call:
            self.alert.test()
        elif action=='ui_foreground' and self.alert:
            self.alert.set_foreground(frame.get('foreground'))
        elif action=='control':
            content=frame.get('content',{})
            if content.get('act')=='request':self.broker_alert_id=frame.get('brokerAlert','')
            if content.get('act') in ('request','cancel','end_call','reject','busy','answer','answer_ack','ring_ring'):
                self.emit({'event':'call_control','kind':content['act']})
            self.core.control(content)
        elif action=='restore_alert':
            self.broker_alert_id=''
            if self.alert and self.last_state.get('state')=='incoming':self.alert.update(self.last_state)
        elif action=='answer':
            self.emit({'event':'local_choice','kind':'answer'})
            self.core.answer()
        elif action=='hangup':
            self.emit({'event':'local_choice','kind':'hangup'})
            self.core.finish()
        elif action=='lookup' and self.ready:
            self.send('lookup',{'phone':str(frame.get('phone',''))})
        elif action=='quit':
            self.core.finish();self.running=False

    def speaker_request(self):
        # Z19: one tiny HTTP request from the app's call screen; only "/spk", answers the state in use ("1"/"0")
        try:
            conn,peer=self.spk_server.accept()
        except (OSError,socket.error):
            return
        try:
            conn.settimeout(0.3)
            if peer[0]!='127.0.0.1':return
            first=conn.recv(1024)
            head=first.decode('latin-1','replace').split('\r\n',1)[0].split(' ')
            path=head[1] if len(head)>1 else ''
            if path.startswith('/grp/') and head[0]=='POST':
                # Z30-Z32: group functions of the app (create, info, members, polls...); answered from a thread
                if self.group_request(conn,first):conn=None
                return
            if path.startswith('/spk'):
                if 'on=1' in path:want=True
                elif 'on=0' in path:want=False
                else:want=None
                if want is not None:
                    self.speaker=want
                    if self.media:self.speaker=self.media.set_speaker_z19(want)
                body='1' if self.speaker else '0'
                conn.sendall(('HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s'%(len(body),body)).encode('ascii'))
            else:
                conn.sendall(b'HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
        except (OSError,socket.error,ValueError):
            pass
        finally:
            if conn is not None:conn.close()

    def group_request(self,conn,first):
        # Z30: read the whole POST (JSON body: name, members), then create the group on Zalo in a thread
        data=first
        try:
            conn.settimeout(2)
            while b'\r\n\r\n' not in data and len(data)<65536:
                more=conn.recv(4096)
                if not more:break
                data+=more
            head,_,body=data.partition(b'\r\n\r\n')
            length=0
            for line in head.decode('latin-1','replace').split('\r\n')[1:]:
                if line.lower().startswith('content-length:'):length=int(line.split(':',1)[1].strip() or 0)
            if length<0 or length>65536:return False
            while len(body)<length:
                more=conn.recv(4096)
                if not more:break
                body+=more
            req=json.loads(body[:length].decode('utf-8'))
            if not isinstance(req,dict):return False
            action=head.decode('latin-1','replace').split(' ')[1][5:].split('?')[0]
        except (OSError,socket.error,ValueError,TypeError,AttributeError,IndexError):
            return False
        import threading
        if getattr(self,'group_lock',None) is None:self.group_lock=threading.Lock()
        if getattr(self,'group_waiting',0)>=6:
            self.group_answer(conn,{'ok':False,'error':'busy'});return True
        self.group_waiting=getattr(self,'group_waiting',0)+1
        def work():
            # Z32: one Zalo request at a time (the TLS library keeps one connection state); others wait their turn
            with self.group_lock:
                try:
                    import groupapi
                    ca=os.environ.get('Q10_CA_FILE') or os.path.join(self.root,'q10-ca.pem')
                    result=groupapi.call(action,req,self.args.settings,ca)
                except Exception as error:
                    result={'ok':False,'error':'internal','kind':error.__class__.__name__}
                self.group_waiting-=1
            self.group_answer(conn,result)
        worker=threading.Thread(target=work);worker.daemon=True;worker.start()
        return True

    def group_answer(self,conn,result):
        body=json.dumps(result).encode('utf-8')
        try:
            conn.settimeout(5)
            conn.sendall(('HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %d\r\nConnection: close\r\n\r\n'%len(body)).encode('ascii')+body)
        except (OSError,socket.error):
            pass
        finally:
            conn.close()

    def run(self):
        startup=time.time()
        last_client=startup
        try:
            while self.running:
                if self.client:last_client=time.time()
                elif self.args.socket and time.time()-last_client>90:break
                fds=[] if self.args.external_api else [self.api.stdout.fileno()]
                if self.server: fds.append(self.server.fileno())
                if self.spk_server: fds.append(self.spk_server.fileno())
                if self.client: fds.append(self.client.fileno())
                if self.media and self.media.sock:
                    fds.append(self.media.sock.fileno())
                if not self.args.socket and not self.args.lookup and not self.args.test_peer_file:
                    fds.append(sys.stdin.fileno())
                wfds=[self.client.fileno()] if self.client and self.out_buffer else []
                available,writable,_=select.select(fds,wfds,[],0.005)
                if self.client and writable:
                    self.flush_out()
                if self.spk_server and self.spk_server.fileno() in available:
                    self.speaker_request()
                if self.server and self.server.fileno() in available:
                    client,_=self.server.accept()
                    if self.client: self.drop_client()
                    self.client=client;self.client.setblocking(False);self.input_buffer=b'';self.out_buffer=b''
                    self.emit({'event':'ready','voice_service':self.ready,'runs_on_q10':True})
                    self.emit(self.last_state)
                    if self.pending_open_ui:self.emit({'event':'open_ui'})
                if not self.args.external_api and self.api.stdout.fileno() in available:
                    chunk=os.read(self.api.stdout.fileno(),65536)
                    if not chunk:
                        self.core.finish('native_api_exited',False);break
                    self.api_buffer+=chunk
                    if len(self.api_buffer)>262144:
                        raise RuntimeError('Native IPC frame exceeded bound')
                    while b'\n' in self.api_buffer:
                        line,self.api_buffer=self.api_buffer.split(b'\n',1)
                        self.api_event(json.loads(line.decode('utf-8')))
                if self.media and self.media.sock and self.media.sock.fileno() in available:
                    self.media.handle_udp()
                input_fd=self.client.fileno() if self.client else (None if self.args.socket else sys.stdin.fileno())
                if not self.args.lookup and not self.args.test_peer_file and input_fd in available:
                    try:
                        chunk=os.read(input_fd,65536)
                    except OSError as error:
                        if error.errno in (errno.EAGAIN,errno.EWOULDBLOCK):continue
                        chunk=b''
                    if not chunk:
                        if self.args.socket:
                            self.drop_client();continue
                        self.core.finish('ui_closed');break
                    self.input_buffer+=chunk
                    if len(self.input_buffer)>1048576:
                        raise RuntimeError('UI IPC frame exceeded bound')
                    while b'\n' in self.input_buffer:
                        line,self.input_buffer=self.input_buffer.split(b'\n',1)
                        frame=None
                        try:
                            frame=json.loads(line.decode('utf-8'))
                            self.input(frame)
                        except (ValueError,TypeError) as error:
                            # kind/where: exception class and the action (or control act); never data
                            where=''
                            if isinstance(frame,dict):
                                where=str(frame.get('action',''))[:24]
                                content=frame.get('content')
                                if isinstance(content,dict):where+='/'+str(content.get('act',''))[:24]
                            self.emit({'event':'error','reason':'invalid_ui_command','kind':error.__class__.__name__,'where':where})
                self.core.tick()
                if self.media:
                    self.media.tick()
                if self.alert:self.alert.tick()
                if not self.ready and time.time()-startup>30:
                    self.emit({'event':'error','reason':'account_startup_timeout'});break
                if self.test_started and time.time()-self.test_started>self.args.max_call_seconds:
                    self.core.finish('test_time_limit')
        except (KeyboardInterrupt,SystemExit):
            self.core.finish('interrupted')
        except Exception as error:
            # Exception objects/raw protocol frames are intentionally not logged.
            self.emit({'event':'error','reason':'runtime_failure','kind':error.__class__.__name__})
            if isinstance(error,AudioOpenError):
                self.emit({'event':'audio_open_error','code':error.code,'stage':error.stage})
            self.core.finish('runtime_failure')
        finally:
            if self.media:
                self.media.stop()
            if self.alert:self.alert.close()
            if self.api:
                try:
                    self.api.stdin.close()
                except (IOError,OSError):
                    pass
                # The API child drains any queued hangup before seeing EOF.
                deadline=time.time()+22
                while self.api.poll() is None and time.time()<deadline:
                    time.sleep(0.05)
                if self.api.poll() is None:
                    self.api.terminate()
            self.devnull.close()
            if self.client: self.client.close()
            if self.spk_server:self.spk_server.close()
            if self.server:
                self.server.close()
                if os.path.exists(self.args.socket): os.unlink(self.args.socket)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--settings',required=True)
    parser.add_argument('--external-api',action='store_true')
    parser.add_argument('--socket')
    parser.add_argument('--lookup')
    parser.add_argument('--peer-output',default='/var/tmp/q10-selected-peer.json')
    parser.add_argument('--test-peer-file')
    parser.add_argument('--max-call-seconds',type=int,default=60)
    args=parser.parse_args()
    if not 5<=args.max_call_seconds<=300:
        parser.error('Test duration must be between 5 and 300 seconds')
    record_health(args.socket,'starting')
    try:
        engine=PhoneEngine(args)
    except Exception as exc:
        record_health(args.socket,'startup_'+type(exc).__name__+'_'+str(getattr(exc,'errno','')))
        raise
    signal.signal(signal.SIGTERM,lambda signum,frame:engine.input({'action':'quit'}))
    engine.run()


if __name__=='__main__':
    main()

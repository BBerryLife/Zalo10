"""1:1 voice call state machine. No network, audio I/O, or account data here.

Wire semantics adapted from VN-Linux-Family/zalo-for-linux, BSD-3-Clause.
This is experimental; group/video calls are deliberately not advertised.
"""
import json
import os
import struct
import time
from q10media import AudioOpenError

CODEC='[{"dynamicFptime":0,"frmPtime":20,"name":"opus/16000/1","payload":112}]\n'


def obj(value):
    if isinstance(value,str):
        try:
            value=json.loads(value)
        except ValueError:
            return {}
    return value if isinstance(value,dict) else {}


def extend_data(server,video=False):
    return {"callType":int(video),"fecTP":0,"gccAudio":1,"gccEarlyCall":0,"gccMode":1,"gccSVLR":1,
        "maxFT":60,"newZrtc":1,"p2p":[],"packetMode":2,"platform":2,"sP2P":0,
        "serverAddr":[{"rtcp":server["rtcpaddr"],"rtcpIPv6":server.get("rtcpIPv6",""),
            "rtp":server["rtpaddr"],"rtpIPv6":server.get("rtpIPv6",""),"tpType":0}],
        "spTcp":0,"srtcp":0,"srtpMode":1,"supportCallBusy":1,"supportHevcDecode":0,"tpType":0,
        "video":{"codec":[{"name":"h264","payload":97}]}}


class CallCore:
    def __init__(self, send, event, prepare, start, stop, clock=time.time):
        self.send,self.event,self.prepare,self.start,self.stop=send,event,prepare,start,stop
        self.clock=clock
        self.call=None
        self.ended={}

    def status(self,state,reason=None):
        if self.call:
            self.call["state"]=state
        value={"event":"state","state":state}
        if self.call and state=='incoming':
            value['name']=self.call.get('name','')
            value['peer']=self.call.get('peer','')
            value['callId']=str(self.call['id'])
        if self.call:value['video']=bool(self.call.get('video'))
        if reason:
            value["reason"]=reason
        self.event(value)

    def make_call(self,peer,video=False):
        if self.call:
            raise ValueError("A call is already active")
        if not isinstance(peer,str) or not peer.isdigit() or not 1 <= len(peer) <= 24:
            raise ValueError("Invalid peer account identifier")
        call_id=1000000000+struct.unpack("<I",os.urandom(4))[0] % (0x7fffffff-1000000000)
        self.call={"id":call_id,"peer":peer,"role":"caller","state":"calling","deadline":self.clock()+25,'video':bool(video)}
        self.status("calling")
        self.send(401,{"callId":call_id,"calleeId":peer,"codec":"[]\n","type":3 if video else 1})

    def api_reply(self,command,data,ok=True,error=0):
        c=self.call
        if not c or command==12006:
            return
        if not ok:
            self.finish("api_error_%s" % error,signal=(command!=401))
            return
        if command==401 and c["state"]=="calling":
            if data.get("status") or not data.get("servers") or not data.get("sessId"):
                self.finish("call_setup_refused",False)
                return
            c["session"]=data["sessId"]
            c["local_uid"],c["peer_uid"]=int(data.get("fromId",0)),int(data.get("toId",0))
            if not 0 < c["local_uid"] <= 0xffffffff or not 0 < c["peer_uid"] <= 0xffffffff:
                self.finish("invalid_media_identifiers",True)
                return
            if len(c["session"].encode("utf-8")) < 30:
                self.finish("invalid_media_session",True)
                return
            self.status("negotiating")
            self.prepare(c,data["servers"])
        elif command==416 and c["state"]=="negotiating":
            self.status("calling")
            c["deadline"]=self.clock()+45
        elif command==402 and c["state"]=="answering":
            self.connect()

    def media_ready(self,server):
        c=self.call
        if not c or c["state"] not in ("negotiating","answering"):
            return
        c["server"]=server
        params=extend_data(server,c.get('video',False))
        if c["role"]=="callee" and c.get("video"):
            params.update({"numServers":0,"select2side":1,"supportCallBusy":0})
        data={"callId":c["id"],"codec":CODEC,"extendData":json.dumps(params,sort_keys=True,separators=(',',':'))+'\n',
            "rtcpAddress":server["rtcpaddr"],"rtpAddress":server["rtpaddr"],"session":c["session"]}
        if c["role"]=="caller":
            data["calleeId"]=c["peer"]
            self.send(416,data)
        else:
            data["callerId"]=c["peer"];data["status"]=0
            self.send(402,data)

    def control(self,content):
        before=self.call['state'] if self.call else None
        try:self._control(content)
        finally:
            try:self._trace(content,before)
            except Exception:pass

    def _trace(self,content,before):
        d=obj(content.get('data'));p=obj(d.get('params'));x=obj(p.get('extendData'))
        sess=d.get('session') or p.get('sessId') or ''
        info={'t':int(time.time()),'act':content.get('act'),'keys':sorted(content.keys())[:12],'dkeys':sorted(d.keys())[:30],
            'pkeys':sorted(p.keys())[:30],'callType':x.get('callType'),'sessLen':len(str(sess)),
            'uidFrom':str(d.get('uidFrom'))[:3]+'..' if d.get('uidFrom') else None,'uidTo':bool(d.get('uidTo')),
            'tsAgeS':(time.time()-float(d['ts'])/1000) if str(d.get('ts','')).replace('.','',1).isdigit() else None,
            'before':before,'after':self.call['state'] if self.call else None}
        path='/accounts/1000/shared/documents/zalo10/incoming_call.log'
        if os.path.exists(path) and os.stat(path).st_size>65536:os.rename(path,path+'.old')
        with open(path,'a') as f:f.write(json.dumps(info)+'\n')

    def _control(self,content):
        action=content.get("act")
        d=obj(content.get("data"))
        call_id=d.get("callId",d.get("id",content.get("callId",content.get("id"))))
        self.ended=dict((k,v) for k,v in self.ended.items() if self.clock()-v < 60)
        if d.get("ts"):
            try:
                if self.clock()*1000-float(d["ts"]) > 60000:
                    return
            except (ValueError,TypeError):
                return
        c=self.call
        if action=="request":
            if c or str(call_id) in self.ended:
                return
            p=obj(d.get("params"));extra=obj(p.get("extendData"))
            if extra.get("callType",0) not in (0,1):
                return
            session=d.get("session") or p.get("sessId")
            if not session or len(session.encode("utf-8"))<30:
                return
            try:
                local_uid,peer_uid=int(d.get("uidTo",0)),int(d.get("uidFrom",0))
                number=int(call_id)
            except (ValueError,TypeError):
                return
            if not 0<number<0x80000000 or not 0<local_uid<=0xffffffff or not 0<peer_uid<=0xffffffff:
                return
            servers=[{"rtpaddr":d.get("rtpAddress"),"rtcpaddr":d.get("rtcpAddress")}]
            alternatives=extra.get("serverAddr")
            if isinstance(alternatives,list):
                servers.extend(s for s in alternatives if isinstance(s,dict) and s.get("rtp") and s.get("rtcp"))
            self.call={"id":number,"peer":str(d.get("uidN","")),"role":"callee","state":"incoming",
                "session":session,"local_uid":local_uid,"peer_uid":peer_uid,"deadline":self.clock()+45,
                "servers":servers,"name":p.get("Dname",""),'video':extra.get('callType',0)==1}
            self.send(407,{"callId":number,"callerId":self.call["peer"]})
            self.status("incoming")
            return
        if not c or (call_id is not None and str(call_id)!=str(c["id"])):
            if action in ("cancel","end_call","reject") and call_id is not None:
                self.ended[str(call_id)]=self.clock()
            return
        if action=="ring_ring" and c["role"]=="caller" and c["state"] in ("calling","negotiating"):
            self.status("ringing");c["deadline"]=self.clock()+45
        elif action=="answer":
            if c["role"]=="callee":
                if c['state']=='incoming':
                    self.finish("answered_on_another_device",False)
                return
            p=obj(d.get("params"))
            # Video answers may carry status as "" or another non-number: that is not a decline.
            try:status=int(d.get("status",p.get("status",0)) or 0)
            except (ValueError,TypeError):status=0
            if status:
                self.finish("recipient_declined",False)
            elif c.get("server"):
                self.connect()
        elif action in ("cancel","end_call","reject","busy"):
            self.finish("peer_"+action,False)

    def answer(self):
        c=self.call
        if not c or c["state"]!="incoming":
            return
        self.status("answering")
        c["deadline"]=self.clock()+20
        self.prepare(c,c["servers"])

    def connect(self):
        c=self.call
        if not c or c["state"]=="connected" or not c.get("server"):
            return
        # Start audio first; report a device error rather than a silent connected call.
        try:
            self.start()
        except AudioOpenError:
            self.finish('microphone_or_speaker_unavailable')
            return
        c["started"]=self.clock()
        c["deadline"]=None
        if c["role"]=="caller":
            self.send(408,{"callId":c["id"],"calleeId":c["peer"]})
        self.status("connected")

    def early_audio(self,count):
        c=self.call
        if c and c["role"]=="caller" and c.get("server") and c["state"] in ("calling","ringing","negotiating") and count>=20:
            self.connect()

    def camera_state(self,enabled):
        c=self.call
        if not c or c['state']!='connected' or not c.get('video'):return
        if c.get('camera_enabled')==enabled:return
        c['camera_enabled']=enabled
        c['camera_repeat_at']=self.clock()+2 if enabled else None
        self.send(12006,{'callId':c['id'],'calleeId':c['peer'],'data':'','isOnCam':int(enabled),'isOnMic':-1})

    def tick(self):
        c=self.call
        if c and c.get('camera_repeat_at') and self.clock()>=c['camera_repeat_at']:
            c['camera_repeat_at']=None
            self.send(12006,{'callId':c['id'],'calleeId':c['peer'],'data':'','isOnCam':1,'isOnMic':-1})
        c=self.call
        if c and c.get("deadline") and self.clock()>c["deadline"]:
            self.finish("timeout")

    def finish(self,reason="local_hangup",signal=True):
        c=self.call
        if not c:
            return
        self.ended[str(c["id"])]=self.clock()
        if len(self.ended)>256:
            self.ended.pop(min(self.ended,key=self.ended.get))
        if signal:
            if c["state"]=="incoming":
                self.send(402,{"callId":c["id"],"callerId":c["peer"],"status":3,"codec":"[]\n","extendData":"{}\n",
                    "rtcpAddress":"","rtpAddress":"","session":c["session"]})
            else:
                command=409 if c["state"]=="connected" else 405
                self.send(command,{"callId":c["id"],"toId":c["peer"],"callType":int(bool(c.get('video')))})
        self.stop()
        self.call=None
        self.status("idle",reason)

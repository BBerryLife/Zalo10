"""Bounded 1:1 H264 packetization, adapted from zalo-for-linux (BSD-3-Clause)."""
import time
MAX_FRAME=256*1024
def packetize(data,chunk=1068):
    if not data or len(data)>MAX_FRAME:raise ValueError('Invalid H264 frame size')
    if len(data)<=chunk:return [data]
    result=[]
    for start in range(0,len(data),chunk):
        end=min(start+chunk,len(data))
        flag=128 if start==0 else 64 if end==len(data) else 0
        result.append(bytes(bytearray([28,flag]))+data[start:end])
    return result

class VideoAssembler:
    def __init__(self,emit,gap,clock=time.time):
        self.emit,self.gap,self.clock=emit,gap,clock
        self.pending={};self.ready={};self.last=-1;self.started=False
    def push(self,index,timestamp,pt,marker,payload):
        if pt not in (97,98) or not payload or index<=self.last:return
        now=self.clock()
        self.pending=dict((k,v) for k,v in self.pending.items() if now-v['since']<.7)
        if timestamp not in self.pending:
            if len(self.pending)>=12:
                self.pending.pop(min(self.pending,key=lambda k:self.pending[k]['since']));self.gap()
            self.pending[timestamp]={'parts':{},'end':None,'key':False,'size':0,'since':now}
        frame=self.pending[timestamp]
        if index in frame['parts']:return
        frame['parts'][index]=payload;frame['size']+=len(payload)
        frame['key']=frame['key'] or pt==98
        if frame['size']>MAX_FRAME:
            self.pending.pop(timestamp);self.gap();return
        if marker:frame['end']=index
        if frame['end'] is not None:
            indices=sorted(frame['parts']);first=frame['parts'][indices[0]]
            fragment=len(first)>=2 and first[0]==28 and first[1]&63==0
            if (not fragment or first[1]==128) and indices[-1]==frame['end'] and indices[-1]-indices[0]+1==len(indices):
                parts=[]
                for i in indices:
                    p=frame['parts'][i]
                    parts.append(p[2:] if len(p)>=2 and p[0]==28 and p[1]&63==0 else p)
                self.ready[indices[0]]=(frame['end'],frame['key'],b''.join(parts),now)
                self.pending.pop(timestamp)
        self.flush()
    def flush(self):
        while self.ready:
            first=min(self.ready);last,key,data,since=self.ready[first]
            if self.last>=0 and first!=self.last+1 and len(self.ready)<=3 and self.clock()-since<.2:return
            self.ready.pop(first)
            if not self.started and not key:continue
            if self.last>=0 and first!=self.last+1:self.gap()
            self.last=last;self.started=True;self.emit(key,data)
            self.pending=dict((k,v) for k,v in self.pending.items() if max(v['parts'])>self.last)

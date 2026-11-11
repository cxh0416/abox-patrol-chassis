import json,secrets,time
from pathlib import Path
import paho.mqtt.client as mqtt
root=Path(__file__).parent;topic=(root/'topic.txt').read_text().strip();events=[]
c=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='patrol-uncertain-check-'+secrets.token_hex(8))
c.on_connect=lambda client,u,f,r,p:client.subscribe(topic+'/out',qos=1) if r==0 else None
c.on_message=lambda c,u,m:events.append((time.monotonic(),json.loads(m.payload)))
c.connect('test.mosquitto.org',1883,30);c.loop_start()
def wait(fn,timeout=70,after=0):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        for e in reversed(events):
            if e[0]>after and fn(e[1]):return e
        time.sleep(.05)
    raise TimeoutError(str(events[-8:]))
try:
    _,stats=wait(lambda x:x.get('event')=='stats' and x.get('ready')==1)
    assert stats['actions']==0
    t=time.monotonic();c.publish(topic+'/in',(root/'uncertain-request.json').read_text(),qos=1).wait_for_publish(5)
    _,result=wait(lambda x:x.get('data',{}).get('requestId')=='board-uncertain',30,t)
    assert result['data']['code']==4003
    _,stats=wait(lambda x:x.get('event')=='stats',20,t)
    assert stats['actions']==0
    print('PASS product adapter reset between simulated actuation and final write: retry unavailable; no second simulated action')
    c.publish(topic+'/in','finish',qos=1).wait_for_publish(5)
finally:
    (root/'uncertain-observer.jsonl').write_text(''.join(json.dumps({'monotonic':t,'payload':x},ensure_ascii=False)+'\n' for t,x in events),encoding='utf-8',newline='\n')
    c.disconnect();c.loop_stop()

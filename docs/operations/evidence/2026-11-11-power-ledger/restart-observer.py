import json,secrets,time
from pathlib import Path
import paho.mqtt.client as mqtt
root=Path(__file__).parent;topic=(root/'topic.txt').read_text().strip();events=[]
c=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='patrol-replay-check-'+secrets.token_hex(8))
c.on_connect=lambda client,u,f,r,p:client.subscribe(topic+'/out',qos=1) if r==0 else None
c.on_message=lambda c,u,m:events.append((time.monotonic(),json.loads(m.payload),m.payload.decode()))
c.connect('test.mosquitto.org',1883,30);c.loop_start()
def wait(fn,timeout=70):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        for e in reversed(events):
            if fn(e[1]):return e
        time.sleep(.05)
    raise TimeoutError(str(events[-8:]))
def send(s): c.publish(topic+'/in',s,qos=1).wait_for_publish(5)
def req(id,expiry):return json.dumps({'version':'4.0','timestamp':expiry-5000,'data':{'requestId':id,'command':'power_on','params':None,'expiresAt':expiry}},separators=(',',':'))
try:
    at,stats,_=wait(lambda x:x.get('event')=='stats' and x.get('ready')==1)
    old=[json.loads(x)['payload'] for x in (root/'observer.jsonl').read_text().splitlines() if '"requestId": "board-first"' in x and '"code": 200' in x][-1]
    send((root/'first-request.json').read_text())
    replay=wait(lambda x:x.get('data',{}).get('requestId')=='board-first',30)
    assert replay[2]==json.dumps(old,separators=(',',':'),ensure_ascii=False)
    print('PASS product adapter after MCU restart and modem power-cycle: first response byte-identical, simulated actions remain zero')
    send('arm-after');wait(lambda x:x.get('event')=='armed-after',15)
    at,stats,_=wait(lambda x:x.get('event')=='stats' and x.get('ready')==1,15)
    expiry=1800000700000+stats['tick']+int((time.monotonic()-at)*1000)+4800
    (root/'uncertain-request.json').write_text(req('board-uncertain',expiry),encoding='utf-8',newline='\n')
    send(req('board-uncertain',expiry));wait(lambda x:x.get('event')=='begin' and x.get('id')=='board-uncertain',20)
    print('Armed reset during simulated action; waiting for RTT confirmation')
finally:
    (root/'restart-observer.jsonl').write_text(''.join(json.dumps({'monotonic':t,'payload':x},ensure_ascii=False)+'\n' for t,x,_ in events),encoding='utf-8',newline='\n')
    c.disconnect();c.loop_stop()

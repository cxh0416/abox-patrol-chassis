import json,secrets,time,threading
from pathlib import Path
import paho.mqtt.client as mqtt
root=Path(__file__).parent;topic=(root/'topic.txt').read_text().strip();events=[]
c=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='patrol-ledger-check-'+secrets.token_hex(8))
c.on_connect=lambda client,u,f,r,p:client.subscribe(topic+'/out',qos=1) if r==0 else None
def observe(c,u,m):
    try: events.append((time.monotonic(),json.loads(m.payload),m.payload.decode()))
    except (ValueError,UnicodeDecodeError): pass
c.on_message=observe;c.connect('test.mosquitto.org',1883,30);c.loop_start()
def wait(pred,timeout=60,start=0):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        match=[x for x in events[start:] if pred(x[1])]
        if match:return match[-1]
        time.sleep(.05)
    raise TimeoutError(str(events[-12:]))
def send(v,qos=1):
    p=c.publish(topic+'/in',v,qos=qos)
    if qos:p.wait_for_publish(5)
def request(id,expiry,cmd='power_on'):
    return json.dumps({'version':'4.0','timestamp':expiry-5000,'data':{'requestId':id,'command':cmd,'params':None,'expiresAt':expiry}},separators=(',',':'))
try:
    at,st,_=wait(lambda e:e.get('event')=='stats' and e.get('ready')==1,90)
    expire=1800000700000+st['tick']+int((time.monotonic()-at)*1000)+4900
    active=threading.Event();active.set()
    def flood():
        while active.is_set():send('ping',0);time.sleep(.06)
    worker=threading.Thread(target=flood);worker.start()
    req=request('board-first',expire)
    (root/'first-request.json').write_text(req,encoding='utf-8',newline='\n')
    send(req);send(req);send(request('board-first',expire+1))
    response=wait(lambda e:e.get('data',{}).get('requestId')=='board-first' and e.get('data',{}).get('code')==200,40)
    active.clear();worker.join();send(req)
    end=time.monotonic()+25
    while time.monotonic()<end:
        matches=[x for x in events if x[1].get('data',{}).get('requestId')=='board-first' and x[1].get('data',{}).get('code')==200]
        if len(matches)>=2:break
        time.sleep(.05)
    assert len(matches)>=2 and matches[-1][2]==response[2]
    assert any(x[1].get('data',{}).get('requestId')=='board-first' and x[1].get('data',{}).get('code')==4006 for x in events)
    stats=wait(lambda e:e.get('event')=='stats' and e.get('actions')==1 and e.get('probes',0)>0,30)[1]
    assert stats.get('drops')==0
    print('PASS product adapter and FSM with real MQTT: accepted, conflict, byte-identical replay and concurrent traffic')
    print('stats:',json.dumps(stats))
    at,st,_=wait(lambda e:e.get('event')=='stats' and e.get('ready')==1,15)
    expire=1800000700000+st['tick']+int((time.monotonic()-at)*1000)+4800
    start=len(events);send(request('board-stop',expire));send(request('board-off',expire,'power_off'))
    wait(lambda e:e.get('event')=='safe_stop',30,start)
    stopped=wait(lambda e:e.get('data',{}).get('requestId')=='board-stop',30,start)[1]
    assert stopped['data']['code']==4003 and 'superseded' in stopped['data']['msg']
    st=wait(lambda e:e.get('event')=='stats' and e.get('stops',0)>=1,20,start)[1]
    assert st['actions']==1
    print('PASS safe stop preempts pending simulated power-on:',json.dumps(stopped))
    send('finish')
finally:
    (root/'observer.jsonl').write_text(''.join(json.dumps({'monotonic':t,'payload':x},ensure_ascii=False)+'\n' for t,x,_ in events),encoding='utf-8',newline='\n')
    c.disconnect();c.loop_stop()

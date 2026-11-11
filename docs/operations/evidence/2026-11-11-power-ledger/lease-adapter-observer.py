import json,secrets,time
from pathlib import Path
import paho.mqtt.client as mqtt
root=Path(__file__).parent;e=Path('D:/work/A-Box/Patrol_Vehicle_Chassis/docs/operations/evidence/2026-11-11-power-ledger');topic=(root/'topic.txt').read_text().strip();events=[]
c=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='patrol-lease-check-'+secrets.token_hex(8))
c.on_connect=lambda client,u,f,r,p:client.subscribe(topic+'/out',qos=1) if r==0 else None
c.on_message=lambda c,u,m:events.append((time.monotonic(),json.loads(m.payload),m.payload.decode()))
c.connect('test.mosquitto.org',1883,30);c.loop_start()
def wait(fn,timeout=70,after=0):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  for item in reversed(events):
   if item[0]>after and fn(item[1]):return item
  time.sleep(.05)
 raise TimeoutError(str(events[-12:]))
def send(s):c.publish(topic+'/in',s,qos=1).wait_for_publish(5)
try:
 wait(lambda x:x.get('event')=='stats' and x.get('ready')==1)
 first=(e/'first-request.json').read_text();old=[json.loads(x)['payload'] for x in (e/'observer.jsonl').read_text().splitlines() if '"requestId": "board-first"' in x and '"code": 200' in x][-1]
 t=time.monotonic();send(first);r=wait(lambda x:x.get('data',{}).get('requestId')=='board-first' and x['data'].get('code')==200,30,t)
 assert r[2]==json.dumps(old,separators=(',',':'),ensure_ascii=False)
 request=json.loads(first);request['data']['command']='power_off'
 t=time.monotonic();send(json.dumps(request,separators=(',',':')))
 wait(lambda x:x.get('data',{}).get('requestId')=='board-first' and x['data'].get('code')==4006,30,t)
 _,st,_=wait(lambda x:x.get('event')=='stats' and x.get('stops',0)>=1,20,t);assert st['actions']==0
 print('PASS latest adapter with OTA/enrollment lease checks: restored response and safety-stop conflict, no new action')
 print('stats:',json.dumps(st));send('finish')
finally:
 (root/'observer.jsonl').write_text(''.join(json.dumps({'monotonic':t,'payload':x},ensure_ascii=False)+'\n' for t,x,_ in events),encoding='utf-8',newline='\n')
 c.disconnect();c.loop_stop()

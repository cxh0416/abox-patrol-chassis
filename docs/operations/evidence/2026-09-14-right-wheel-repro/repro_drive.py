import os,sys,uuid,time,sqlite3,json
os.chdir('/opt/device-ota-service/current');sys.path.insert(0,'src')
from device_ota import db
c=sqlite3.connect('file:/var/lib/device-ota-service/ota.sqlite3?mode=ro',uri=True);c.row_factory=sqlite3.Row
def op(cap,p={}):
 ident=db.create_operation('patrol_vehicle_chassis','PATROL-CHASSIS-000',cap,p,'codex-maintenance',str(uuid.uuid4()))
 for _ in range(100):
  r=dict(c.execute('select state,result_json,error from operations where id=?',(ident,)).fetchone())
  if r['state'] in ['succeeded','failed','expired']:
   d=json.loads(r['result_json']);print(cap,ident,r['state'],json.dumps(d),flush=True)
   if r['state']!='succeeded':raise RuntimeError(r['error'])
   return d
  time.sleep(.25)
 raise RuntimeError('operation timeout '+ident)

os.environ['PATROL_CHASSIS_CONTROL_DB_PATH']='/var/lib/device-ota-service/chassis-control.sqlite3'
r=op('get_info');assert r['fwVersion']=='patrol-chassis-0.2.28' and not r['otaBusy']
sid=None
try:
 op('power_on');time.sleep(4)
 r=op('get_bench_steering');assert r['coordinateValid'] and r['profileReady'] and r['rangeValid'] and r['driveFault']==0
 r=op('arm_bench_drive',{'limitMdeg':1000});assert r['drivePermitted']
 sid=r['sessionId'];token=str(uuid.uuid4())
 db.create_chassis_control_session('codex-maintenance',token,'PATROL-CHASSIS-000',device_session_id=sid,steering_mdeg=0)
 start=time.monotonic();seq=0
 while time.monotonic()-start<12:
  elapsed=time.monotonic()-start;rpm=10 if 5<=elapsed<8 else 0;seq+=1
  r=db.update_chassis_control_target(sid,token,seq,rpm,0)
  print('control',round(elapsed,2),rpm,json.dumps({k:r.get(k) for k in ['state','device_accepted','feedback','end_reason']} if r else {}),flush=True)
  if not r or r['state']!='active':raise RuntimeError('control ended')
  time.sleep(.2)
finally:
 if sid:db.end_chassis_control_session(sid,'explicit_stop',token)
 op('power_off')
 op('get_bench_steering');op('get_can_diag')

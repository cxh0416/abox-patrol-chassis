"""Read current mechanical-center data. Upload-only, no motor writes."""
import datetime
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, 'E:/1_work/ChassisCar_test')
from steering_sdo_timing_compare import UploadOnlyCanalyst, verify_stopped
from can1_bus_monitor import DEFAULT_DLL

root=Path.cwd()
output=root/'out/flash'/datetime.datetime.now().strftime('%Y-%m-%d-car2-center-read-%H%M%S-%f')
output.mkdir(parents=True)
can=UploadOnlyCanalyst(channel=1,dll_path=str(DEFAULT_DLL))
start=time.perf_counter();results={}
with (output/'frames.jsonl').open('x',encoding='utf-8') as log:
    def record(direction,cob,payload):
        log.write(json.dumps({'at_s':time.perf_counter()-start,'direction':direction,
                              'id':cob,'data':payload.hex(' ')})+'\n');log.flush()
    try:
        can.open()
        # Reject competing masters before an independent uploader is used.
        until=time.perf_counter()+.25
        while time.perf_counter()<until:
            frame=can.receive(0)
            if frame:
                record('RX',*frame)
                if frame[0] in (0,0x601,0x602):raise RuntimeError('Another CAN master active')
            else:time.sleep(.001)
        results['stationary_before']=verify_stopped(can,record)
        for row in results['stationary_before']:
            if row['index']=='0x6041' and row['value']&8:
                raise RuntimeError('Driver Fault status; refuse calibration')
        positions=[]
        for _ in range(3):
            request=bytes.fromhex('40 64 60 00 00 00 00 00')
            if not can.transmit(0x601,request):raise RuntimeError('Position read send failed')
            record('TX_READ_ONLY_POSITION',0x601,request)
            deadline=time.perf_counter()+.3
            value=None
            while time.perf_counter()<deadline:
                f=can.receive(0)
                if not f:time.sleep(.001);continue
                record('RX',*f);cob,data=f
                if cob in (0,0x601,0x602):raise RuntimeError('Another CAN master active')
                if cob==0x581 and len(data)>=8 and data[:4]==bytes.fromhex('43 64 60 00'):
                    value=int.from_bytes(data[4:8],'little',signed=True);break
                if cob==0x581 and len(data)>=4 and data[:4]==bytes.fromhex('80 64 60 00'):
                    raise RuntimeError('Position read aborted')
            if value is None:raise RuntimeError('Position read timed out')
            positions.append(value);time.sleep(.05)
        if max(positions)-min(positions)>5:raise RuntimeError('Steering not stationary')
        results['driver_position_samples']=positions
        results['stationary_after']=verify_stopped(can,record)
        results['operator_confirmed_mechanical_center']=True
        results['read_only']=True
    except Exception as exc:
        results['error']=str(exc)
    finally:can.close()
(output/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
(root/'out/logs/car2-center-read-latest.json').write_text(json.dumps({'directory':str(output),'error':results.get('error')},ensure_ascii=False),encoding='utf-8')
print(json.dumps(results,ensure_ascii=False,indent=2));print('OUTPUT',output)
if 'error' in results:raise SystemExit(1)

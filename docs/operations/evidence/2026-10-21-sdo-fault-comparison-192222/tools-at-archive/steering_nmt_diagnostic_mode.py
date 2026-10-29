"""Prepared diagnostic state switch: steering shutdown, then Node 1 NMT only; never enable/reset."""
import argparse
import json
import time
from pathlib import Path
from datetime import datetime
from steering_center_once import Observer,Recovery
from chassis_offline_remote import Canalyst
from can1_bus_monitor import DEFAULT_DLL


# 仅在主控无会话、输出关闭且双轮已停时切换 Node 1 NMT；全程不恢复电机使能。
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state',choices=('preoperational','operational'),required=True)
    args=parser.parse_args()
    dest=Path(__file__).parent/'logs'/datetime.now().strftime('steering-nmt-diagnostic-%Y%m%d-%H%M%S-%f');dest.mkdir()
    can=Canalyst(channel=1,dll_path=str(DEFAULT_DLL));obs=None;opened=False;report=dict(requested=args.state)
    with (dest/'frames.jsonl').open('x',encoding='utf-8') as log:
        rec=Recovery(can,log)
        try:
            obs=Observer();report['mcu']=obs.sample();obs.close();obs=None
            can.open();opened=True
            until=time.perf_counter()+.2
            while time.perf_counter()<until:
                f=can.receive(0)
                if f:
                    rec.record('RX',id=f[0],data=f[1].hex(' '))
                    if f[0] in (0x601,0x602):raise RuntimeError('another CAN master active')
                    if f[0]==0x701 and len(f[1])==1:report['initial_heartbeat']=f[1][0]
            for _ in range(3):rec.wheels();time.sleep(.05)
            report['initial_status']=hex(rec.sdo(1,0x6041))
            if rec.sdo(1,0x603f):raise RuntimeError('steering driver fault; no NMT trial')
            rec.sdo(1,0x6040,6,width=2)
            until=time.perf_counter()+1
            while True:
                sw=rec.sdo(1,0x6041)
                if not sw&4:break
                if time.perf_counter()>=until:raise RuntimeError('steering disable not confirmed')
                time.sleep(.02)
            report['disabled_status']=hex(sw)
            payload=bytes([0x80 if args.state=='preoperational' else 1,1])
            assert payload in (b'\x80\x01',b'\x01\x01')
            if not can.transmit(0,payload):raise RuntimeError('NMT transmit failed')
            rec.record('TX_NMT_NODE1_ONLY',id=0,data=payload.hex(' '))
            expected=127 if args.state=='preoperational' else 5
            until=time.perf_counter()+1
            while time.perf_counter()<until:
                f=can.receive(0)
                if not f:time.sleep(.001);continue
                rec.record('RX',id=f[0],data=f[1].hex(' '))
                if f[0]==0x701 and f[1]==bytes([expected]):report['confirmed_heartbeat']=expected;break
            else:raise RuntimeError('NMT state not confirmed')
            report['final_status']=hex(rec.sdo(1,0x6041));report['wheels']=rec.wheels()
            if int(report['final_status'],16)&4:raise RuntimeError('unexpected steering operation enabled')
            report['result']='state confirmed; steering and drive remain disabled'
        except Exception as exc:report['error']=str(exc)
        finally:
            if opened:can.close()
            if obs:obs.close()
    (dest/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2),flush=True);print('LOG',dest,flush=True)
    return 1 if 'error' in report else 0


if __name__=='__main__':raise SystemExit(main())

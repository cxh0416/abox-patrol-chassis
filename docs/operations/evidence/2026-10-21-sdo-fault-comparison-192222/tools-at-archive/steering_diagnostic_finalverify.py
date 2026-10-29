"""Read-only final verification after the authorized PDO comparison; never enable/write/NMT."""
import json
import time
from datetime import datetime
from pathlib import Path
from chassis_offline_remote import Canalyst
from can1_bus_monitor import DEFAULT_DLL
from steering_center_once import Observer, Recovery


# 只读核对主控故障保留、Node 1 心跳恢复和双类执行器撤使能；不改变转角。
def main():
    dest = Path(__file__).parent/'logs'/datetime.now().strftime('steering-diagnostic-finalverify-%Y%m%d-%H%M%S-%f')
    dest.mkdir()
    observer = None
    opened = False
    can = Canalyst(channel=1, dll_path=str(DEFAULT_DLL))
    report = dict(read_only=True, physical_channel=2)
    with (dest/'frames.jsonl').open('x', encoding='utf-8') as log:
        recovery = Recovery(can, log)
        try:
            observer = Observer()
            report['mcu'] = observer.sample()
            observer.close()
            observer = None
            can.open()
            opened = True
            deadline = time.perf_counter()+.5
            while time.perf_counter() < deadline:
                frame = can.receive(0)
                if frame:
                    cob, payload = frame
                    recovery.record('RX',id=cob,data=payload.hex(' '))
                    if cob in (0x601,0x602):
                        raise RuntimeError('another CAN master active')
                    if cob == 0x701 and len(payload)==1:
                        report['heartbeat'] = payload[0]
            if report.get('heartbeat')!=5:
                raise RuntimeError('Node 1 operational heartbeat not confirmed')
            report['wheels'] = [recovery.wheels() for _ in range(3)]
            report['steering_status'] = hex(recovery.sdo(1,0x6041))
            report['steering_fault'] = recovery.sdo(1,0x603f)
            report['steering_actual'] = recovery.sdo(1,0x6064,signed=True)
            if int(report['steering_status'],16)&4:
                raise RuntimeError('steering operation enabled unexpectedly')
            report['result'] = 'Node 1 operational restored; both motors disabled; wheels zero; MCU owner/output off'
        except Exception as exc:
            report['error'] = str(exc)
        finally:
            if opened:can.close()
            if observer:observer.close()
    (dest/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2),flush=True)
    print('LOG',dest,flush=True)
    return 1 if 'error' in report else 0


if __name__=='__main__':raise SystemExit(main())

"""Stationary read-only SDO timing sweep around steering heartbeat; no writes or enable."""
import heapq
import argparse
import hashlib
import json
import statistics
import time
from datetime import datetime
from pathlib import Path
from chassis_offline_remote import Canalyst
from can1_bus_monitor import DEFAULT_DLL
from steering_center_once import Observer


# 仅发送 Node 1 upload，改变读取时刻以核对 PDO/心跳并发附近的应答。
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--heartbeat-state',type=int,choices=(5,127),default=5,help='expected Node 1 NMT state; this script never changes it')
    parser.add_argument('--count',type=int,default=400,help='planned reads, 1..5000; one per heartbeat')
    args=parser.parse_args()
    if not 1<=args.count<=5000:parser.error('--count must be 1..5000')
    dest=Path(__file__).parent/'logs'/datetime.now().strftime('steering-phase-readonly-%Y%m%d-%H%M%S-%f');dest.mkdir()
    can=Canalyst(channel=1,dll_path=str(DEFAULT_DLL));observer=None;opened=False
    start=time.perf_counter();last_hb=None;periods=[];schedule=[];pending=None;results=[];planned=0;sent=0;error=None
    with (dest/'frames.jsonl').open('x',encoding='utf-8') as log:
        def record(direction,cob,payload):
            log.write(json.dumps(dict(elapsed_s=time.perf_counter()-start,direction=direction,id=cob,data=payload.hex(' ')))+'\n')
        try:
            observer=Observer();print('MCU_CHECK',json.dumps(observer.sample()),flush=True)
            observer.close();observer=None
            can.open();opened=True
            while time.perf_counter()-start<max(65,args.count*.12+10) and (planned<args.count or pending or schedule):
                now=time.perf_counter();f=can.receive(0)
                if f:
                    cob,p=f;record('RX',cob,p)
                    if cob in (0x601,0x602):raise RuntimeError('another CAN master active')
                    if cob==0x701 and p==bytes([args.heartbeat_state]):
                        if last_hb is not None and .08<now-last_hb<.12:periods.append(now-last_hb)
                        last_hb=now
                        if len(periods)>=10 and planned<args.count:
                            # 主机收到心跳与实际总线发送存在 USB 延迟，扫描整个 ±5ms 窗口。
                            offset=((planned%41)-20)*.00025
                            heapq.heappush(schedule,(now+statistics.median(periods[-30:])+offset,planned,offset))
                            planned+=1
                    if pending and cob==0x581 and len(p)>=4 and int.from_bytes(p[1:3],'little')==pending['index'] and p[3]==0:
                        n={0x43:4,0x47:3,0x4b:2,0x4f:1}.get(p[0])
                        if n and len(p)>=4+n:
                            pending.update(status='ok',value=int.from_bytes(p[4:4+n],'little'),host_roundtrip_ms=(now-pending['at'])*1000)
                        elif p[0]==0x80 and len(p)>=8:pending.update(status='abort',abort=hex(int.from_bytes(p[4:8],'little')))
                        if 'status' in pending:
                            results.append(pending);pending=None
                            if len(results)%400==0:print('PROGRESS',json.dumps(dict(completed=len(results),sent=sent,timeouts=sum(r.get('status')=='timeout' for r in results))),flush=True)
                if pending and now-pending['at']>=.3:
                    pending.update(status='timeout');results.append(pending);print('MISSING',json.dumps(pending),flush=True);pending=None
                while schedule and schedule[0][0]<=now:
                    due,ordinal,offset=heapq.heappop(schedule)
                    if pending:continue
                    index=(0x6041,0x6064,0x6081)[ordinal%3]
                    payload=bytes([0x40,index&255,index>>8,0,0,0,0,0]);assert payload[0]==0x40
                    at=time.perf_counter()
                    if not can.transmit(0x601,payload):raise RuntimeError('SDO upload transmit failed')
                    record('TX_READ_ONLY',0x601,payload);sent+=1
                    pending=dict(index=index,at=at,elapsed_s=at-start,offset_s=offset,host_lateness_ms=(at-due)*1000)
                if not f and (not schedule or schedule[0][0]-now>.003):time.sleep(.0005)
        except Exception as exc:
            error=str(exc);print('ERROR',error,flush=True)
        finally:
            if observer:observer.close()
            if opened:can.close()
    report=dict(product='patrol_vehicle_chassis',read_only=True,physical_channel=2,expected_heartbeat_state=args.heartbeat_state,planned=args.count,script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),sent=sent,results=results,error=error,
        purpose='Sweep upload request host timing around periodic heartbeat; host timing does not prove exact wire phase. No parameter writes, NMT, trigger or enable.')
    (dest/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('SUMMARY',json.dumps(dict(sent=sent,statuses={k:sum(r.get('status')==k for r in results) for k in ('ok','timeout','abort')},error=error)),flush=True)
    print('LOG',dest,flush=True)


if __name__=='__main__':main()

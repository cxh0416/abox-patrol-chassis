"""Stationary read-only SDO timing sweep around steering heartbeat; no writes or enable."""
import heapq
import ctypes
import subprocess
import sys
import argparse
import hashlib
import json
import statistics
import time
from datetime import datetime
from pathlib import Path
from chassis_offline_remote import Canalyst, VciCanObj
from can1_bus_monitor import DEFAULT_DLL
from can_adapter_status_readonly import query, Status, Errors
REPO=Path(r'E:\1_work\ChassisCar\abox-patrol-vehicle-chassis-firmware')


# 读取精确匹配 0.2.76 的主控镜像和安全字段，不暂停、复位、写内存或清锁。
class Observer:
    def __init__(self):
        done=subprocess.run([sys.executable,str(REPO/'out/logs/fault-stop-0276-readonly.py'),str(REPO/'out/logs/fault-stop-0276-t12-schema.json')],cwd=REPO,capture_output=True,text=True,encoding='utf-8')
        if done.returncode:raise RuntimeError('MCU read-only precheck failed: '+done.stdout+done.stderr)
        pointer=json.loads((REPO/'out/logs/fault-stop-readonly-latest.json').read_text(encoding='utf-8'))
        self.row=json.loads((Path(pointer['directory'])/'snapshot.json').read_text(encoding='utf-8'))
        f=self.row['fields']
        if self.row.get('verified_image')!='patrol-chassis-0.2.76' or f['power_command'] or f['session_active'] or f['enable_request'] or f['fault_pending'] or not f['fault_latched'] or f['fault_stop_active'] or f['fault_stop_disabled_mask']!=3:
            raise RuntimeError('MCU not parked with both disables confirmed; refuse competing master')

    # 只返回上述已验证字段；不把历史轮速缓存当作当前轮速。
    def sample(self):return self.row

    # SWD helper 已关闭探针；此接口保持原有脚本的资源清理约定。
    def close(self):pass


# 发送入口仅允许标准 SDO upload；其他写入、NMT、使能和运动触发在进入 DLL 前拒绝。
class UploadOnlyCanalyst(Canalyst):
    def transmit(self,cob,data):
        index=int.from_bytes(data[1:3],'little') if len(data)>=4 else -1
        allowed={0x601:{0x6041,0x6064,0x6081},0x602:{0x6041,0x6069,0x6869}}
        if len(data)!=8 or data[0]!=0x40 or data[3]!=0 or data[4:]!=bytes(4) or index not in allowed.get(cob,set()):
            raise RuntimeError('Blocked non-whitelisted CAN transmission')
        return super().transmit(cob,data)

    # 接收失败与无帧分开处理，避免 USB 断开伪装成单个驱动器漏应答。
    def receive(self,timeout_ms):
        obj=VciCanObj();count=int(self.dll.VCI_Receive(self.dev_type,self.dev_index,self.channel,ctypes.byref(obj),1,int(timeout_ms)))
        if count==0:return None
        if count!=1:raise RuntimeError('CAN receive failed: '+hex(count))
        if obj.DataLen>8 or obj.ExternFlag or obj.RemoteFlag:return (int(obj.ID),bytes())
        return int(obj.ID),bytes(obj.Data[:obj.DataLen])


# 初始化和结束时独立读取双节点状态及三组双轮转速；任何使能位或超过 20 rpm 均拒绝继续。
def verify_stopped(can,record):
    result=[]
    for node,index in [(1,0x6041),(2,0x6041)]+[(2,index) for _ in range(3) for index in (0x6069,0x6869)]:
        payload=bytes([0x40,index&255,index>>8,0,0,0,0,0])
        if not can.transmit(0x600+node,payload):raise RuntimeError('Safety read transmit failed')
        record('TX_READ_ONLY_PREFLIGHT',0x600+node,payload)
        deadline=time.perf_counter()+.3;value=None
        while time.perf_counter()<deadline:
            f=can.receive(0)
            if not f:time.sleep(.0005);continue
            cob,data=f;record('RX',cob,data)
            if cob in (0,0x601,0x602):raise RuntimeError('Another CAN master active')
            if cob!=0x580+node or len(data)<4 or int.from_bytes(data[1:3],'little')!=index or data[3]!=0:continue
            width={0x43:4,0x47:3,0x4b:2,0x4f:1}.get(data[0])
            if not width or len(data)<4+width:raise RuntimeError('Safety read returned invalid response')
            value=int.from_bytes(data[4:4+width],'little',signed=index in (0x6069,0x6869));break
        if value is None:raise RuntimeError('Safety read timed out')
        if (index==0x6041 and value&4) or (index!=0x6041 and abs(value)>20):raise RuntimeError('Motor not safely stopped: '+str((node,index,value)))
        result.append(dict(node=node,index=hex(index),value=value))
    return result



# 仅发送 Node 1 upload，改变读取时刻以核对 PDO/心跳并发附近的应答。
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('sweep','after-heartbeat'),default='sweep')
    parser.add_argument('--heartbeat-state',type=int,choices=(5,127),default=5,help='expected Node 1 NMT state; this script never changes it')
    parser.add_argument('--count',type=int,default=400,help='planned reads, 1..5000; one per heartbeat')
    args=parser.parse_args()
    if not 1<=args.count<=5000:parser.error('--count must be 1..5000')
    dest=Path(__file__).parent/'logs'/datetime.now().strftime('steering-timing-compare-%Y%m%d-%H%M%S-%f');dest.mkdir()
    can=UploadOnlyCanalyst(channel=1,dll_path=str(DEFAULT_DLL));observer=None;opened=False
    start=time.perf_counter();last_hb=None;periods=[];schedule=[];pending=None;results=[];planned=0;sent=0;error=None;initial_safety=None;final_safety=None;adapter_before=None;adapter_after=None
    with (dest/'frames.jsonl').open('x',encoding='utf-8') as log:
        def record(direction,cob,payload):
            log.write(json.dumps(dict(elapsed_s=time.perf_counter()-start,direction=direction,id=cob,data=payload.hex(' ')))+'\n')
        try:
            observer=Observer();print('MCU_CHECK',json.dumps(observer.sample()),flush=True)
            observer.close();observer=None
            can.open();opened=True
            initial_safety=verify_stopped(can,record)
            adapter_before=dict(status=query(can,'VCI_ReadCANStatus',Status),errors=query(can,'VCI_ReadErrInfo',Errors))
            while time.perf_counter()-start<max(65,args.count*.12+10) and (planned<args.count or pending or schedule):
                now=time.perf_counter();f=can.receive(0)
                if f:
                    cob,p=f;record('RX',cob,p)
                    if cob in (0,0x601,0x602):raise RuntimeError('another CAN master active')
                    if cob==0x701 and p==bytes([args.heartbeat_state]):
                        if last_hb is not None and .08<now-last_hb<.12:periods.append(now-last_hb)
                        last_hb=now
                        if len(periods)>=10 and planned<args.count:
                            # 主机收到心跳与实际总线发送存在 USB 延迟，扫描整个 ±5ms 窗口。
                            offset=((planned%41)-20)*.00025 if args.mode=='sweep' else .030
                            due=now+statistics.median(periods[-30:])+offset if args.mode=='sweep' else now+offset
                            heapq.heappush(schedule,(due,planned,offset))
                            planned+=1
                    if pending and cob==0x581 and len(p)>=4 and int.from_bytes(p[1:3],'little')==pending['index'] and p[3]==0:
                        n={0x43:4,0x47:3,0x4b:2,0x4f:1}.get(p[0])
                        if n and len(p)>=4+n:
                            pending.update(status='ok',value=int.from_bytes(p[4:4+n],'little'),host_roundtrip_ms=(now-pending['at'])*1000)
                            if pending['index']==0x6041 and pending['value']&4:raise RuntimeError('Steering enabled during read-only comparison')
                        elif p[0]==0x80 and len(p)>=8:pending.update(status='abort',abort=hex(int.from_bytes(p[4:8],'little')))
                        if 'status' in pending:
                            results.append(pending);pending=None
                            if len(results)%400==0:print('PROGRESS',json.dumps(dict(completed=len(results),sent=sent,timeouts=sum(r.get('status')=='timeout' for r in results))),flush=True)
                if pending and now-pending['at']>=.3:
                    pending.update(status='timeout',adapter_status=query(can,'VCI_ReadCANStatus',Status),adapter_errors=query(can,'VCI_ReadErrInfo',Errors));results.append(pending);print('MISSING',json.dumps(pending),flush=True);pending=None
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
            if opened:
                try:
                    if error is None:final_safety=verify_stopped(can,record)
                    adapter_after=dict(status=query(can,'VCI_ReadCANStatus',Status),errors=query(can,'VCI_ReadErrInfo',Errors))
                except Exception as exc:error='Final check: '+str(exc)
                finally:can.close()
    report=dict(product='patrol_vehicle_chassis',read_only=True,hardware_listen_only=False,upload_only=True,mode=args.mode,initial_safety=initial_safety,final_safety=final_safety,adapter_before=adapter_before,adapter_after=adapter_after,physical_channel=2,expected_heartbeat_state=args.heartbeat_state,planned=args.count,script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),sent=sent,results=results,error=error,
        purpose='Sweep upload request host timing around periodic heartbeat; Compare a +/-5ms heartbeat sweep with reads 30ms after received heartbeat; host timing does not prove exact wire phase. No parameter writes, NMT, trigger or enable.')
    (dest/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('SUMMARY',json.dumps(dict(sent=sent,statuses={k:sum(r.get('status')==k for r in results) for k in ('ok','timeout','abort')},error=error)),flush=True)
    print('LOG',dest,flush=True)


if __name__=='__main__':main()

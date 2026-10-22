"""Read-only steering SDO response probe after the operator-confirmed stop."""
import json
import struct
import time
from pathlib import Path
from datetime import datetime
from chassis_offline_remote import Canalyst
from can1_bus_monitor import DEFAULT_DLL

clock = time.perf_counter


# 只读取 Node 1 转向轮廓速度、状态和模式；不写参数，不使能或复位。
def main():
    dest=Path(__file__).parent/'logs'/datetime.now().strftime('steering-readonly-%Y%m%d-%H%M%S-%f')
    dest.mkdir(parents=True)
    can=Canalyst(channel=1,dll_path=str(DEFAULT_DLL))
    start=clock(); results=[]
    with (dest/'frames.jsonl').open('x',encoding='utf-8') as log:
        def event(direction,cob,data):
            log.write(json.dumps(dict(elapsed_s=clock()-start,direction=direction,id=cob,data=data.hex(' ').upper()))+'\n');log.flush()
        try:
            can.open()
            for index in [0x6081]*20+[0x6080,0x6041,0x6061]:
                # 清读旧缓存，避免将发送请求前已存在的同索引响应计入应答时间。
                deadline=clock()+.02
                while clock()<deadline:
                    frame=can.receive(0)
                    if frame: event('RX',*frame)
                    else: time.sleep(.001)
                request=bytes([0x40,index&255,index>>8,0,0,0,0,0]); assert request[0]==0x40
                at=clock(); sent=can.transmit(0x601,request);event('TX_READ_ONLY',0x601,request)
                result={'index':hex(index),'sent':sent}
                deadline=at+.3
                while sent and clock()<deadline:
                    frame=can.receive(0)
                    if frame is None: time.sleep(.001);continue
                    event('RX',*frame);cob,data=frame
                    if cob!=0x581 or len(data)<4 or int.from_bytes(data[1:3],'little')!=index or data[3]!=0:continue
                    if data[0]==0x80:
                        if len(data)<8:continue
                        result.update(status='abort',abort=hex(int.from_bytes(data[4:8],'little')));break
                    widths={0x43:4,0x47:3,0x4b:2,0x4f:1}
                    if data[0] not in widths:continue
                    width=widths[data[0]]
                    if len(data)<4+width:continue
                    result.update(status='ok',width=width,value=int.from_bytes(data[4:4+width],'little'),host_roundtrip_ms=(clock()-at)*1000);break
                if 'status' not in result:result['status']='timeout' if sent else 'send_failed'
                results.append(result)
                print(json.dumps(result),flush=True)
                time.sleep(.08)
        finally:can.close()
    (dest/'results.json').write_text(json.dumps({'physical_channel':2,'read_only':True,'results':results,'limitations':'Host round trips after stop; not the original failing SDO timing or MCU receive timestamps.'},indent=2),encoding='utf-8')
    print('LOG:',dest,flush=True)


if __name__=='__main__':main()

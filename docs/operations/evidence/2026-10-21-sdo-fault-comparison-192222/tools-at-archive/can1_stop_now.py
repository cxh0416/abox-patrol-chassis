"""Operator-requested CAN1 stop: zeros and shutdown only; never enable/reset."""
import ctypes
import json
import time
from datetime import datetime
from pathlib import Path
from can1_bus_monitor import CanObject, InitConfig, DEFAULT_DLL, decode


def main():
    directory = Path(__file__).parent / 'logs' / datetime.now().strftime('direct-stop-%Y%m%d-%H%M%S-%f')
    directory.mkdir(parents=True)
    print('LOG:', directory, flush=True)
    dll = ctypes.WinDLL(str(DEFAULT_DLL))
    u = ctypes.c_uint32
    for name, args in {
        'VCI_OpenDevice': [u,u,u], 'VCI_CloseDevice': [u,u],
        'VCI_InitCAN': [u,u,u,ctypes.POINTER(InitConfig)],
        'VCI_StartCAN': [u,u,u],
        'VCI_Receive': [u,u,u,ctypes.POINTER(CanObject),u,ctypes.c_int32],
        'VCI_Transmit': [u,u,u,ctypes.POINTER(CanObject),u],
    }.items():
        fn = getattr(dll, name)
        fn.argtypes, fn.restype = args, u
    started = time.monotonic()
    channels, counts, latest = [], {}, {}
    opened = False
    report = {'purpose':'operator requested stop', 'writes_allowed': ['node2:60FF=0','node2:68FF=0','node2:6040=6'], 'motion_success':False}
    with (directory/'frames.jsonl').open('x', encoding='utf-8') as log:
        # 逐帧保存分析仪原始收发，不把驱动器应答当作已停车。
        def record(row):
            row['elapsed_s'] = time.monotonic()-started
            log.write(json.dumps(row)+'\n')
            log.flush()
        buffer = (CanObject*2048)()
        # 两路先识别驱动器总线，避免把 CANopen 写入遥控接收器支路。
        def receive():
            seen = set()
            for ch in channels:
                n = int(dll.VCI_Receive(4,0,ch,buffer,2048,0))
                if n > 2048:
                    raise RuntimeError('receive failed CH%d'% (ch+1))
                for obj in buffer[:n]:
                    f = dict(id=int(obj.ID),dlc=int(obj.DataLen),data=bytes(obj.Data[:min(obj.DataLen,8)]).hex(' ').upper(),extended=bool(obj.ExternFlag),remote=bool(obj.RemoteFlag))
                    item = decode(f)
                    record(dict(direction='RX',channel=ch+1,**f,decoded=item))
                    counts[(ch,f['id'])] = counts.get((ch,f['id']),0)+1
                    if not f['extended'] and not f['remote'] and (f['id']==0x702 and f['dlc']==1 or f['id']==0x582 and f['dlc']==8):
                        seen.add(ch)
                    if item and item.get('kind')=='sdo' and item.get('node')==2 and item.get('action')=='read_response':
                        latest[str(item['index_hex'])] = dict(channel=ch+1,at_s=time.monotonic()-started,value=item['value'])
            return seen
        # 限定只发零速、shutdown 和状态/轮速/目标读取，禁止其他写入。
        def transmit(ch, data):
            assert data in (bytes.fromhex('23 FF 60 00 00 00 00 00'),bytes.fromhex('23 FF 68 00 00 00 00 00'),bytes.fromhex('2B 40 60 00 06 00 00 00')) or data[0]==0x40 and int.from_bytes(data[1:3],'little') in (0x6041,0x6069,0x6869,0x60ff,0x68ff)
            f=CanObject(); f.ID=0x602; f.DataLen=8; f.SendType=1
            for i,b in enumerate(data): f.Data[i]=b
            sent=int(dll.VCI_Transmit(4,0,ch,ctypes.byref(f),1))
            record(dict(direction='TX',channel=ch+1,id=0x602,data=data.hex(' ').upper(),sdk_sent=sent))
            if sent!=1: raise RuntimeError('transmit failed CH%d'% (ch+1))
        try:
            if dll.VCI_OpenDevice(4,0,0)!=1:
                raise RuntimeError('Cannot open analyzer. Close CANTest/other CAN programs; check USB.')
            opened=True
            for ch in (0,1):
                cfg=InitConfig(); cfg.AccMask=0xffffffff; cfg.Filter=1; cfg.Timing0=0; cfg.Timing1=0x1c; cfg.Mode=0
                if dll.VCI_InitCAN(4,0,ch,ctypes.byref(cfg))==1 and dll.VCI_StartCAN(4,0,ch)==1:
                    channels.append(ch)
            found=set(); deadline=time.monotonic()+1.2
            while time.monotonic()<deadline:
                found.update(receive())
                if found: break
                time.sleep(.005)
            if not found:
                raise RuntimeError('No node2 response/heartbeat on either analyzer channel. No stop writes sent.')
            ch=min(found); report['physical_channel']=ch+1
            print('DRIVE BUS: physical CH%d; sending both zero targets and shutdown.'%(ch+1),flush=True)
            for _ in range(3):
                for data in ('23 FF 60 00 00 00 00 00','23 FF 68 00 00 00 00 00','2B 40 60 00 06 00 00 00'):
                    transmit(ch,bytes.fromhex(data)); receive(); time.sleep(.015)
                time.sleep(.05)
            queries=(0x6041,0x6069,0x6869,0x60ff,0x68ff)
            deadline=time.monotonic()+5; next_read=0; cursor=0
            while time.monotonic()<deadline:
                receive()
                if time.monotonic()>=next_read:
                    index=queries[cursor%len(queries)];cursor+=1
                    transmit(ch,bytes([0x40,index&255,index>>8,0,0,0,0,0]))
                    next_read=time.monotonic()+.09
                time.sleep(.005)
            receive(); report['latest']=latest
            print(json.dumps(latest,indent=2),flush=True)
            report['result']='stop commands sent; inspect speed feedback and physical wheels'
        except Exception as exc:
            report['error']=str(exc); print('ERROR:',exc,flush=True)
        finally:
            if opened: dll.VCI_CloseDevice(4,0)
            report['frame_counts']={f'CH{ch+1}:0x{cob:X}':n for (ch,cob),n in counts.items()}
            (directory/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
            print('RESULT:',directory/'result.json',flush=True)


if __name__=='__main__':
    main()

"""Operator-requested CAN1 steering recovery. Never enable drive, reset or save EEPROM."""
import argparse
import ctypes
import hashlib
import json
import struct
import time
from datetime import datetime
from pathlib import Path
from chassis_offline_remote import Canalyst
from can1_bus_monitor import DEFAULT_DLL

REPO=Path(r'E:\1_work\ChassisCar\abox-patrol-vehicle-chassis-firmware')
APP_SHA='b46d49d5c079a1e5285d62ac917c9528bdef1bc97635ae5f42c9d0e4676b2a97'


# 不暂停或重启主控，只读取匹配固件的编码器及会话状态。
class Observer:
    def __init__(self):
        scope={}
        src=(REPO/'out/logs/steering-live-readonly.py').read_text()
        exec(src[src.index('j=ctypes.WinDLL'):src.index("command('DisableAutoUpdateFW')")],globals(),scope)
        # exec 定义函数的全局命名空间需包含其使用的 J-Link 实例。
        self.j=scope['j'];globals()['j']=self.j
        self.read=scope['read'];command=scope['command']
        for s in ('DisableAutoUpdateFW','SuppressGUI 1','InhibitConnectRetries = 1','ForceAttachTarget = 1'):command(s)
        err=self.j.JLINKARM_Open()
        if err:raise RuntimeError(err.decode(errors='replace'))
        command('Device = Cortex-M3');self.j.JLINKARM_TIF_Select(1);self.j.JLINKARM_SetSpeed(1000)
        if self.j.JLINKARM_Connect()<0:raise RuntimeError('SWD connect failed')
        if hashlib.sha256(self.read(0x08008000,151016)).hexdigest()!=APP_SHA:raise RuntimeError('firmware mismatch')

    def sample(self):
        tick=int.from_bytes(self.read(0x2000021c,4),'little')
        diag=self.read(0x20008540,224)
        flags=self.read(0x200080f0+66,8)
        center=struct.unpack('<i',self.read(0x20008540+224+36,4))[0]
        power=self.read(0x20008438,1)[0]
        encoder=struct.unpack_from('<I',diag,92)[0]
        delta=encoder if encoder<=2048 else encoder-4096
        row=dict(tick_ms=tick,encoder=encoder,encoder_angle_deg=delta/55.25,
                 encoder_age_ms=(tick-struct.unpack_from('<I',diag,36)[0])&0xffffffff,
                 encoder_config_valid=diag[162],center=center,session=flags[0],enable=flags[3],power=power)
        # Tick 与反馈为顺序只读；反馈可能稍晚于 Tick，允许一帧读取造成的微小负年龄。
        if row['encoder_age_ms']>0xffff0000:row['encoder_age_ms']=0
        if encoder>=4096 or not row['encoder_config_valid'] or row['encoder_age_ms']>250:raise RuntimeError('encoder invalid/stale')
        if power or flags[0] or flags[3]:raise RuntimeError('MCU control/output active; refuse competing master')
        if self.read(0x200087B1,1)[0]:raise RuntimeError('MCU CAN Bus-Off')
        return row

    def close(self):self.j.JLINKARM_Close()


# 所有收发逐帧记录；只允许 Node 1 中位恢复和 Node 2 停车写入。
class Recovery:
    def __init__(self,can,log):self.can=can;self.log=log;self.start=time.perf_counter()
    def record(self,kind,**row):
        self.log.write(json.dumps(dict(kind=kind,elapsed_s=time.perf_counter()-self.start,**row))+'\n');self.log.flush()
    def sdo(self,node,index,value=None,width=4,signed=False):
        if value is None:data=bytes([0x40,index&255,index>>8,0,0,0,0,0])
        else:
            assert node==1 and (index==0x6081 and value==60 or index==0x607a or index==0x6040 and value in (6,7,15,0x2f,0x3f)) or node==2 and (index in (0x60ff,0x68ff) and value==0 or index==0x6040 and value==6)
            data=bytes([{1:0x2f,2:0x2b,4:0x23}[width],index&255,index>>8,0])+int(value).to_bytes(width,'little',signed=signed)+bytes(4-width)
        if not self.can.transmit(0x600+node,data):raise RuntimeError('CAN transmit failed')
        self.record('TX',id=0x600+node,data=data.hex(' '))
        deadline=time.perf_counter()+.3
        while time.perf_counter()<deadline:
            f=self.can.receive(0)
            if not f:time.sleep(.001);continue
            cob,p=f;self.record('RX',id=cob,data=p.hex(' '))
            if cob in (0x601,0x602):raise RuntimeError('another CAN master is issuing commands')
            if cob!=0x580+node or len(p)<4 or p[1:4]!=data[1:4]:continue
            if p[0]==0x80:raise RuntimeError('SDO abort '+p[4:8].hex())
            if value is not None and p[0]==0x60:return
            n={0x43:4,0x47:3,0x4b:2,0x4f:1}.get(p[0])
            if value is None and n and len(p)>=4+n:return int.from_bytes(p[4:4+n],'little',signed=signed)
        raise RuntimeError('SDO timeout node%d index%04x'%(node,index))
    def wheels(self):
        left=self.sdo(2,0x6069,signed=True);right=self.sdo(2,0x6869,signed=True)
        sw=self.sdo(2,0x6041)
        if abs(left)>20 or abs(right)>20 or sw&4:raise RuntimeError('drive not stationary/disabled')
        return dict(left=left,right=right,status=hex(sw))


# 默认仅检查；执行时仅按本次中位低速回中，反馈异常立即撤转向使能。
def main():
    p=argparse.ArgumentParser();p.add_argument('--execute',action='store_true');a=p.parse_args()
    dest=Path(__file__).parent/'logs'/datetime.now().strftime('steering-center-%Y%m%d-%H%M%S-%f');dest.mkdir(parents=True)
    can=Canalyst(channel=1,dll_path=str(DEFAULT_DLL));obs=None;opened=False;moving=False;report={'execute':a.execute,'physical_channel':2}
    with (dest/'frames.jsonl').open('x',encoding='utf-8') as log:
        rec=Recovery(can,log)
        try:
            obs=Observer();can.open();opened=True
            # 排空只听模式关闭前和本次打开时的旧缓存。
            until=time.perf_counter()+.1
            while time.perf_counter()<until:
                f=can.receive(0)
                if f:rec.record('RX',id=f[0],data=f[1].hex(' '))
            for _ in range(3):rec.wheels();time.sleep(.05)
            if rec.sdo(2,0x60ff,signed=True)!=0 or rec.sdo(2,0x68ff,signed=True)!=0:raise RuntimeError('drive targets nonzero')
            sw=rec.sdo(1,0x6041);fault=rec.sdo(1,0x603f);mode=rec.sdo(1,0x6061)
            pos=rec.sdo(1,0x6064,signed=True);low=rec.sdo(1,0x201a,signed=True);high=rec.sdo(1,0x2019,signed=True)
            sample=obs.sample();center=sample['center'];angle=(pos-center)/2710.8
            report['preflight']=dict(sample=sample,position=pos,center=center,driver_angle_deg=angle,limits=[low,high],status=hex(sw),fault=fault,mode=mode)
            print(json.dumps(report['preflight'],indent=2),flush=True);rec.record('preflight',**report['preflight'])
            if fault or sw&8 or mode!=1 or sw&0x6f not in (0x21,0x23,0x27,0x40):raise RuntimeError('steering is not healthy in position mode')
            if not low<center<high or not low<=pos<=high or abs(angle)>30.5:raise RuntimeError('invalid center/position/limits')
            if abs(angle-sample['encoder_angle_deg'])>2:raise RuntimeError('driver/encoder disagree; refuse center')
            if a.execute:
                moving=True
                rec.sdo(1,0x6081,60);assert rec.sdo(1,0x6081)==60
                if sw&0x6f!=0x27:
                    # 先保持实时位置，再仅恢复 Node 1 的状态机；不清故障、不使能行走。
                    rec.sdo(1,0x607a,pos,signed=True)
                    for cw,expected in ((6,0x21),(7,0x23),(15,0x27)):
                        rec.sdo(1,0x6040,cw,width=2)
                        time.sleep(.03)
                        if rec.sdo(1,0x6041)&0x6f!=expected:raise RuntimeError('steering state transition failed')
                rec.sdo(1,0x6040,0x2f,width=2)
                rec.sdo(1,0x607a,center,signed=True)
                rec.sdo(1,0x6040,0x3f,width=2);rec.sdo(1,0x6040,0x2f,width=2)
                until=time.perf_counter()+45;stable=0;prior=abs(angle)
                while time.perf_counter()<until:
                    pos=rec.sdo(1,0x6064,signed=True);sample=obs.sample();angle=(pos-center)/2710.8
                    rec.wheels();sw=rec.sdo(1,0x6041)
                    row=dict(position=pos,driver_angle_deg=angle,sample=sample,status=hex(sw));rec.record('progress',**row)
                    print(json.dumps(row),flush=True)
                    if sw&8 or abs(angle)>30.5 or abs(angle-sample['encoder_angle_deg'])>2 or abs(angle)>prior+1:raise RuntimeError('steering feedback fault/divergence')
                    prior=min(prior,abs(angle))
                    stable=stable+1 if abs(angle)<=.3 and abs(sample['encoder_angle_deg'])<=.5 else 0
                    if stable>=3:report['feedback_centered']=True;report['final']=row;break
                    time.sleep(.08)
                else:raise RuntimeError('return-to-center timeout')
                rec.sdo(1,0x6040,6,width=2)
                # SDO 写应答先于驱动内部状态更新；保持单次 shutdown，轮询确认。
                disabled_deadline=time.perf_counter()+1
                while True:
                    final_status=rec.sdo(1,0x6041)
                    if not final_status&4:break
                    if time.perf_counter()>=disabled_deadline:raise RuntimeError('steering disable not confirmed')
                    time.sleep(.02)
                moving=False
                report['steering_final_status']=hex(final_status);report['drive_final']=rec.wheels()
            report['result']='completed' if a.execute else 'inspection only'
        except Exception as exc:
            report['error']=str(exc);print('ERROR',exc,flush=True)
        finally:
            if moving:
                try:rec.sdo(1,0x6040,6,width=2)
                except Exception as exc:report['shutdown_error']=str(exc)
            if opened:can.close()
            if obs:obs.close()
            (dest/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
            print('LOG',dest,flush=True)
    return 1 if 'error' in report else 0


if __name__=='__main__':raise SystemExit(main())

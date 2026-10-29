"""Passive CAN2 bitrate scan; CAN1 stays configured at 500K in each window."""
import collections
import datetime
import hashlib
import json
from pathlib import Path
import sys
import time

sys.path.insert(0,'E:/1_work/ChassisCar_test')
from can1_bus_monitor import Listener, InitConfig, TIMINGS, DEFAULT_DLL
import ctypes

root=Path.cwd()
output=Path('E:/1_work/ChassisCar_test/logs')/datetime.datetime.now().strftime('car2-receiver-bitrate-%Y%m%d-%H%M%S-%f')
output.mkdir();results=[];start=time.monotonic()


class PerBusListener(Listener):
    def open(self):
        if self.dll.VCI_OpenDevice(4,self.device_index,0)!=1:raise RuntimeError('Adapter open failed')
        self.opened=True
        try:
            for bus,channel in self.channels.items():
                bitrate=500 if bus=='CAN1' else self.bitrate
                cfg=InitConfig();cfg.AccMask=0xffffffff;cfg.Filter=1;cfg.Mode=1
                cfg.Timing0,cfg.Timing1=TIMINGS[bitrate]
                if self.dll.VCI_InitCAN(4,self.device_index,channel,ctypes.byref(cfg))!=1:raise RuntimeError('Listen-only init failed')
                if self.dll.VCI_StartCAN(4,self.device_index,channel)!=1:raise RuntimeError('CAN start failed')
        except Exception:
            self.close();raise


error=None
with (output/'frames.jsonl').open('x',encoding='utf-8') as raw:
    try:
        for rate in (500,250,125,1000,500):
            listener=PerBusListener(DEFAULT_DLL,{'CAN1':1,'CAN2':0},rate)
            counts=collections.Counter();ids=collections.Counter();window_start=time.monotonic()
            try:
                listener.open();window_start=time.monotonic();deadline=window_start+8
                while time.monotonic()<deadline:
                    for bus,channel in listener.channels.items():
                        for frame in listener.receive(channel):
                            counts[bus]+=1;ids[(bus,frame['id'])]+=1
                            row=dict(bus=bus,channel=channel,bitrate_kbps=500 if bus=='CAN1' else rate,
                                     elapsed_s=time.monotonic()-start,**frame)
                            raw.write(json.dumps(row)+'\n')
                    time.sleep(.002)
            finally:listener.close()
            result={'CAN2_bitrate_kbps':rate,'CAN1_bitrate_kbps':500,'hardware_listen_only':True,
                    'window_host_s':time.monotonic()-window_start,'counts':dict(counts),
                    'ids':{f'{bus}:0x{cob:X}':n for (bus,cob),n in ids.items()}}
            results.append(result);raw.flush();print(json.dumps(result),flush=True)
    except Exception as exc:error=type(exc).__name__+': '+str(exc)
summary={'created_at':datetime.datetime.now().astimezone().isoformat(),'hardware_listen_only':True,
         'transmit_api_bound':False,'results':results,'error':error,
         'limits':['Short listen-only windows do not exclude all bitrates, invalid/error frames, wiring failures or a transmitter already bus-off.',
                   'Wireless link indicator does not establish CAN output; no active CAN probe or driver/motor writes.']}
(output/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in output.iterdir() if p.is_file()}
(output/'sha256.json').write_text(json.dumps(files,indent=2),encoding='utf-8')
(root/'out/logs/car2-receiver-bitrate-latest.json').write_text(json.dumps({'directory':str(output),'error':error},ensure_ascii=False),encoding='utf-8')
print('OUTPUT',output,flush=True)
if error:raise SystemExit(error)

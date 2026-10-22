"""Read analyzer diagnostic API buffers; no application CAN transmission or vehicle writes."""
import ctypes
import json
import time
from datetime import datetime
from pathlib import Path
from chassis_offline_remote import Canalyst
from can1_bus_monitor import DEFAULT_DLL
from steering_center_once import Observer


# 布局来自随分析仪驱动提供的 ControlCAN.pas / ControlCAN.bas，保留填充哨兵以识别空实现。
class Status(ctypes.Structure):
    _fields_=[(n,ctypes.c_ubyte) for n in ('ErrInterrupt','regMode','regStatus','regALCapture','regECCapture','regEWLimit','regRECounter','regTECounter')]+[('Reserved',ctypes.c_uint32)]


class Errors(ctypes.Structure):
    _fields_=[('ErrCode',ctypes.c_uint32),('Passive_ErrData',ctypes.c_ubyte*3),('ArLost_ErrData',ctypes.c_ubyte)]


# 仅查询分析仪本地寄存器/错误接口；成功返回但未回填的数据不能用来声称总线无错误。
def query(can,name,typ):
    fn=getattr(can.dll,name)
    fn.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(typ)];fn.restype=ctypes.c_uint32
    value=typ();ctypes.memset(ctypes.byref(value),0xa5,ctypes.sizeof(value))
    code=fn(can.dev_type,can.dev_index,can.channel,ctypes.byref(value))
    raw=bytes(value)
    return dict(return_code=code,raw=raw.hex(),buffer_changed=raw!=b'\xa5'*len(raw),
                values={k:(list(getattr(value,k)) if isinstance(getattr(value,k),ctypes.Array) else getattr(value,k)) for k,*_ in typ._fields_})


def main():
    dest=Path(__file__).parent/'logs'/datetime.now().strftime('adapter-status-readonly-%Y%m%d-%H%M%S-%f');dest.mkdir()
    can=Canalyst(channel=1,dll_path=str(DEFAULT_DLL));observer=None;opened=False;report=dict(transmit_called=False,physical_channel=2,queries=[])
    try:
        observer=Observer();report['mcu']=observer.sample();observer.close();observer=None
        can.open();opened=True
        for _ in range(3):
            report['queries'].append(dict(status=query(can,'VCI_ReadCANStatus',Status),errors=query(can,'VCI_ReadErrInfo',Errors)))
            time.sleep(.05)
    except Exception as exc:report['error']=str(exc)
    finally:
        if observer:observer.close()
        if opened:can.close()
    (dest/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2),flush=True);print('LOG',dest,flush=True)


if __name__=='__main__':main()

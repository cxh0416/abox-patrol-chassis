import ctypes
import json
from pathlib import Path

ctx=json.loads(Path('out/logs/steering-timeout-context.json').read_text())
private=Path(ctx['private']);public=Path(ctx['public'])
j=ctypes.WinDLL(r'E:/SEGGER/JLink_V984/JLink_x64.dll')
j.JLINKARM_ExecCommand.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int]
j.JLINKARM_ExecCommand.restype=ctypes.c_int
j.JLINKARM_Open.restype=ctypes.c_char_p
j.JLINKARM_Connect.restype=ctypes.c_int
j.JLINKARM_TIF_Select.argtypes=[ctypes.c_int]
j.JLINKARM_SetSpeed.argtypes=[ctypes.c_uint32]
j.JLINKARM_ReadMemEx.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32]
j.JLINKARM_ReadMemEx.restype=ctypes.c_int

def command(s):
    out=ctypes.create_string_buffer(1024)
    result=j.JLINKARM_ExecCommand(s.encode(),out,len(out))
    print('COMMAND',s,'result',result,out.value.decode(errors='replace'),flush=True)
    if result<0:raise RuntimeError('unsupported J-Link command')

def read(address,count):
    buf=(ctypes.c_uint8*count)()
    done=j.JLINKARM_ReadMemEx(address,count,buf,0)
    if done!=count:raise RuntimeError(f'Read failed {address:x}: {done}/{count}')
    return bytes(buf)

command('DisableAutoUpdateFW')
command('SuppressGUI 1')
command('InhibitConnectRetries = 1')
command('ForceAttachTarget = 1')
error=j.JLINKARM_Open()
print('OPEN',error.decode(errors='replace') if error else 'OK',flush=True)
if error:raise SystemExit(2)
try:
    command('Device = Cortex-M3')
    j.JLINKARM_TIF_Select(1);j.JLINKARM_SetSpeed(1000)
    result=j.JLINKARM_Connect();print('CONNECT',result,flush=True)
    if result<0:raise SystemExit(3)
    print('DHCSR before',read(0xE000EDF0,4).hex(),flush=True)
    (private/'flash-dll.bin').write_bytes(read(0x08000000,0x40000))
    (private/'ram-dll.bin').write_bytes(read(0x20000000,0x10000))
    for name,address,size in [('GPIOA',0x40010800,20),('CAN1',0x40006400,32),('CAN2',0x40006800,32),('DHCSR',0xE000EDF0,4)]:
        data=read(address,size)
        (public/f'{name}-registers.bin').write_bytes(data)
        print(name,data.hex(),flush=True)
finally:
    j.JLINKARM_Close()

"""Exact-image fault snapshot; attach-only SWD, no target writes or run control."""
import ctypes
from datetime import datetime, timezone, timedelta
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

root=Path.cwd()
build=root/'app/build/Steering800'
elf=build/'Patrol_Vehicle_Chassis_App.elf'
app=build/'Patrol_Vehicle_Chassis_App.bin'
stamp=datetime.now(timezone(timedelta(hours=8)))
folder=root/'docs/operations/evidence'/stamp.strftime('%Y-%m-%d-steering-fault-readonly-%H%M%S-%f')
folder.mkdir(parents=True)
nm=subprocess.check_output(['arm-none-eabi-nm','-S',str(elf)],text=True)
symbols={}
for line in nm.splitlines():
    parts=line.split()
    if len(parts)==4 and parts[2] in ('B','b','D','d'):
        symbols.setdefault(parts[3],[]).append((int(parts[0],16),int(parts[1],16)))
types={'g_control':'ChassisControl','g_motion':'PatrolMotion','fault_stop':'PatrolFaultStop',
       'last_fault':'MotionFaultRecord','g_patrol_config':'PatrolRemoteConfigV5_t','hcan1':'CAN_HandleTypeDef'}
old=json.loads((root/'out/logs/car2-0277-schema.json').read_text(encoding='utf-8'))['fields']
fields={k:v for k,v in old.items() if re.split(r'[.\[]',v['expression'])[0] in types or v['expression'] in ('uwTick','g_commanded_on','g_can_bus_off_mask','g_can_rx_overflow_count')}
rows=[]; bases=[]
for name,f in fields.items():
    expr=f['expression']; base=expr.split('.')[0]; address,size=symbols[base][0];bases.append(address)
    if '.' in expr:
        suffix=expr.split('.',1)[1]; typ=types[base]
        rows.append('{offsetof('+typ+','+suffix+'),sizeof((('+typ+'*)0)->'+suffix+')}')
    else:
        rows.append('{0,'+str(size)+'}')
source='#include <stddef.h>\n#include <stdint.h>\n#include "motion_internal.h"\n#include "remote_service.h"\n#include "patrol_fault_stop.h"\n#include "patrol_config.h"\n#include "can.h"\n'
for name,typ in types.items():
    source+=f'_Static_assert(sizeof({typ})=={symbols[name][0][1]},"ELF ABI {name}");\n'
source+='const uint32_t schema[][2] __attribute__((used,section(".schema")))={'+','.join(rows)+'};\n'
abi=root/'out/logs/steering-current-abi.c';abi.write_text(source,encoding='utf-8')
commands=json.loads((build/'compile_commands.json').read_text())
command=next(c for c in commands if c['file'].endswith('patrol_runtime.c'))
args=command['command'].split();flags=[];skip=False
for a in args[1:]:
    if skip:skip=False;continue
    if a=='-o':skip=True;continue
    if a=='-c' or a.replace('\\','/')==command['file'].replace('\\','/') or a.startswith('-DPATROL_APP_FW_VERSION='):continue
    flags.append(a.replace('\\','/') if a.startswith('-I') else a)
obj=abi.with_suffix('.o');binary=abi.with_suffix('.bin')
subprocess.run([args[0],*flags,'-c',str(abi),'-o',str(obj)],check=True)
subprocess.run(['arm-none-eabi-objcopy','-O','binary','--only-section=.schema',str(obj),str(binary)],check=True)
for f,base,(offset,size) in zip(fields.values(),bases,struct.iter_unpack('<II',binary.read_bytes())):
    f['address']=base+offset;f['bytes']=size;f['address_hex']=hex(base+offset)
schema=dict(app_sha256=hashlib.sha256(app.read_bytes()).hexdigest(),app_bytes=app.stat().st_size,
            elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),fields=fields)
(folder/'schema.json').write_text(json.dumps(schema,indent=2),encoding='utf-8')
j=ctypes.WinDLL('E:/SEGGER/JLink_V984/JLink_x64.dll')
j.JLINKARM_ExecCommand.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int];j.JLINKARM_ExecCommand.restype=ctypes.c_int
j.JLINKARM_Open.restype=ctypes.c_char_p;j.JLINKARM_Connect.restype=ctypes.c_int
j.JLINKARM_EMU_SelectByUSBSN.argtypes=[ctypes.c_uint32];j.JLINKARM_EMU_SelectByUSBSN.restype=ctypes.c_int
j.JLINKARM_TIF_Select.argtypes=[ctypes.c_int];j.JLINKARM_SetSpeed.argtypes=[ctypes.c_uint32]
j.JLINKARM_ReadMemEx.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32];j.JLINKARM_ReadMemEx.restype=ctypes.c_int
def cmd(value):
    assert value in ('DisableAutoUpdateFW','SuppressGUI 1','InhibitConnectRetries = 1','ForceAttachTarget = 1','Device = Cortex-M3')
    out=ctypes.create_string_buffer(1024)
    if j.JLINKARM_ExecCommand(value.encode(),out,len(out))<0:raise RuntimeError('Connection configuration failed: '+value)
def read(address,length):
    buf=(ctypes.c_uint8*length)();n=j.JLINKARM_ReadMemEx(address,length,buf,0)
    if n!=length:raise RuntimeError(f'Read failed at {address:#x}: {n}')
    return bytes(buf)
result=dict(started_at=stamp.isoformat(),mode='attach-only SWD, no halt/reset/go/target writes',probe_serial=59406895)
opened=False
try:
    for value in ('DisableAutoUpdateFW','SuppressGUI 1','InhibitConnectRetries = 1','ForceAttachTarget = 1'):cmd(value)
    if j.JLINKARM_EMU_SelectByUSBSN(59406895)<0:raise RuntimeError('J-Link unavailable')
    error=j.JLINKARM_Open()
    if error:raise RuntimeError(error.decode(errors='replace'))
    opened=True;cmd('Device = Cortex-M3');j.JLINKARM_TIF_Select(1);j.JLINKARM_SetSpeed(1000)
    if j.JLINKARM_Connect()<0:raise RuntimeError('SWD attach failed')
    result['dhcsr_before']=hex(int.from_bytes(read(0xE000EDF0,4),'little'))
    result['uid_raw_hex']=read(0x1FFFF7E8,12).hex()
    image=read(0x08008000,schema['app_bytes']);result['app_sha256']=hashlib.sha256(image).hexdigest()
    result['version_markers']=[m.decode(errors='replace') for m in re.findall(rb'PATROL_VEHICLE_CHASSIS_FW_VERSION=[ -~]+',image)]
    result['image_matches_exact_elf']=result['app_sha256']==schema['app_sha256']
    if not result['image_matches_exact_elf']:raise RuntimeError('Image differs; no RAM ABI interpretation')
    samples=[]
    for _ in range(3):
        values={}
        for name,f in fields.items():
            data=read(f['address'],f['bytes'])
            if name=='last_fault':
                (folder/f'last-fault-{len(samples)}.bin').write_bytes(data)
                names='magic sequence at_ms source reason gates target_rpm target_mdeg left_rpm right_rpm driver_raw encoder_raw steer_sw drive_sw left_code right_code left_code_age right_code_age steer_age drive_age bench_phase bench_step bench_fault drive_phase drive_step drive_fault last_abort encoder_error center_phase reserved0 reserved1 checksum'.split()
                values[name]={n:v for n,v in zip(names,struct.unpack('<6I5i21I',data))}
            else:values[name]=int.from_bytes(data,'little',signed=f['signed'])
        samples.append(values)
    result['samples']=samples
    result['dhcsr_after']=hex(int.from_bytes(read(0xE000EDF0,4),'little'))
except Exception as exc:result['error']=type(exc).__name__+': '+str(exc)
finally:
    if opened:j.JLINKARM_Close()
    (folder/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    (folder/'steering-fault-current-readonly.py').write_bytes(Path(__file__).read_bytes())
    entries=[dict(archive_path=str(p.relative_to(root)),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in folder.iterdir() if p.is_file()]
    (folder/'index.json').write_text(json.dumps(dict(product='patrol_vehicle_chassis',files=entries),indent=2),encoding='utf-8')
    (root/'out/logs/steering-fault-current-latest.json').write_text(json.dumps(dict(directory=str(folder),error=result.get('error')),indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='samples'},ensure_ascii=False,indent=2))
    if result.get('samples'):print(json.dumps(result['samples'][0],ensure_ascii=False,indent=2))
    print('EVIDENCE',folder)
if 'error' in result:raise SystemExit(1)

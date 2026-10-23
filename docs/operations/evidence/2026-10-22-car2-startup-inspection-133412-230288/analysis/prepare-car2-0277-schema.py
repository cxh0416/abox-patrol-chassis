"""Generate exact-image ARM ABI offsets offline, never access hardware."""
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess

root = Path.cwd()
out = root / 'out/logs'
archive = root / 'docs/operations/evidence/2026-10-22-write-ack-verify-103353'
m = json.loads((root/'WriteAckVerify_0_2_77_Flash/candidate.manifest.json').read_text(encoding='utf-8'))
for name, digest in m['source_sha256'].items():
    assert hashlib.sha256((root/name).read_bytes()).hexdigest() == digest, name
elf = archive / 'Patrol_Vehicle_Chassis_App.elf'
mapping = (archive/'Patrol_Vehicle_Chassis_App.map').read_text(encoding='utf-8')
symbols = {}
for match in re.finditer(r'^\s+\.bss\.(\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)', mapping, re.M):
    name, address, size, owner = match.groups()
    symbols.setdefault(name, []).append((int(address,16),int(size,16),owner))
nm = subprocess.check_output(['E:/Tools/arm-gcc-14.2/bin/arm-none-eabi-nm.exe','-S',str(elf)],text=True)
for line in nm.splitlines():
    parts=line.split()
    if len(parts)==4 and parts[2] in ('b','B','d','D') and parts[3] not in symbols:
        symbols[parts[3]]=[(int(parts[0],16),int(parts[1],16),'nm')]
types={'g_control':'ChassisControl','g_motion':'PatrolMotion','fault_stop':'PatrolFaultStop',
       'last_fault':'MotionFaultRecord','hcan1':'CAN_HandleTypeDef','g_patrol_config':'PatrolRemoteConfigV5_t'}
fields=json.loads((out/'fault-stop-0276-schema.json').read_text(encoding='utf-8'))['fields']
items=[(name,f['expression'],f['bytes'],f['signed']) for name,f in fields.items()]
for name,expr in [
 ('steering_verify','g_motion.bench.verify'),('steering_pp_ack_low','g_motion.bench.pp_ack_low'),
 ('drive_verify','g_motion.drive.verify'),('config_calibrated','g_patrol_config.calibrated'),
 ('config_steering_center','g_patrol_config.steering_center_count'),
 ('config_absolute_center','g_patrol_config.absolute_center_count'),
 ('config_steering_scale','g_patrol_config.steering_counts_per_degree_milli'),
 ('config_absolute_scale','g_patrol_config.absolute_counts_per_degree_milli'),
 ('config_steering_direction','g_patrol_config.steering_direction'),
 ('config_absolute_direction','g_patrol_config.absolute_direction'),
 ('config_tolerance_mdeg','g_patrol_config.steering_tolerance_mdeg'),
 ('feedback_calibrated','g_control.feedback.calibrated'),
 ('feedback_steering_online','g_control.feedback.steering_online'),
 ('feedback_drive_online','g_control.feedback.drive_online'),
 ('feedback_can_bus_off','g_control.feedback.can_bus_off'),
 ('feedback_drive_fault','g_control.feedback.drive_fault'),
 ('feedback_ota_busy','g_control.feedback.ota_busy'),
 ('feedback_power_ready','g_control.feedback.power_ready'),
 ('feedback_encoder_range_valid','g_control.feedback.encoder_range_valid'),
 ('feedback_encoder_profile_ready','g_control.feedback.encoder_profile_ready'),
 ('feedback_steering_mdeg','g_control.feedback.steering_actual_mdeg'),
 ('feedback_encoder_mdeg','g_control.feedback.encoder_actual_mdeg'),
 ('feedback_ms','g_control.feedback.feedback_ms'),
 ('encoder_ms','g_motion.diagnostics.encoder_ms'),
 ('encoder_count','g_motion.diagnostics.encoder_count'),
 ('encoder_seen','g_motion.diagnostics.encoder_seen'),
 ('encoder_config_valid','g_motion.diagnostics.encoder_config_valid'),
 ('encoder_timeouts','g_motion.diagnostics.encoder_timeout_count'),
 ('encoder_crc_errors','g_motion.diagnostics.encoder_crc_error_count'),
 ('steering_position_ms','g_motion.diagnostics.steering_position_ms'),
 ('steering_position_count','g_motion.diagnostics.steering_position_count'),
 ('steering_heartbeat_ms','g_motion.diagnostics.steering_heartbeat_ms'),
 ('drive_heartbeat_ms','g_motion.diagnostics.drive_heartbeat_ms'),
 ('steering_heartbeat_seen','g_motion.diagnostics.steering_heartbeat_seen'),
 ('drive_heartbeat_seen','g_motion.diagnostics.drive_heartbeat_seen'),
 ('steering_heartbeat_state','g_motion.diagnostics.steering_heartbeat_state'),
 ('drive_heartbeat_state','g_motion.diagnostics.drive_heartbeat_state'),
 ('left_fault','g_motion.diagnostics.drive_objects[0]'),
 ('right_fault','g_motion.diagnostics.drive_objects[1]'),
 ('left_fault_ms','g_motion.diagnostics.drive_code_ms[0]'),
 ('right_fault_ms','g_motion.diagnostics.drive_code_ms[1]'),
 ('center_cal_phase','g_motion.center_cal.phase'),
 ('coordinate_valid','g_motion.coordinate.valid'),
 ('coordinate_runtime_center','g_motion.coordinate.runtime_center'),
 ('t12_switch_seen','switch_seen'),('t12_last_switch_ms','last_switch_ms'),
 ('t12_last_enable','last_enable'),('t12_armed','armed')]:
    items.append((name,expr,0,'center' in name or name.endswith('mdeg') or 'direction' in name or name in ('left_fault','right_fault','steering_position_count')))
rows=[]; bases=[]
for name,expr,default_size,signed in items:
    rootname=re.split(r'[.\[]',expr)[0]
    matches=symbols[rootname]
    if rootname in ('switch_seen','last_switch_ms','last_enable','armed'):
        matches=[x for x in matches if 'patrol_phy_remote.c.obj' in x[2]]
    assert len(matches)==1,(rootname,matches)
    address,size,owner=matches[0];bases.append(address)
    if '.' in expr:
        suffix=expr.split('.',1)[1];ctype=types[rootname]
        offset=f'offsetof({ctype}, {suffix})';width=f'sizeof((({ctype}*)0)->{suffix})'
    elif '[' in expr:
        offset=str(int(re.search(r'\[(\d+)\]',expr)[1])*4);width='4'
    else:
        offset='0';width=str(default_size or size)
    rows.append('{'+offset+','+width+'}')
c='#include <stddef.h>\n#include <stdint.h>\n#include "motion_internal.h"\n#include "remote_service.h"\n#include "patrol_fault_stop.h"\n#include "patrol_config.h"\n#include "can.h"\n'
for symbol,ctype in types.items():
    c+=f'_Static_assert(sizeof({ctype})=={symbols[symbol][0][1]}U,"ELF size {symbol}");\n'
c+='const uint32_t schema[][2] __attribute__((used,section(".schema")))={'+','.join(rows)+'};\n'
source=out/'car2-0277-abi.c';obj=out/'car2-0277-abi.o';binary=out/'car2-0277-abi.bin'
source.write_text(c,encoding='utf-8')
commands=json.loads((root/'app/build/FaultStopRelease/compile_commands.json').read_text(encoding='utf-8'))
command=next(x for x in commands if x['file'].endswith('patrol_runtime.c'))
args=command['command'].split()
args=[arg.replace(chr(92),'/') if arg.startswith('-I') or arg.endswith('.exe') or arg.endswith('.c') else arg for arg in args]
args=[arg for arg in args if not arg.startswith('-DPATROL_APP_FW_VERSION=')]
flags=[];skip=False
for arg in args[1:]:
    if skip:skip=False;continue
    if arg=='-o':skip=True;continue
    if arg=='-c' or arg.replace('\\','/')==command['file']:continue
    flags.append(arg)
compiled=subprocess.run([args[0],*flags,'-c',str(source),'-o',str(obj)],capture_output=True,text=True)
(out/'car2-0277-abi.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
assert compiled.returncode==0,compiled.stderr
subprocess.run(['E:/Tools/arm-gcc-14.2/bin/arm-none-eabi-objcopy.exe','-O','binary','--only-section=.schema',str(obj),str(binary)],check=True)
values=list(struct.iter_unpack('<II',binary.read_bytes()));assert len(values)==len(items)
newfields={}
for (name,expr,_,signed),base,(offset,size) in zip(items,bases,values):
    newfields[name]={'address':base+offset,'address_hex':hex(base+offset),'bytes':size,'expression':expr,'signed':bool(signed)}
schema={'product':m['product'],'version':m['version'],'app_address':0x08008000,'app_bytes':m['size'],'app_sha256':m['sha256'],
        'elf':str(elf),'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),
        'source':'Exact ELF map and ARM offsetof/sizeof from matching source and actual compile flags; no hardware access.',
        'fields':newfields}
schema_path=out/'car2-0277-schema.json';schema_path.write_text(json.dumps(schema,indent=2),encoding='utf-8')
reader=(out/'fault-stop-0276-readonly.py').read_text(encoding='utf-8')
reader=reader.replace('0.2.76','0.2.77').replace('fault-stop-0276-schema.json','car2-0277-schema.json')
reader=reader.replace('fault-stop-readonly-latest.json','car2-startup-readonly-latest.json')
(out/'car2-0277-readonly.py').write_text(reader,encoding='utf-8')
print('Prepared',len(newfields),'fields with exact ELF sizes; g_motion',hex(symbols['g_motion'][0][1]))

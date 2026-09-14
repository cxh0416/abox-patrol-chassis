"""Check the known calibration-save stack chain, reserving 1 KiB for MQTT/HAL/IRQ context."""
import re
import sys
from pathlib import Path

build=Path(sys.argv[1])
frames={}
for path in build.rglob('*.su'):
    for line in path.read_text(errors='replace').splitlines():
        fields=line.split('\t')
        if len(fields)>=3:
            if not Path(fields[0].rsplit(':',3)[0]).is_file():
                continue
            name=fields[0].rsplit(':',1)[-1].split('.')[0]
            frames[name]=max(frames.get(name,0),int(fields[1]))
chain=['Vcu_RemoteTask','CloudService_Task','CloudCommands_OnMessage','handle_config_request','VcuConfig_SetCalibration',
       'VcuConfig_Save','OTA_Info_SaveRemoteConfig','ota_info_write_raw']
for name in chain:
    if name not in frames:raise SystemExit('Missing stack evidence: '+name)
used=sum(frames[n] for n in chain)
source=(Path(__file__).resolve().parents[1]/'app/Core/User/system/src/vcu_rtos.c').read_text()
capacity=int(re.search(r'#define VCU_REMOTE_STACK_WORDS (\d+)U',source)[1])*4
assert used+1024<=capacity, (used,capacity)
print(f'PASS calibration stack: known frames {used} + 1024 byte reserve <= {capacity}')
fault_chain=['Vcu_CanopenTask','PatrolRuntime_Tick','PatrolRuntime_ApplyReset',
             'CenterCalibration_Fail','AppLog_Printf']
for name in fault_chain:
    if name not in frames:raise SystemExit('Missing fault stack evidence: '+name)
fault_used=sum(frames[n] for n in fault_chain)
fault_capacity=int(re.search(r'#define VCU_CANOPEN_STACK_WORDS (\d+)U',source)[1])*4
assert fault_used+512<=fault_capacity, (fault_used,fault_capacity)
print(f'PASS CANopen fault stack: known frames {fault_used} + 512 byte library reserve <= {fault_capacity}')

# Calibration commit executes from Remote after CloudService_Task has returned.
center_chain=['Vcu_RemoteTask','CenterCalibration_Tick','VcuConfig_SetCalibration',
              'VcuConfig_Save','OTA_Info_SaveRemoteConfig','ota_info_write_raw']
for name in center_chain:
    if name not in frames:raise SystemExit('Missing center stack evidence: '+name)
center_used=sum(frames[n] for n in center_chain)
assert center_used+1024<=capacity, (center_used,capacity)
print(f'PASS center commit stack: known frames {center_used} + 1024 byte reserve <= {capacity}')

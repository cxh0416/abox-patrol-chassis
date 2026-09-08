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
            name=fields[0].rsplit(':',1)[-1].split('.')[0]
            frames[name]=max(frames.get(name,0),int(fields[1]))
chain=['CloudService_Task','cloud_on_message','handle_config_request','VcuConfig_SetCalibration',
       'VcuConfig_Save','OTA_Info_SaveRemoteConfig','ota_info_write_raw']
for name in chain:
    if name not in frames:raise SystemExit('Missing stack evidence: '+name)
used=sum(frames[n] for n in chain)
source=(Path(__file__).resolve().parents[1]/'app/Core/User/Src/vcu_rtos.c').read_text()
capacity=int(re.search(r'#define VCU_REMOTE_STACK_WORDS (\d+)U',source)[1])*4
assert used+1024<=capacity, (used,capacity)
print(f'PASS calibration stack: known frames {used} + 1024 byte reserve <= {capacity}')

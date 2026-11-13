"""Check the encoder task's bounded serial and diagnostic call chains."""
import re
import sys
from pathlib import Path

build = Path(sys.argv[1])
frames = {}
for path in build.rglob('*.su'):
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        fields = line.split('\t')
        if len(fields) < 3:
            continue
        name = fields[0].rsplit(':', 1)[-1].split('.')[0]
        frames[name] = max(frames.get(name, 0), int(fields[1]))

def chain(*names):
    missing = [name for name in names if name not in frames]
    if missing:
        raise SystemExit('Missing encoder stack evidence: ' + ', '.join(missing))
    return sum(frames[name] for name in names)

prefix = ('Patrol_EncoderTask', 'ABoxSerialTransaction_Begin')
if 'HAL_UART_Transmit' in frames:
    send = chain(*prefix, 'ABoxSerialPort_SendBlocking', 'HAL_UART_Transmit', 'UART_WaitOnFlagUntilTimeout')
    arm = chain(*prefix, 'ABoxSerialPort_StartRx', 'HAL_UARTEx_ReceiveToIdle_IT')
else:
    send = chain(*prefix, 'ABoxSerialPort_SendBlocking', 'ABoxGdMonotonic_GetMs')
    arm = chain(*prefix, 'ABoxSerialPort_StartRx')
snapshot = chain('Patrol_EncoderTask', 'PatrolRuntime_BeginEncoderCycle', 'PatrolDiagnostics_Snapshot')
take = chain('Patrol_EncoderTask', 'ABoxSerialTransaction_Take')
used = max(send, arm, snapshot, take)
source = (Path(__file__).resolve().parents[1] / 'app/Core/User/system/src/patrol_rtos.c').read_text(encoding='utf-8')
capacity = int(re.search(r'#define PATROL_ENCODER_STACK_WORDS (\d+)U', source)[1]) * 4
reserve = 256
if used + reserve > capacity:
    raise SystemExit(f'FAIL encoder stack: known {used} + reserve {reserve} > {capacity}')
print(f'PASS encoder stack: known {used} + reserve {reserve} <= {capacity}')

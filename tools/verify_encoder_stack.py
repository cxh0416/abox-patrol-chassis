"""Check the encoder task's bounded serial and diagnostic call chains."""
import re
import sys
from pathlib import Path

build = Path(sys.argv[1])
frames = {}
for path in build.rglob('*.su'):
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        fields = line.split('\t')
        if len(fields) < 3 or not Path(fields[0].rsplit(':', 3)[0]).is_file():
            continue
        name = fields[0].rsplit(':', 1)[-1].split('.')[0]
        frames[name] = max(frames.get(name, 0), int(fields[1]))

def chain(*names):
    missing = [name for name in names if name not in frames]
    if missing:
        raise SystemExit('Missing encoder stack evidence: ' + ', '.join(missing))
    return sum(frames[name] for name in names)

prefix = ('Patrol_EncoderTask', 'ABoxSerialTransaction_Begin')
cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
target = re.search(r'^ABOX_HARDWARE:[^=]+=(.+)$', cache, re.MULTILINE)
if not target:
    raise SystemExit('Missing ABOX_HARDWARE in build cache')
if target[1] == 'gd32f105_ec800_v1':
    send = chain(*prefix, 'ABoxUart_SendBlocking', 'tick', 'ABoxGdMonotonic_GetMs')
    irq_name = 'UART4_IRQHandler'
elif target[1] == 'stm32f105_ec800_v1':
    send = chain(*prefix, 'ABoxUart_SendBlocking', 'get_tick', 'HAL_GetTick', 'ABoxStm32Monotonic_GetMs')
    irq_name = 'UART5_IRQHandler'
else:
    raise SystemExit('Unsupported ABOX_HARDWARE: ' + target[1])
arm = chain(*prefix, 'ABoxUart_StartRx', 'ABoxDmaResource_Claim', 'ABoxCritical_Enter')
abort = chain(*prefix, 'ABoxUart_AbortRx', 'stop_rx', 'ABoxDmaResource_Release', 'ABoxCritical_Enter')
init = chain('Patrol_EncoderTask', 'ABoxSerialTransaction_Init', 'ABoxUart_SetEvents', 'ABoxCritical_Enter')
snapshot = chain('Patrol_EncoderTask', 'PatrolRuntime_BeginEncoderCycle', 'PatrolDiagnostics_Snapshot')
take = chain('Patrol_EncoderTask', 'ABoxSerialTransaction_Take')
used = max(send, arm, abort, init, snapshot, take)
source = (Path(__file__).resolve().parents[1] / 'app/Core/User/system/src/patrol_rtos.c').read_text(encoding='utf-8')
capacity = int(re.search(r'#define PATROL_ENCODER_STACK_WORDS (\d+)U', source)[1]) * 4
reserve = 256
if used + reserve > capacity:
    raise SystemExit(f'FAIL encoder stack: known {used} + reserve {reserve} > {capacity}')
print(f'PASS encoder stack: known {used} + reserve {reserve} <= {capacity}')

# The serial receive callback executes in handler mode on the main stack, not
# the encoder task stack. The linked startup selects the chip-specific vector.
irq_used = chain(irq_name, 'ABoxUart_Irq', 'finish_rx', 'emit_rx', 'on_rx',
                 'ABoxUart_StartRx', 'ABoxDmaResource_Claim', 'ABoxCritical_Enter')
linker = (Path(__file__).resolve().parents[1] / 'app/STM32F105XX_FLASH.ld').read_text(encoding='utf-8')
irq_capacity = int(re.search(r'_Min_Stack_Size\s*=\s*(0x[0-9a-fA-F]+)', linker)[1], 16)
irq_reserve = 512
if irq_used + irq_reserve > irq_capacity:
    raise SystemExit(f'FAIL encoder UART IRQ stack: known {irq_used} + reserve {irq_reserve} > {irq_capacity}')
print(f'PASS encoder UART IRQ stack: known {irq_used} + reserve {irq_reserve} <= {irq_capacity}')

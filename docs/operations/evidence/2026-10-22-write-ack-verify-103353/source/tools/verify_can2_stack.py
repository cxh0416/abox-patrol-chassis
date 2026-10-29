"""Bound the CAN2 command and safety owner stacks using ARM .su evidence."""
import re
import sys
from pathlib import Path

build = Path(sys.argv[1])
frames = {}
for path in build.rglob('*.su'):
    for line in path.read_text(errors='replace').splitlines():
        parts = line.split('\t')
        if len(parts) < 3 or not Path(parts[0].rsplit(':', 3)[0]).is_file():
            continue
        name = parts[0].rsplit(':', 1)[-1].split('.')[0]
        frames[name] = max(frames.get(name, 0), int(parts[1]))
source = (Path(__file__).resolve().parents[1] / 'app/Core/User/system/src/patrol_rtos.c').read_text()
rx_capacity = int(re.search(r'#define PATROL_CAN_RX_STACK_WORDS (\d+)U', source)[1]) * 4
safety_capacity = int(re.search(r'#define PATROL_SAFETY_STACK_WORDS (\d+)U', source)[1]) * 4
canopen_capacity = int(re.search(r'#define PATROL_CANOPEN_STACK_WORDS (\d+)U', source)[1]) * 4
chains = {
    'CAN2 target': (rx_capacity, ['Patrol_CanRxTask', 'PatrolCanRemote_OnFrame',
                                'RemoteService_ApplyLocal', 'ChassisControl_Apply']),
    'CAN2 reset': (rx_capacity, ['Patrol_CanRxTask', 'PatrolCanRemote_OnFrame',
                               'PatrolRuntime_ResetFault', 'RemoteService_Init', 'AppLog_Printf']),
    'CAN2 reply': (rx_capacity, ['Patrol_CanRxTask', 'PatrolCanRemote_TakeReply',
                               'ChassisControl_SafetyReady', 'ChassisControl_SafetyReason',
                               'ChassisControl_RuntimeSafetyReason']),
    'CAN2 lease stop': (safety_capacity, ['Patrol_SafetyControlTask', 'PatrolCanRemote_Tick',
                                        'RemoteService_ApplyLocal', 'ChassisControl_Apply']),
    'handset A switch': (rx_capacity, ['Patrol_CanRxTask', 'PatrolPhyRemote_OnFrame',
                                     'RemoteService_ApplyLocal', 'ChassisControl_Apply']),
    'handset joystick': (rx_capacity, ['Patrol_CanRxTask', 'PatrolPhyRemote_OnFrame',
                                     'RemoteService_ApplyLocal', 'ChassisControl_Apply']),
    'handset lease stop': (safety_capacity, ['Patrol_SafetyControlTask', 'PatrolPhyRemote_Tick',
                                           'RemoteService_ApplyLocal', 'ChassisControl_Apply']),
    'fault capture from CAN2': (rx_capacity, ['Patrol_CanRxTask', 'PatrolPhyRemote_OnFrame',
        'RemoteService_ApplyLocal', 'ChassisControl_Apply', 'force_stop',
        'ChassisControl_RequestFault', 'PatrolRuntime_RequestFaultStop', 'PatrolRuntime_CaptureFault']),
    'fault frame from CAN2': (rx_capacity, ['Patrol_CanRxTask', 'PatrolPhyRemote_OnFrame',
        'RemoteService_ApplyLocal', 'ChassisControl_Apply', 'force_stop',
        'ChassisControl_RequestFault', 'PatrolRuntime_RequestFaultStop', 'dispatch_fault_stop', 'PatrolFaultStop_Take']),
    'fault transmit from CAN2': (rx_capacity, ['Patrol_CanRxTask', 'PatrolPhyRemote_OnFrame',
        'RemoteService_ApplyLocal', 'ChassisControl_Apply', 'force_stop',
        'ChassisControl_RequestFault', 'PatrolRuntime_RequestFaultStop', 'dispatch_fault_stop',
        'PatrolRuntimePort_SendCan', 'HAL_CAN_AddTxMessage']),
    'fault frame from safety': (safety_capacity, ['Patrol_SafetyControlTask', 'RemoteService_SafetyTick',
        'ChassisControl_Tick', 'force_stop', 'ChassisControl_RequestFault',
        'PatrolRuntime_RequestFaultStop', 'dispatch_fault_stop', 'PatrolFaultStop_Take']),
    'fault latch log': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
        'service_fault_stop', 'AppLog_Printf']),
    'fault stop retry': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
        'service_fault_stop', 'dispatch_fault_stop', 'PatrolFaultStop_Take']),
    'diagnostic timeout log': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
        'PatrolRuntime_Take', 'AppLog_Printf']),
    'write verification log': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick', 'AppLog_Printf']),
    'steering verification read': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
        'PatrolRuntime_Take', 'BenchSteering_Take', 'PatrolCanopen_SdoRead']),
    'drive verification read': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
        'PatrolRuntime_Take', 'BenchDrive_Take', 'PatrolCanopen_SdoRead']),
    'steering verification reply': (rx_capacity, ['Patrol_CanRxTask', 'PatrolRuntime_OnCanFrame',
        'BenchSteering_OnFrame', 'PatrolCanopen_ParseSdoValue']),
    'drive verification reply': (rx_capacity, ['Patrol_CanRxTask', 'PatrolRuntime_OnCanFrame',
        'BenchDrive_OnFrame', 'PatrolCanopen_ParseSdoValue']),
}
for label, (capacity, chain) in chains.items():
    missing = [name for name in chain if name not in frames]
    if missing:
        raise SystemExit('Missing CAN2 stack evidence: ' + ', '.join(missing))
    used = sum(frames[name] for name in chain)
    # Covers non-inlined small helpers, C library calls, HAL and interrupt entry.
    if used + 512 > capacity:
        raise SystemExit(f'{label}: known {used} + reserve 512 exceeds {capacity}')
    print(f'PASS {label}: known {used} + reserve 512 <= {capacity}')

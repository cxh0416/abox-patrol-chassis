"""Bound the CAN2 command and safety owner stacks using ARM .su evidence."""
import re
import sys
from pathlib import Path

build = Path(sys.argv[1])
frames = {}
for path in build.rglob('*.su'):
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        parts = line.split('\t')
        if len(parts) < 3 or not Path(parts[0].rsplit(':', 3)[0]).is_file():
            continue
        name = parts[0].rsplit(':', 1)[-1].split('.')[0]
        frames[name] = max(frames.get(name, 0), int(parts[1]))
source = (Path(__file__).resolve().parents[1] / 'app/Core/User/system/src/patrol_rtos.c').read_text(encoding='utf-8')
rx_capacity = int(re.search(r'#define PATROL_CAN_RX_STACK_WORDS (\d+)U', source)[1]) * 4
safety_capacity = int(re.search(r'#define PATROL_SAFETY_STACK_WORDS (\d+)U', source)[1]) * 4
canopen_capacity = int(re.search(r'#define PATROL_CANOPEN_STACK_WORDS (\d+)U', source)[1]) * 4
chains = {
    'drive angle selection': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
        'BenchDrive_SetSteeringAngle']),
    'drive differential selection': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
        'BenchDrive_Tick']),
    'centering boundary fault capture': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
        'RemoteService_SafetyFault', 'ChassisControl_RequestFault',
        'PatrolRuntime_RequestFaultStop', 'PatrolRuntime_CaptureFault']),
    'centering boundary fault transmit': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
        'RemoteService_SafetyFault', 'ChassisControl_RequestFault', 'PatrolRuntime_RequestFaultStop',
        'dispatch_fault_stop', 'PatrolRuntimePort_SendCan', 'ABoxCanEvents_Send', 'consume', 'patrol_can_complete']),
    'centering boundary stop log': (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
        'RemoteService_SafetyFault', 'ChassisControl_RequestFault', 'PatrolRuntime_RequestFaultStop',
        'BenchSteering_Stop', 'AppLog_Printf']),
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
        'PatrolRuntimePort_SendCan', 'ABoxCanEvents_Send', 'consume', 'patrol_can_complete']),
    'fault frame from safety': (safety_capacity, ['Patrol_SafetyControlTask', 'RemoteService_SafetyTick',
        'ChassisControl_Tick', 'force_stop', 'ChassisControl_RequestFault',
        'PatrolRuntime_RequestFaultStop', 'dispatch_fault_stop', 'PatrolFaultStop_Take']),
    'fault transmit from safety': (safety_capacity, ['Patrol_SafetyControlTask', 'RemoteService_SafetyTick',
        'ChassisControl_Tick', 'force_stop', 'ChassisControl_RequestFault', 'PatrolRuntime_RequestFaultStop',
        'dispatch_fault_stop', 'PatrolRuntimePort_SendCan', 'ABoxCanEvents_Send', 'consume', 'patrol_can_complete']),
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
# GCC may inline the static calibration dispatcher into OnFrame; its stack is
# then already included in OnFrame's .su frame. Count it separately if emitted.
calibration_chain = ['calibration_command'] if 'calibration_command' in frames else []
chains['CAN2 maintenance idle check'] = (rx_capacity, ['Patrol_CanRxTask', 'PatrolCanRemote_OnFrame',
    *calibration_chain, 'PatrolRuntime_MaintenanceReady'])
chains['CAN2 maintenance ownership'] = (rx_capacity, ['Patrol_CanRxTask', 'PatrolCanRemote_OnFrame',
    *calibration_chain, 'RemoteService_SetMaintenance', 'RemoteService_Revoke'])
chains['collision input sampling'] = (safety_capacity, ['Patrol_SafetyControlTask',
    'RemoteService_CollisionInputs', 'ChassisControl_CollisionInputs'])
chains['collision neutral observation'] = (rx_capacity, ['Patrol_CanRxTask', 'PatrolPhyRemote_OnFrame',
    'RemoteService_CollisionJoystick', 'ChassisControl_CollisionJoystick'])
chains['collision enabled drive hold'] = (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
    'BenchDrive_CollisionHold'])
chains['collision steering supersession'] = (canopen_capacity, ['Patrol_CanopenTask', 'PatrolRuntime_Tick',
    'BenchSteering_CollisionHold'])
linker = (Path(__file__).resolve().parents[1] / 'app/STM32F105XX_FLASH.ld').read_text(encoding='utf-8')
irq_capacity = int(re.search(r'_Min_Stack_Size\s*=\s*(0x[0-9a-fA-F]+)', linker)[1], 16)
if 'patrol_can_irq' in frames:
    irq_prefix = ['CAN1_RX0_IRQHandler', 'patrol_can_irq']
    receive_tail = ['receive', 'ABoxCanPort_Receive', 'HAL_CAN_GetRxMessage']
    init_tail = ['ABoxCanPort_Init', 'HAL_CAN_Init', 'HAL_CAN_MspInit']
else:
    irq_prefix = ['CAN0_RX0_IRQHandler', 'can_irq']
    receive_tail = ['receive', 'ABoxCanPort_Receive', 'can_message_receive']
    init_tail = ['ABoxCanPort_Init', 'can_init']
chains['CAN start'] = (rx_capacity, ['Patrol_CanRxTask'] + init_tail)
chains['CAN bind'] = (rx_capacity, ['Patrol_CanRxTask', 'ABoxCanPort_BindEvents', 'ABoxCanEvents_Init'])
irq_common = irq_prefix + ['ABoxCanPort_DispatchEventIrq', 'ABoxCanEvents_Irq']
chains['CAN IRQ queue'] = (irq_capacity, irq_common + ['patrol_can_rx', 'xQueueGenericSendFromISR', 'prvCopyDataToQueue'])
chains['CAN IRQ receive'] = (irq_capacity, irq_common + receive_tail)
chains['CAN IRQ error'] = (irq_capacity, irq_common + ['patrol_can_error'])
for label, (capacity, chain) in chains.items():
    missing = [name for name in chain if name not in frames]
    if missing:
        raise SystemExit('Missing CAN2 stack evidence: ' + ', '.join(missing))
    used = sum(frames[name] for name in chain)
    # Covers non-inlined small helpers, C library calls, HAL and interrupt entry.
    if used + 512 > capacity:
        raise SystemExit(f'{label}: known {used} + reserve 512 exceeds {capacity}')
    print(f'PASS {label}: known {used} + reserve 512 <= {capacity}')

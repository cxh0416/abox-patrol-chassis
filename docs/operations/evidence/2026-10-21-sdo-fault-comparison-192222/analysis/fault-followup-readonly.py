"""Attach without halt/reset or probe update; preserve each read in a fresh directory."""
import ctypes
import json
import struct
from pathlib import Path
from datetime import datetime

stamp = datetime.now().strftime('%Y-%m-%d-fault-followup-%H%M%S-%f')
private = Path('out/flash')/stamp
public = Path('docs/operations/evidence')/stamp
private.mkdir(parents=True, exist_ok=False)
public.mkdir(parents=True, exist_ok=False)
source = Path('out/logs/steering-live-readonly.py').read_text()
start = source.index("j=ctypes.WinDLL")
end = source.index("command('DisableAutoUpdateFW')")
exec(source[start:end])
for cmd in ('DisableAutoUpdateFW', 'SuppressGUI 1', 'InhibitConnectRetries = 1', 'ForceAttachTarget = 1'):
    command(cmd)
err=j.JLINKARM_Open()
if err: raise RuntimeError(err.decode(errors='replace'))
try:
    command('Device = Cortex-M3')
    j.JLINKARM_TIF_Select(1);j.JLINKARM_SetSpeed(1000)
    if j.JLINKARM_Connect()<0:raise RuntimeError('connect failed')
    before=read(0xE000EDF0,4)
    ram=read(0x20000000,0x10000)
    (private/'ram.bin').write_bytes(ram)
    def unpack(addr,fmt='I'):
        return struct.unpack_from('<'+fmt,ram,addr-0x20000000)[0]
    b=0x20008540+224;d=0x20008540+308;c=0x20008540+364
    result=dict(tick_ms=unpack(0x2000021C),power_command=unpack(0x20008438,'B'),
        coordinate_valid=unpack(c,'B'), runtime_center=unpack(c+20,'i'),
        bench_center=unpack(b+36,'i'), bench_min=unpack(b+28,'i'),bench_max=unpack(b+32,'i'),
        bench_phase=unpack(b,'B'),bench_step=unpack(b+1,'B'),bench_fault=unpack(b+24),bench_sent_ms=unpack(b+12),
        bench_index=hex(unpack(b+10,'H')),bench_expected=unpack(b+52,'i'),
        drive_phase=unpack(d,'B'),drive_step=unpack(d+1,'B'),drive_fault=unpack(d+32),
        fault_latched=unpack(0x200080F0+70,'B'),session=unpack(0x200080F0+66,'B'),enable=unpack(0x200080F0+69,'B'),
        stop_reason=unpack(0x200080F0+73,'B'),can1_rx=unpack(0x200087B4),can2_rx=unpack(0x200087B8),
        rx_overflow=unpack(0x200087BC),can1_hal_error=hex(unpack(0x20000FB0+36)),
        busoff_mask=unpack(0x200087B1,'B'),busoff_seen=unpack(0x200087B0,'B'),
        fault_sequence=unpack(0x200084C0+4), fault_at_ms=unpack(0x200084C0+8))
    (public/'motion-fault.bin').write_bytes(ram[0x84C0:0x8540])
    for name,address,size in [('GPIOA',0x40010800,20),('CAN1',0x40006400,32),('CAN2',0x40006800,32)]:
        (public/(name+'-registers.bin')).write_bytes(read(address,size))
    after=read(0xE000EDF0,4)
    result.update(dhcsr_before=before.hex(),dhcsr_after=after.hex(), cpu_halted_before=bool(int.from_bytes(before,'little')&(1<<17)), cpu_halted_after=bool(int.from_bytes(after,'little')&(1<<17)))
    (public/'snapshot.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    Path('out/logs/fault-followup-latest.json').write_text(json.dumps(dict(private=str(private.resolve()),public=str(public.resolve()))))
    print(json.dumps(result,indent=2),flush=True)
    print('EVIDENCE',public,flush=True)
finally:
    j.JLINKARM_Close()

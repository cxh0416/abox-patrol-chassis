"""Verify and archive the completed passive dual-bus fault-latched input test."""
import collections
import datetime
import hashlib
import json
from pathlib import Path
import shutil

root = Path.cwd()
handle = json.loads((root / 'out/logs/dual-can-capture-current.json').read_text(encoding='utf-8-sig'))
capture = Path(handle['output'])
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
summary = json.loads((capture / 'summary.json').read_text(encoding='utf-8'))
metadata = json.loads((capture / 'metadata.json').read_text(encoding='utf-8'))
assert summary['error'] is None and summary['stop_reason'] == 'operator_stop_file'
assert metadata['hardware_listen_only'] and metadata['transmit_api_bound'] is False
assert metadata['physical_channels'] == {'CAN1': 2, 'CAN2': 1}
for name, digest in json.loads((capture / 'sha256.json').read_text(encoding='utf-8')).items():
    assert sha(capture / name) == digest, name

counts = collections.Counter()
axes, a_values, controls = [], set(), []
with (capture / 'frames.jsonl').open(encoding='utf-8') as stream:
    for line in stream:
        f = json.loads(line)
        counts[(f['bus'], f['id'])] += 1
        if f['bus'] == 'CAN1' and f['id'] in (0, 0x601, 0x602):
            controls.append(f)
        if f['bus'] == 'CAN2' and f['id'] in (0x10a, 0x10b):
            assert f['adapter_channel'] == 0 and f['dlc'] == 8
            data = bytes.fromhex(f['data'])
            if f['id'] == 0x10a:
                a_values.add(data[2])
            else:
                axes.append((f, int.from_bytes(data[4:6], 'little', signed=True),
                             int.from_bytes(data[0:2], 'little', signed=True)))
assert not controls and a_values == {2}
expected = [('forward', 450, 0), ('reverse', -450, 0), ('left', 0, -450), ('right', 0, 450)]
representatives = {}
for name, throttle, steer in expected:
    representatives[name] = next(f for f, t, s in axes if (t, s) == (throttle, steer))
assert [f['elapsed_s'] for f in representatives.values()] == sorted(f['elapsed_s'] for f in representatives.values())
assert axes[-1][1:] == (0, 0)

snapshot_dir = root / 'out/flash/2026-10-22-fault-stop-readonly-093225-012526'
snapshot = json.loads((snapshot_dir / 'snapshot.json').read_text(encoding='utf-8'))
fields = snapshot['fields']
assert snapshot['observed_app_sha256'] == '177efdb46df9a968755edcaf0007fac78768125fb2bad2a43e4ca7fa27d822b2'
assert fields['fault_latched'] == 1 and fields['t12_armed'] == 0
assert fields['target_rpm'] == fields['requested_rpm'] == fields['enable_request'] == fields['session_active'] == 0
assert fields['fault_stop_disabled_mask'] == 3 and fields['fault_stop_active'] == 0
age = (snapshot['tick_after_ms'] - fields['t12_last_switch_ms']) & 0xffffffff
assert fields['t12_last_enable'] == 2 and age < 100

analysis = dict(product='patrol_vehicle_chassis', capture=str(capture),
    duration_s=summary['elapsed_s'], frames=sum(counts.values()),
    counts={f'{bus}:0x{cob:03X}': count for (bus, cob), count in sorted(counts.items())},
    operator_report='Completed forward, reverse, left, right while original fault latch retained',
    representative_frames=representatives, final_joystick=axes[-1][0],
    a_values=sorted(a_values), can1_control_requests=len(controls),
    mcu_snapshot=str(snapshot_dir), mcu_fields=fields, switch_age_ms=age,
    conclusions=[
        'All four operator actions and return to center are present on receiver/ABOX CAN2.',
        'The exact-image MCU snapshot shows fresh parsed T12 switch input, fault_latched=1, armed=0, and no session or target.',
        'The fault-latched T12 handler rejects joystick execution before motion mapping; no new CAN1 SDO/NMT control requests were captured.',
        'This after-fault test identifies why current commands do not move the vehicle; it does not capture the original fault onset or prove there was never a transient CAN2 gap.',
    ],
    boundaries=['No enable, motion, reset, clear-latch, NMT or parameter write by this passive capture.',
                'Raw hardware timestamps are retained without assuming units or inter-channel synchronization.',
                'No inference of physical angle or stopping distance from CAN status.'])
analysis_path = root / 'out/logs/dual-can-four-actions-final.json'
analysis_path.write_text(json.dumps(analysis, ensure_ascii=False, indent=2), encoding='utf-8')

evidence = root / 'docs/operations/evidence' / datetime.datetime.now().strftime('%Y-%m-%d-dual-can-four-actions-%H%M%S-%f')
evidence.mkdir()
entries = []
def archive(src, relative):
    dst = evidence / relative
    dst.parent.mkdir(parents=True, exist_ok=True)
    assert not dst.exists()
    shutil.copyfile(src, dst)
    assert sha(src) == sha(dst)
    entries.append(dict(original_path=str(src.resolve()), archive_path=str(dst.relative_to(root)),
                        bytes=dst.stat().st_size, sha256=sha(dst)))

for src in sorted(capture.iterdir()):
    if src.is_file(): archive(src, Path('raw') / capture.name / src.name)
for key in ('stdout', 'stderr', 'stop_file'):
    src = Path(handle[key]); archive(src, Path('raw') / src.name)
for src in sorted(snapshot_dir.iterdir()):
    if src.is_file(): archive(src, Path('swd') / snapshot_dir.name / src.name)
for name in ('dual-can-capture-current.json', 'dual-can-four-actions-final.json', 'dual-can-four-actions.json',
             'dual-can-first-input-analysis.json', 'dual-can-listener-tests.log',
             'fault-stop-0276-readonly.py', 'fault-stop-0276-schema.json',
             'fault-stop-0276-t12-schema.json', 'fault-stop-0276-t12-armed-schema.json',
             'dual-can-t12-after-operation.log'):
    archive(root / 'out/logs' / name, Path('analysis') / name)
for name in ('can1_bus_monitor.py', 'test_can1_bus_monitor.py'):
    archive(Path('E:/1_work/ChassisCar_test') / name, Path('analysis') / name)
for src in (root / 'app/Core/User/motion/src/patrol_phy_remote.c',
            root / 'docs/operations/evidence/2026-10-22-steering-timeout-200ms-084214/Patrol_Vehicle_Chassis_App.map'):
    archive(src, Path('source-at-analysis') / src.name)
archive(Path(__file__), Path('analysis') / Path(__file__).name)
index = dict(product='patrol_vehicle_chassis', created_at=datetime.datetime.now().astimezone().isoformat(),
             capture_closed=True, hardware_listen_only=True, target_reset_or_halted=False,
             driver_parameters_written=False, files=entries)
(evidence / 'index.json').write_text(json.dumps(index, ensure_ascii=False, indent=2), encoding='utf-8')
for entry in entries:
    assert sha(root / entry['archive_path']) == entry['sha256']
(root / 'out/logs/dual-can-four-actions-latest.json').write_text(
    json.dumps(dict(evidence=str(evidence), analysis=str(analysis_path)), ensure_ascii=False), encoding='utf-8')
print(json.dumps(dict(evidence=str(evidence), files=len(entries), frames=analysis['frames'],
                     duration_s=analysis['duration_s'], switch_age_ms=age, can1_control_requests=len(controls)), indent=2))

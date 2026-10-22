"""Copy complete CAN evidence to a new immutable group and verify every SHA256."""
import argparse
import collections
import hashlib
import json
import shutil
from datetime import datetime
from pathlib import Path

root = Path.cwd()
external = Path(r'E:\1_work\ChassisCar_test')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--comparison', type=Path, required=True)
parser.add_argument('--post-snapshot', type=Path, required=True)
args = parser.parse_args()
dest = root/'docs/operations/evidence'/datetime.now().strftime('%Y-%m-%d-sdo-fault-comparison-%H%M%S')
dest.mkdir(exist_ok=False)
entries = []


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def archive(source, relative):
    target = dest/relative
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        raise RuntimeError('would overwrite evidence: '+str(target))
    digest = sha(source)
    shutil.copyfile(source, target)
    if sha(target) != digest or sha(source) != digest:
        raise RuntimeError('copy/source changed: '+str(source))
    entries.append(dict(original_path=str(source.resolve()), archive_path=str(target.relative_to(root)),
                        sha256=digest, bytes=target.stat().st_size))


groups = ['can1-20261021-183631-973740', 'direct-stop-20261021-184557-195697',
          'steering-readonly-20261021-184749-913680', 'steering-readonly-20261021-184919-028237',
          'steering-readonly-20261021-185810-169046']
groups += [p.name for p in sorted((external/'logs').iterdir()) if p.is_dir() and
           p.name.startswith(('steering-phase-readonly-20261021-', 'steering-nmt-diagnostic-20261021-', 'steering-diagnostic-finalverify-20261021-'))]
for group in groups:
    folder = external/'logs'/group
    if group.startswith('steering-phase') and not (folder/'results.json').exists():
        raise RuntimeError('probe not completed: '+group)
    for source in sorted(folder.rglob('*')):
        if source.is_file():
            archive(source, Path('raw')/group/source.relative_to(folder))

snapshot_groups = ['2026-10-21-fault-followup-184153-677663', '2026-10-21-fault-followup-184603-189167', args.post_snapshot.name]
for group in snapshot_groups:
    for source in sorted((root/'docs/operations/evidence'/group).iterdir()):
        if source.is_file():
            archive(source, Path('snapshots')/group/source.name)

for name in ['can1_bus_monitor.py','can1_stop_now.py','steering_sdo_readonly_probe.py',
             'steering_sdo_phase_probe.py','steering_nmt_diagnostic_mode.py','steering_center_once.py','steering_diagnostic_finalverify.py']:
    archive(external/name, Path('tools-at-archive')/name)
for name in ['analyze-sdo-missing.py','analyze-steering-comparison.py','sdo-fault-capture-final-1846.json',
             'fault-followup-readonly.py','steering-live-readonly.py','steering-timeout-runtime-repro.c',
             'steering-timeout-runtime-repro.log','static-sdo-probe-1849.log','archive-sdo-fault-comparison.py']:
    archive(root/'out/logs'/name, Path('analysis')/name)
archive(args.comparison, Path('analysis')/args.comparison.name)
for relative in ['app/Core/User/motion/src/patrol_runtime.c', 'app/Core/User/motion/src/bench_drive.c',
                 'app/Core/User/motion/src/bench_steering.c']:
    archive(root/relative, Path('source-at-analysis')/relative)

protected = []
for group in snapshot_groups:
    for source in sorted((root/'out/flash'/group).iterdir()):
        if source.is_file():
            protected.append(dict(path=str(source.resolve()), sha256=sha(source), bytes=source.stat().st_size,
                                  reason='Full RAM may contain credentials; not copied into public evidence.'))

capture = json.loads((root/'out/logs/sdo-fault-capture-final-1846.json').read_text())[0]
if len(capture['pending']) != 1:
    raise RuntimeError('unexpected unmatched final transaction')
pending = capture['pending'][0]
rows = [json.loads(line) for line in (external/'logs/can1-20261021-183631-973740/frames.jsonl').read_bytes().splitlines()]
failure = pending['request']
after = [r for r in rows if r['hardware_timestamp_raw'] > failure['hardware_timestamp_raw']]
comparison = json.loads(args.comparison.read_text())
aggregate = {}
for state in (5,127):
    tests = [r for r in comparison if r['expected_heartbeat_state'] == state]
    counts = collections.Counter()
    for test in tests:
        counts.update(test['statuses'])
    aggregate[str(state)] = dict(sent=sum(t['sent'] for t in tests), statuses=dict(counts),
                                pdo_frames=sum(t['pdo_frames'] for t in tests))
findings = dict(product='patrol_vehicle_chassis', recorded_at=datetime.now().astimezone().isoformat(),
    physical_channel=2, bitrate_kbps=500,
    authorization='Operator authorized steering shutdown and temporary Node 1 pre-operational/operational comparison, keeping all motors stopped; no enable/reset/fault clear.',
    captured_fault=dict(pending=pending, post_request_span_s=rows[-1]['elapsed_s']-failure['elapsed_s'],
                        post_request_frames=len(after), post_request_sdo_requests=sum(r['id'] in (0x601,0x602) for r in after),
                        post_request_ids={hex(k):v for k,v in collections.Counter(r['id'] for r in after).items()},
                        mcu_fault_at_ms=432881, mcu_sent_at_ms=432781,
                        subsequent_readback_607a=13469, previous_acknowledged_target_607a=20398,
                        conclusion='Node 1 accepted final 607A write but matching SDO ACK is absent in full analyzer trace and MCU timed out at 100ms. PDO/heartbeat continue; no CAN stop follows for 26.9s.'),
    comparison=aggregate,
    stationary_reproduction=dict(source='steering-phase-readonly-20261021-185337-062674', sent=398, ok=397, timeout=1,
                                  note='Earlier standalone analyzer read-only reproduction while stationary; steering was still enabled then. Not included in disabled comparison aggregate.'),
    final_switch='operational; heartbeat05 restored, steering and drive disabled; see final NMT result and post snapshot',
    can_hal_accumulated_errors=dict(before='0x40',after='0x58',new_flags=['HAL_CAN_ERROR_STF 0x08','HAL_CAN_ERROR_FOR 0x10'],
        interpretation='Additional stuff/form error flags appeared between snapshots; no per-event timestamps. They cannot be assigned to the missed SDO transactions or used to rule out electrical errors.',
        source='app/Drivers/STM32F1xx_HAL_Driver/Inc/stm32f1xx_hal_can.h'),
    physical_confirmation='Operator confirmed wheels stopped and fault scene retained after direct stop at 18:45. No new movement was requested during comparison.',
    confirmed_stop_defect='Production ApplyCut emergency-stops executors into FAULT and turns software OUT1 off without sending drive zero/shutdown. Manual motor power bypasses OUT1 contactor, so last drive target persists.',
    limitations=['NMT pre-operational changes internal driver state as well as disabling PDO; association alone does not prove a specific driver mailbox/firmware defect.',
                 'Captures show usable CAN frames, not analog waveform or all error frames; cannot fully distinguish driver TX handling, transceiver/electrical corruption, and capture loss.',
                 'Independent MCU timeout and analyzer silence, target readback, and stationary analyzer-only misses narrow the failure beyond a pure MCU SDO parser problem.',
                 'Tools-at-archive files are the latest maintenance versions, not claimed identical to every older run. Extended phase probes embed their exact script hash.',
                 'Firmware, Flash, fault journal and main MCU execution were not modified/reset/halted. This is diagnosis, not a repaired firmware or vehicle acceptance result.'])
(dest/'findings.json').write_text(json.dumps(findings,ensure_ascii=False,indent=2),encoding='utf-8')
entries.append(dict(original_path='new derived findings',archive_path=str((dest/'findings.json').relative_to(root)),
                    sha256=sha(dest/'findings.json'),bytes=(dest/'findings.json').stat().st_size))
(dest/'index.json').write_text(json.dumps(dict(product='patrol_vehicle_chassis',files=entries,protected_originals=protected,
    originals_retained=True),ensure_ascii=False,indent=2),encoding='utf-8')
for entry in entries:
    if sha(root/entry['archive_path']) != entry['sha256']:
        raise RuntimeError('final verification failed')
Path('out/logs/sdo-comparison-evidence-latest.json').write_text(json.dumps(dict(path=str(dest.resolve())),indent=2))
print(json.dumps(dict(evidence=str(dest.resolve()),files=len(entries),comparison=aggregate),ensure_ascii=False,indent=2))

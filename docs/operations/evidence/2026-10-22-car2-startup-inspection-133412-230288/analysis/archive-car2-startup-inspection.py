"""Archive completed CAN1 listen-only inspection and exact-image SWD reads."""
import collections
import datetime
import hashlib
import json
from pathlib import Path
import shutil

root = Path.cwd()
logs = root/'out/logs'
handles = [json.loads((logs/name).read_text(encoding='utf-8')) for name in
           ('car2-startup-capture-current.json','car2-running-capture-current.json')]
captures = []
for handle in handles:
    directory = Path(handle['output'])
    summary = json.loads((directory/'summary.json').read_text(encoding='utf-8'))
    metadata = json.loads((directory/'metadata.json').read_text(encoding='utf-8'))
    assert summary['error'] is None and summary['stop_reason']=='duration'
    assert metadata['hardware_listen_only'] and not metadata['transmit_api_bound']
    assert metadata['physical_channels']=={'CAN1':2}
    for name,digest in json.loads((directory/'sha256.json').read_text(encoding='utf-8')).items():
        assert hashlib.sha256((directory/name).read_bytes()).hexdigest()==digest
    counts=collections.Counter()
    for line in (directory/'frames.jsonl').open(encoding='utf-8'):
        f=json.loads(line);counts[hex(f['id'])]+=1
    assert set(counts)=={'0x701','0x702'},counts
    captures.append({'directory':str(directory),'duration_s':summary['elapsed_s'],
                     'frames':sum(counts.values()),'counts':dict(counts),
                     'only_heartbeats_captured':True,'can1_sdo_requests_captured':0})

snapshot_dirs=[root/'out/flash'/name for name in (
 '2026-10-22-fault-stop-readonly-132217-783084',
 '2026-10-22-fault-stop-readonly-132455-092116',
 '2026-10-22-fault-stop-readonly-132534-892733',
 '2026-10-22-fault-stop-readonly-132617-572067',
 '2026-10-22-fault-stop-readonly-133128-477119',
 '2026-10-22-fault-stop-readonly-133157-795994',
 '2026-10-22-fault-stop-readonly-133254-892751')]
first=json.loads((snapshot_dirs[-2]/'snapshot.json').read_text(encoding='utf-8'))
last=json.loads((snapshot_dirs[-1]/'snapshot.json').read_text(encoding='utf-8'))
hw=json.loads((snapshot_dirs[4]/'snapshot.json').read_text(encoding='utf-8'))
assert first['ram_decoding_valid'] and last['ram_decoding_valid']
assert first['vtor']==last['vtor']=='0x8008000'
assert last['tick_after_ms']>first['tick_after_ms']
assert last['fields']['can1_rx']>first['fields']['can1_rx']
for name in ('fault_pending','fault_latched','busoff_mask','busoff_seen','can1_hal_error',
             'config_calibrated','power_command','can2_rx','t12_switch_seen','t12_armed'):
    assert first['fields'][name]==last['fields'][name]==0,name
assert all(record['matches'] for record in hw['boot_and_state_hashes'].values())
assert hw['regions']['config_header_only']['hex']=='ff'*12
analysis={
 'product':'patrol_vehicle_chassis','operator_selected_vehicle':'CAR2',
 'created_at':datetime.datetime.now().astimezone().isoformat(),
 'operator_confirmation':'Already reset or power-cycled after flashing.',
 'mode':'CAN1 hardware listen-only + running SWD attach/read only',
 'captures':captures,'app_sha256':last['observed_app_sha256'],
 'app_version':'patrol-chassis-0.2.77','boot_and_state_hashes':hw['boot_and_state_hashes'],
 'uid_raw_hex':hw['regions']['uid']['hex'],
 'latest_fields':last['fields'],'previous_valid_snapshot_fields':first['fields'],
 'boot_vtor':last['vtor'],
 'early_ram_snapshot_invalid':str(snapshot_dirs[1]),
 'early_ram_snapshot_explanation':'App VTOR was 0, SysTick disabled, and RAM values failed boolean/range invariants. Those decoded values are NOT valid application state and must not be interpreted as faults/calibration/targets. Later exact-image reads confirmed App VTOR and initialized structures. No agent reset was issued; intervening reset/power timing was not independently observed.',
 'config_evidence':'Flash Config V5 header and calibration-region samples are erased; initialized RAM uses uncalibrated defaults.',
 'conclusions':[
  'Boot, App, State A and State B read back exactly match the selected 0.2.77 bundle.',
  'Latest valid reads show the application running: VTOR=0x08008000, tick and CAN1 receive count increase, both heartbeats are fresh, encoder count/config are being received.',
  'No current MCU fault latch, pending fault or Bus-Off was observed in the valid reads.',
  'Vehicle is not ready for normal control: no saved calibration, software power command off, runtime steering coordinate invalid, and zero CAN2/T12 received frames.',
  'No SDO position, speed, statusword or vendor-fault samples have been obtained; their zero-initialized cache fields do not prove wheel stop or healthy drivers.',
  'No motor enable/motion/clear-latch/reset, CAN transmit or parameter writes were performed.',
 ],
 'next_steps':['Verify receiver-to-CAN2 wiring and receiver/handset power; do not infer transmitter failure from a zero MCU receive count alone.',
               'Initialize this board identity/network configuration as needed and complete this CAR2 mechanical-center calibration; do not copy another vehicle config or historical center blindly.',
               'After valid calibration and fresh feedback, perform a separately authorized physical low-speed steering/traction acceptance.'],
}
analysis_path=logs/'car2-startup-inspection.json'
analysis_path.write_text(json.dumps(analysis,ensure_ascii=False,indent=2),encoding='utf-8')
evidence=root/'docs/operations/evidence'/datetime.datetime.now().strftime('%Y-%m-%d-car2-startup-inspection-%H%M%S-%f')
evidence.mkdir();entries=[]


def archive(source,relative):
    target=evidence/relative;target.parent.mkdir(parents=True,exist_ok=True)
    assert not target.exists()
    shutil.copyfile(source,target)
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    assert hashlib.sha256(target.read_bytes()).hexdigest()==digest
    entries.append({'original_path':str(source.resolve()),'archive_path':str(target.relative_to(root)),
                    'bytes':target.stat().st_size,'sha256':digest})


for handle in handles:
    directory=Path(handle['output'])
    for source in sorted(directory.iterdir()):
        if source.is_file():archive(source,Path('can1')/directory.name/source.name)
    for key in ('stdout','stderr'):
        source=Path(handle[key]);archive(source,Path('can1')/source.name)
for directory in snapshot_dirs:
    for source in sorted(directory.iterdir()):
        if source.is_file():archive(source,Path('swd')/directory.name/source.name)
for name in ('car2-0277-schema.json','car2-0277-readonly.py','car2-hardware-readonly.py',
             'car2-boot-state-readonly.py','car2-startup-fault-readonly.py',
             'prepare-car2-0277-schema.py','car2-0277-abi.c','car2-0277-abi.bin','car2-0277-abi.log',
             'car2-startup-capture-current.json','car2-running-capture-current.json','car2-startup-inspection.json'):
    archive(logs/name,Path('analysis')/name)
archive(Path(__file__),Path('analysis')/Path(__file__).name)
archive(root/'WriteAckVerify_0_2_77_Flash/candidate.manifest.json',Path('image-reference')/'candidate.manifest.json')
archive(root/'docs/operations/evidence/2026-10-22-write-ack-verify-103353/Patrol_Vehicle_Chassis_App.map',Path('image-reference')/'Patrol_Vehicle_Chassis_App.map')
index={'product':'patrol_vehicle_chassis','created_at':analysis['created_at'],'files':entries,
       'hardware_listen_only':True,'target_reset_halt_or_write':False,'driver_parameters_written':False,
       'early_ram_snapshot_invalid':True,'final_snapshots_valid':True}
(evidence/'index.json').write_text(json.dumps(index,ensure_ascii=False,indent=2),encoding='utf-8')
for entry in entries:
    assert hashlib.sha256((root/entry['archive_path']).read_bytes()).hexdigest()==entry['sha256']
(logs/'car2-startup-inspection-latest.json').write_text(json.dumps({'evidence':str(evidence),'analysis':str(analysis_path)},ensure_ascii=False),encoding='utf-8')
print(json.dumps({'evidence':str(evidence),'files':len(entries),'captures':captures},ensure_ascii=False,indent=2))

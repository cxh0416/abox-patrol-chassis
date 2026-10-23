"""Archive current-board center calibration; keep config/secret backups private."""
import datetime
import hashlib
import json
from pathlib import Path
import shutil

root=Path.cwd();logs=root/'out/logs'
operation=Path(json.loads((logs/'car2-center-maintenance-current.json').read_text(encoding='utf-8'))['directory'])
plan=json.loads((operation/'plan.json').read_text(encoding='utf-8'))
verified=json.loads((operation/'verified.json').read_text(encoding='utf-8'))
zero=json.loads((operation/'zero-verified.json').read_text(encoding='utf-8'))
assert verified['post_reset_loaded'] and verified['boot_app_states_recovery_unchanged']
assert verified['fields']['config_calibrated']==1 and verified['fields']['config_steering_center']==-83815
assert len(zero['samples'])==3 and all(x['count']==0 and x['write_state']==2 for x in zero['samples'])
assert len(list(operation.glob('zero-request.jlink')))==1
assert (operation/'config-after.bin').read_bytes()==(operation/'ConfigV5_CAR2_Center.bin').read_bytes()
evidence=root/'docs/operations/evidence'/datetime.datetime.now().strftime('%Y-%m-%d-car2-center-maintenance-%H%M%S-%f')
evidence.mkdir();entries=[];protected=[]


def sha(source):return hashlib.sha256(source.read_bytes()).hexdigest()


def archive(source,relative):
    target=evidence/relative;target.parent.mkdir(parents=True,exist_ok=True)
    assert not target.exists();shutil.copyfile(source,target)
    assert sha(source)==sha(target)
    entries.append({'original_path':str(source.resolve()),'archive_path':str(target.relative_to(root)),
                    'bytes':target.stat().st_size,'sha256':sha(target)})


for source in sorted(operation.iterdir()):
    if source.is_file():
        if source.suffix=='.bin':
            protected.append({'path':str(source.resolve()),'bytes':source.stat().st_size,'sha256':sha(source),
                              'reason':'Full per-device page or RAM config kept in ignored protected local backup; not for another car.'})
        else:archive(source,Path('operation')/source.name)
for directory in sorted((root/'out/flash').glob('2026-10-22-car2-center-read-*')):
    for source in directory.iterdir():
        if source.is_file():archive(source,Path('stationary-reads')/directory.name/source.name)
snapshot=Path(json.loads((logs/'car2-startup-readonly-latest.json').read_text(encoding='utf-8'))['directory'])
for source in snapshot.iterdir():
    if source.is_file():archive(source,Path('post-reset-swd')/source.name)
for name in ('car2-center-maintenance.py','car2-center-read.py','car2-center-mailbox.json',
             'car2-center-maintenance-abi.c','car2-center-maintenance-abi.bin','car2-0277-schema.json'):
    archive(logs/name,Path('tools-at-operation')/name)
archive(Path(__file__),Path('tools-at-operation')/Path(__file__).name)
record={'product':'patrol_vehicle_chassis','vehicle_operator_selected':'CAR2',
        'created_at':datetime.datetime.now().astimezone().isoformat(),
        'operator_confirmation_verbatim':'当前转向轮就在中位，直接读取当前的值然后以该值进行标定',
        'uid_raw_hex':plan['uid_raw_hex'],'app_sha256':plan['app_sha256'],
        'driver_center_measured_now':-83815,'encoder_before':3963,'encoder_zero_verified_samples':zero['samples'],
        'config_address':'0x0803F800','config_record_offset_within_page':148,
        'config_page_sha256':verified['config_page_sha256'],
        'post_reset_loaded':True,'calibrated':True,'encoder_profile_ready':True,
        'method':'Offline per-board maintenance, not an online start/commit calibration session. After exact-image/UID checks, no active control or OTA, disabled driver statuses and three fresh wheel-zero pairs, request the existing encoder task zero once through its verified ABI mailbox. Verify three new encoder zero samples, stage official Config V5 with checksum, preserve all other page bytes, program and read back only the 2KiB config page, reset MCU and verify loaded config plus independent stationary reads.',
        'target_mutations':['one-byte zero request at exact-version mailbox 0x20008754','one Config V5 page at 0x0803F800','halt/program/reset/run for configuration load'],
        'motor_enable_or_motion_commands':False,'motor_driver_tuning_or_limits_changed':False,
        'boot_app_state_pages_and_recovery_unchanged':True,'original_car_profile_source_changed':False,
        'zero_write_replayed':False,
        'verification_interruption':'First zero verification rejected an unsigned time-age result 4294967291 from sequential running SWD sampling. Only read verification was resumed after bounded clock resampling; the encoder-zero command was NOT replayed. Firmware write status was 2 (validated echo).',
        'scope':['Mechanical center confirmed by operator; no independent mechanical angle measurement.',
                 'MCU restart persistence verified; complete motor/encoder power-off persistence not tested.',
                 'CAN2 receiver still not connected/powered; no steering/traction movement acceptance performed.',
                 'Network identity and credentials were not provisioned; existing RAM defaults were preserved.',
                 'This version-specific maintenance sequence is evidence, not a reusable live procedure; never reuse its RAM address or per-board config on another image/vehicle.'],
        'protected_backups':protected}
(evidence/'result.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
entries.append({'original_path':'Current operation result assembled from verified records','archive_path':str((evidence/'result.json').relative_to(root)),
                'bytes':(evidence/'result.json').stat().st_size,'sha256':sha(evidence/'result.json')})
(evidence/'index.json').write_text(json.dumps({'product':'patrol_vehicle_chassis','created_at':record['created_at'],
            'files':entries,'protected_backups':protected},ensure_ascii=False,indent=2),encoding='utf-8')
for entry in entries:assert sha(root/entry['archive_path'])==entry['sha256']
(logs/'car2-center-maintenance-latest.json').write_text(json.dumps({'evidence':str(evidence),'operation':str(operation)},ensure_ascii=False),encoding='utf-8')
print(json.dumps({'evidence':str(evidence),'public_files':len(entries),'protected_backups':len(protected),
                  'center':record['driver_center_measured_now'],'calibrated':True},ensure_ascii=False,indent=2))

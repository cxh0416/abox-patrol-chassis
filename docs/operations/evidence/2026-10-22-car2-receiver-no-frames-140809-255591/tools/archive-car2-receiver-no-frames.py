"""Archive completed passive receiver inspection; no hardware APIs."""
import collections, datetime, hashlib, json, shutil
from pathlib import Path

r=Path.cwd();logs=r/'out/logs'
e=r/'docs/operations/evidence'/datetime.datetime.now().strftime('%Y-%m-%d-car2-receiver-no-frames-%H%M%S-%f')
e.mkdir();entries=[]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def copy(p,rel):
    d=e/rel;d.parent.mkdir(parents=True,exist_ok=True);assert not d.exists();shutil.copyfile(p,d);assert sha(p)==sha(d)
    entries.append({'original_path':str(p.resolve()),'archive_path':str(d.relative_to(r)),
                    'bytes':d.stat().st_size,'sha256':sha(d)})
reports=[]
for handle_name in ('car2-response-capture-current.json','car2-receiver-capture-current.json'):
    hp=logs/handle_name;h=json.loads(hp.read_text(encoding='utf-8'));p=Path(h['output'])
    s=json.loads((p/'summary.json').read_text(encoding='utf-8'));assert s['error'] is None
    m=json.loads((p/'metadata.json').read_text(encoding='utf-8'))
    assert m['hardware_listen_only'] and not m['transmit_api_bound']
    for name,digest in json.loads((p/'sha256.json').read_text(encoding='utf-8')).items():assert sha(p/name)==digest
    counts=collections.Counter()
    for line in (p/'frames.jsonl').open(encoding='utf-8'):
        f=json.loads(line);counts[(f['bus'],f['id'])]+=1
    assert not any(bus=='CAN2' for bus,cob in counts)
    reports.append({'capture':str(p),'duration_s':s['elapsed_s'],'frames':sum(counts.values()),
                    'counts':{f'{b}:0x{i:X}':v for (b,i),v in counts.items()}})
    for x in p.iterdir():
        if x.is_file():copy(x,Path('capture')/p.name/x.name)
    for name in ('stdout','stderr','stop_file'):
        x=Path(h[name]);copy(x,Path('capture')/x.name)
    copy(hp,Path('handles')/hp.name)
bp=json.loads((logs/'car2-receiver-bitrate-latest.json').read_text(encoding='utf-8'))
p=Path(bp['directory']);scan=json.loads((p/'summary.json').read_text(encoding='utf-8'));assert scan['error'] is None
assert all(x['counts'].get('CAN2',0)==0 and x['counts'].get('CAN1',0)>0 for x in scan['results'])
for x in p.iterdir():
    if x.is_file():copy(x,Path('bitrate-scan')/x.name)
for name in ('2026-10-22-fault-stop-readonly-140018-712608','2026-10-22-fault-stop-readonly-140414-894843'):
    p=r/'out/flash'/name
    for x in p.iterdir():
        if x.is_file():copy(x,Path('swd')/name/x.name)
last=json.loads((p/'snapshot.json').read_text(encoding='utf-8'));f=last['fields'];assert last['ram_decoding_valid']
assert f['can2_rx']==f['t12_switch_seen']==f['fault_latched']==f['fault_pending']==f['power_command']==0
assert f['config_calibrated']==1
copy(logs/'car2-receiver-bitrate-listen.py',Path('tools')/'car2-receiver-bitrate-listen.py')
copy(Path(__file__),Path('tools')/Path(__file__).name)
result={'product':'patrol_vehicle_chassis','vehicle':'CAR2','created_at':datetime.datetime.now().astimezone().isoformat(),
        'operator_reports':['CH1 已接 CAN2','已操作，接收器已供电，连接正常'],
        'physical_channels':{'CAN1':2,'CAN2':1},'captures':reports,'bitrate_scan':scan['results'],
        'latest_mcu_fields':f,'verified_app_sha256':last['observed_app_sha256'],
        'conclusion':'No valid CAN2 wire frames observed during operator actions, and MCU raw CAN2/T12 counters remain zero. No fault latch. Diagnose receiver CAN output, wiring/connector/polarity and bitrate before claiming ABOX received but rejected commands.',
        'limits':['Wireless link status is operator-reported and does not establish CAN output.',
                  'No valid captured frame does not prove no electrical activity or identify which component is faulty.',
                  'Short listen-only scan does not rule out other bitrates, invalid/error frames or a transmitter already bus-off.',
                  'CAN1 alone cannot exercise missing-response tolerance when no control/diagnostic transactions occur.',
                  'No control transmissions, resets, parameter writes, motor enable or clear-latch performed in this inspection.']}
(e/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
entries.append({'original_path':'Analysis of archived passive records and operator replies',
                'archive_path':str((e/'result.json').relative_to(r)),'bytes':(e/'result.json').stat().st_size,
                'sha256':sha(e/'result.json')})
(e/'index.json').write_text(json.dumps({'product':'patrol_vehicle_chassis','created_at':result['created_at'],
            'files':entries},ensure_ascii=False,indent=2),encoding='utf-8')
for x in entries:assert sha(r/x['archive_path'])==x['sha256']
(logs/'car2-receiver-no-frames-latest.json').write_text(json.dumps({'evidence':str(e)},ensure_ascii=False),encoding='utf-8')
print(json.dumps({'evidence':str(e),'files':len(entries),'captures':reports},ensure_ascii=False,indent=2))

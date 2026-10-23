"""Archive the completed upload-only heartbeat-timing comparison, without touching hardware."""
from pathlib import Path
import collections, datetime, hashlib, json, shutil
root=Path.cwd()
folder=Path('E:/1_work/ChassisCar_test/logs/steering-timing-compare-20261022-094120-255108')
result=json.loads((folder/'results.json').read_text(encoding='utf-8'))
assert result['error'] is None and result['mode']=='after-heartbeat' and result['sent']==1600
assert all(row['status']=='ok' for row in result['results'])
for key in ('initial_safety','final_safety'):
    rows=result[key]
    assert len(rows)==8 and all(not (r['value']&4) for r in rows[:2])
    assert all(r['value']==0 for r in rows[2:])
tx=0; other_masters=0
for line in (folder/'frames.jsonl').open(encoding='utf-8'):
    f=json.loads(line)
    if f['direction'].startswith('TX'):
        d=bytes.fromhex(f['data']);key=int.from_bytes(d[1:3],'little')
        assert d[0]==0x40 and len(d)==8 and d[3:]==bytes(5)
        assert key in ({0x6041,0x6064,0x6081} if f['id']==0x601 else {0x6041,0x6069,0x6869})
        tx+=1
    elif f['id'] in (0,0x601,0x602):other_masters+=1
assert tx==1616 and not other_masters
e=root/'docs/operations/evidence'/datetime.datetime.now().strftime('%Y-%m-%d-sdo-heartbeat-compare-%H%M%S-%f')
e.mkdir();entries=[]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def archive(src,name):
    dst=e/name;dst.parent.mkdir(parents=True,exist_ok=True);assert not dst.exists()
    shutil.copyfile(src,dst);assert sha(src)==sha(dst)
    entries.append(dict(original_path=str(src.resolve()),archive_path=str(dst.relative_to(root)),bytes=dst.stat().st_size,sha256=sha(dst)))
for p in folder.iterdir():
    if p.is_file():archive(p,Path('raw')/p.name)
for p in [root/'out/logs/sdo-timing-after-heartbeat-B.log',
          Path('E:/1_work/ChassisCar_test/steering_sdo_timing_compare.py'),
          Path('E:/1_work/ChassisCar_test/can_adapter_status_readonly.py'),Path(__file__)]:
    archive(p,Path('analysis')/p.name)
findings=dict(product='patrol_vehicle_chassis',app_image='0.2.76; read-only hash verified at test start',
    comparison_baseline='docs/operations/evidence/2026-10-22-stationary-sdo-readonly-092939-078498/index.json',
    baseline=dict(mode='sweep around predicted heartbeat',sent=1592,success=1588,timeout=4),
    current=dict(mode='upload 30ms after host-received heartbeat',sent=1600,success=1600,timeout=0,abort=0),
    initial_safety=result['initial_safety'],final_safety=result['final_safety'],tx_uploads=tx,
    other_can_masters=other_masters,driver_parameters_written=False,
    limitations=['Nonrandomized short stationary comparison; not a guarantee of stability.',
                 'Host timestamps do not establish exact on-wire phase.',
                 'No proof of driver firmware defect versus electrical/capture loss.',
                 'This test predates the new write-verification firmware and is not its field acceptance.'])
(e/'findings.json').write_text(json.dumps(findings,ensure_ascii=False,indent=2),encoding='utf-8')
entries.append(dict(original_path='analysis derived from original raw and independently archived baseline',
    archive_path=str((e/'findings.json').relative_to(root)),bytes=(e/'findings.json').stat().st_size,sha256=sha(e/'findings.json')))
(e/'index.json').write_text(json.dumps(dict(product='patrol_vehicle_chassis',capture_closed=True,upload_only=True,files=entries),ensure_ascii=False,indent=2),encoding='utf-8')
assert all(sha(root/r['archive_path'])==r['sha256'] for r in entries)
(root/'out/logs/sdo-heartbeat-compare-latest.json').write_text(json.dumps(dict(evidence=str(e)),ensure_ascii=False),encoding='utf-8')
print(json.dumps(dict(evidence=str(e),files=len(entries),success=1600,timeout=0),ensure_ascii=False))

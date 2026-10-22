"""Offline audit and immutable archive of the completed 0.2.76 reproduction."""
import bisect,collections,datetime,hashlib,json,pathlib,shutil,struct
root=pathlib.Path.cwd();h=json.loads((root/'out/logs/can1-capture-current.json').read_text(encoding='utf-8-sig'));capture=pathlib.Path(h['output'])
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
summary=json.loads((capture/'summary.json').read_text(encoding='utf-8'));assert summary['error'] is None and summary['stop_reason']=='operator_stop_file'
for name,digest in json.loads((capture/'sha256.json').read_text(encoding='utf-8')).items():assert sha(capture/name)==digest,name
rows=[json.loads(x) for x in (capture/'frames.jsonl').read_bytes().splitlines()]
pending={};missing=[];completed=collections.Counter();requests=collections.Counter();heartbeats=[x for x in rows if x['id']==0x701];ticks=[x['hardware_timestamp_raw'] for x in heartbeats]
for ordinal,f in enumerate(rows):
 d=f.get('decoded',{})
 if d.get('kind')!='sdo':continue
 key=(d['node'],d['index'],d['subindex']);action=d.get('action')
 if action in ('read_request','write_request'):
  requests[d['node']]+=1
  if key in pending:
   old=pending.pop(key);old['until_repeat_s']=f['elapsed_s']-old['request']['elapsed_s'];missing.append(old)
  pending[key]={'request':f,'ordinal':ordinal}
 elif key in pending and (action=='abort' or (action=='write_ack' and pending[key]['request']['decoded']['action']=='write_request') or (action=='read_response' and pending[key]['request']['decoded']['action']=='read_request')):
  pending.pop(key);completed[d['node']]+=1
missing+=list(pending.values())
for x in missing:
 f=x['request'];i=bisect.bisect_left(ticks,f['hardware_timestamp_raw']);x['next_heartbeat_raw_delta']=ticks[i]-f['hardware_timestamp_raw'] if i<len(ticks) else None;x['following_frames']=rows[x['ordinal']+1:x['ordinal']+8]
trigger=next(x['request'] for x in missing if x['request']['decoded'].get('value')==0x3f)
shutdowns=[f for f in rows if f.get('decoded',{}).get('action')=='write_request' and f['decoded'].get('index')==0x6040 and f['decoded'].get('value')==6 and f['elapsed_s']>trigger['elapsed_s']]
first_shutdown={n:next(x for x in shutdowns if x['decoded']['node']==n) for n in (1,2)}
first_disabled={n:next(f for f in rows if f['elapsed_s']>=first_shutdown[n]['elapsed_s'] and f.get('decoded',{}).get('node')==n and f['decoded'].get('index')==0x6041 and f['decoded'].get('action')=='read_response' and not (f['decoded']['value']&4)) for n in (1,2)}
snapshots=sorted((root/'out/flash').glob('2026-10-22-fault-stop-readonly-*'));assert len(snapshots)==3
snap=json.loads((snapshots[0]/'snapshot.json').read_text(encoding='utf-8'));fields=snap['fields'];assert snap['observed_app_sha256']=='177efdb46df9a968755edcaf0007fac78768125fb2bad2a43e4ca7fa27d822b2'
assert fields['fault_latched']==1 and fields['fault_stop_disabled_mask']==3 and fields['fault_stop_active']==0
names='magic sequence at_ms source reason gates target_rpm target_mdeg left_rpm right_rpm driver_raw encoder_raw steer_sw drive_sw left_code right_code left_code_age right_code_age steer_age drive_age bench_phase bench_step bench_fault drive_phase drive_step drive_fault last_abort encoder_error center_phase reserved0 reserved1 checksum'.split()
record=dict(zip(names,struct.unpack('<32I',(snapshots[0]/'motion-fault.bin').read_bytes())))
for k in ('target_rpm','target_mdeg','left_rpm','right_rpm','driver_raw'):
 if record[k]>=2**31:record[k]-=2**32
assert record['gates']==8 and record['bench_fault']==1 and record['drive_fault']==3
assert record['at_ms']-fields['steering_sent_ms']==180 and record['steer_age']==270
post=[f for f in rows if f['elapsed_s']>=first_shutdown[2]['elapsed_s']]
nonzero_after=[f for f in post if f.get('decoded',{}).get('action')=='write_request' and f['decoded'].get('index') in (0x60ff,0x68ff,0x607a) and f['decoded'].get('value')!=0]
assert not nonzero_after
latest=json.loads((snapshots[-1]/'snapshot.json').read_text(encoding='utf-8'));lf=latest['fields'];age=(latest['tick_after_ms']-lf['t12_last_switch_ms'])&0xffffffff;assert age<=300 and lf['fault_latched']==1
findings={'product':'patrol_vehicle_chassis','recorded_at':datetime.datetime.now().astimezone().isoformat(),'capture':str(capture),'capture_frames':len(rows),'capture_seconds':summary['elapsed_s'],'requests':dict(requests),'matched_replies':dict(completed),'missing':missing,'last_missing_trigger':trigger,'first_shutdown':first_shutdown,'first_disabled_status':first_disabled,'fault_record':record,'snapshot_fields':fields,'latest_t12':{'can2_rx':lf['can2_rx'],'a_value':lf['t12_last_enable'],'switch_age_ms':age,'fault_latched':lf['fault_latched']},'nonzero_motion_writes_after_stop':len(nonzero_after),'operator_report':{'firmware':'0.2.76 installed by user; independently hash-matched through SWD','wheels':'Operator reported wheels did not continue rotating after reproduction','steering':'Operator reported steering no longer controllable','scene':'Retained without MCU restart during this investigation'},'conclusions':['Missing response to Node 1 6040=0x3F precedes safety loss; independent of any inferred radio failure.','This episode trips feedback gate bit 8 (steering status stale); frozen steer age is 270ms, only 180ms after the pending trigger, before its 200ms transaction deadline. Drive fault3 is unsafe input; steering fault1 is a propagated safety stop.','New fault-stop path transmits shutdown to both nodes and zeros both wheel targets; status eventually confirms both Operation Enabled bits clear.','Stop latch deadline600ms is shorter than the final drive disable confirmation; latch_due remains1 and retries continued after software power off.','Current receiver-to-ABOX CAN2 switch stream is fresh; no synchronized CAN2 capture exists for the exact failure instant.'],'limitations':['Physical stopping time/distance cannot be inferred from status/ACK; only the operator confirmed the eventual physical stop.','The RAM wheel speeds1000/-1001 are pre-fault cached samples; they are not current wheel speeds.','No live post-fault driver parameters were written, no enable/reset/clear was sent by this investigation.','All frame times are host receive-batch times with original raw hardware timestamps retained; no unverified raw timestamp unit is assumed.','No definitive driver firmware/electrical failure attribution from valid-frame capture alone.']}
analysis=root/'out/logs/fault-stop-0276-field-findings.json';analysis.write_text(json.dumps(findings,ensure_ascii=False,indent=2),encoding='utf-8')
e=root/'docs/operations/evidence'/datetime.datetime.now().strftime('%Y-%m-%d-fault-stop-field-%H%M%S-%f');e.mkdir();entries=[]
def archive(src,relative):
 dst=e/relative;dst.parent.mkdir(parents=True,exist_ok=True);assert not dst.exists();shutil.copyfile(src,dst);assert sha(src)==sha(dst)
 entries.append({'original_path':str(src.resolve()),'archive_path':str(dst.relative_to(root)),'bytes':dst.stat().st_size,'sha256':sha(dst)})
for src in sorted(capture.iterdir()):
 if src.is_file():archive(src,pathlib.Path('raw')/capture.name/src.name)
for key in ('stdout','stderr','stop_file'):
 src=pathlib.Path(h[key]);archive(src,pathlib.Path('raw')/src.name)
for folder in snapshots:
 for src in sorted(folder.iterdir()):
  if src.is_file():archive(src,pathlib.Path('swd')/folder.name/src.name)
for name in ('fault-stop-0276-field-findings.json','can1-capture-progress.py','fault-stop-0276-readonly.py','fault-stop-0276-schema.json','fault-stop-0276-t12-schema.json','fault-stop-0276-abi.c','fault-stop-0276-abi.log','can1-capture-current.json'):
 archive(root/'out/logs'/name,pathlib.Path('analysis')/name)
for name in ('patrol_runtime.c','bench_drive.c','bench_steering.c','patrol_diagnostics.c','patrol_phy_remote.c','patrol_fault_stop.c'):
 archive(root/'app/Core/User/motion/src'/name,pathlib.Path('source-at-analysis')/name)
archive(pathlib.Path('E:/1_work/ChassisCar_test/can1_bus_monitor.py'),'analysis/can1_bus_monitor.py')
archive(pathlib.Path(__file__),'analysis/'+pathlib.Path(__file__).name)
index={'product':'patrol_vehicle_chassis','app_version':'patrol-chassis-0.2.76','app_sha256':snap['observed_app_sha256'],'app_image_read_only_verified':True,'board_flashed_by_agent':False,'driver_parameters_written':False,'target_halted_or_reset':False,'capture_closed':True,'files':entries}
(e/'index.json').write_text(json.dumps(index,ensure_ascii=False,indent=2),encoding='utf-8')
for x in entries:assert sha(root/x['archive_path'])==x['sha256']
(root/'out/logs/fault-stop-field-latest.json').write_text(json.dumps({'evidence':str(e),'findings':str(analysis),'capture':str(capture)},ensure_ascii=False),encoding='utf-8')
print(json.dumps({'evidence':str(e),'files':len(entries),'frames':len(rows),'requests':dict(requests),'responses':dict(completed),'missing_next_hb_raw_delta':[x['next_heartbeat_raw_delta'] for x in missing],'fault_at_ms':record['at_ms'],'steering_transaction_age_ms':record['at_ms']-fields['steering_sent_ms'],'steering_status_age_ms':record['steer_age'],'gate':record['gates'],'disabled_status':{n:hex(f['decoded']['value']) for n,f in first_disabled.items()},'last_switch_age_ms':age},ensure_ascii=False,indent=2))

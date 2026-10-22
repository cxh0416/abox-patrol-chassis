"""Read the existing capture only; no device APIs or CAN transmissions."""
import collections,datetime,json,pathlib
root=pathlib.Path.cwd()
h=json.loads((root/'out/logs/can1-capture-current.json').read_text(encoding='utf-8-sig'))
source=pathlib.Path(h['output'])/'frames.jsonl'
pending={};counts=collections.Counter();last={};missed=[];shutdowns=[];max_reply_ms=0.0;completed=0;end=0.0
for line in source.open(encoding='utf-8'):
 try:f=json.loads(line)
 except json.JSONDecodeError:continue
 end=max(end,f['elapsed_s']);counts[hex(f['id'])]+=1
 d=f.get('decoded')
 if not d:continue
 if d['kind']=='heartbeat':last['heartbeat_node'+str(d['node'])]=f
 if d['kind']!='sdo':continue
 action=d.get('action');key=(d['node'],d['index'],d['subindex'])
 if action in ('read_request','write_request'):
  if key in pending:
   old=pending.pop(key)
   missed.append(dict(request=old,until_repeat_ms=round((f['elapsed_s']-old['elapsed_s'])*1000,3)))
  pending[key]=f
 elif key in pending and (action=='abort' or (action=='write_ack' and pending[key]['decoded']['action']=='write_request') or (action=='read_response' and pending[key]['decoded']['action']=='read_request')):
  request=pending.pop(key);completed+=1
  max_reply_ms=max(max_reply_ms,(f['elapsed_s']-request['elapsed_s'])*1000)
 if action in ('read_response','write_request'):
  last[f"node{d['node']}_{action}_{d['object']}"]=f
 if action=='write_request' and d['index']==0x6040 and d.get('value')==6:shutdowns.append(f)
def brief(f):
 d=f['decoded'];return dict(at_s=round(f['elapsed_s'],3),age_ms=round((end-f['elapsed_s'])*1000),data=f['data'],**{k:d[k] for k in ('kind','node','object','action','value','state') if k in d})
interesting=('statusword','left_actual_rpm','right_actual_rpm','left_target_rpm','right_target_rpm','controlword','steering_target_count')
result=dict(host_time=datetime.datetime.now().astimezone().isoformat(),capture=h['output'],elapsed_s=round(end,3),frames=sum(counts.values()),counts=dict(counts),matched_sdo=completed,max_host_batch_reply_ms=round(max_reply_ms,3),
 missing_before_repeat=[dict(**brief(x['request']),until_repeat_ms=x['until_repeat_ms']) for x in missed[-12:]],pending=[brief(x) for x in pending.values()],
 last={k:brief(f) for k,f in last.items() if k.startswith('heartbeat') or f['decoded'].get('object') in interesting},shutdown_writes=[brief(x) for x in shutdowns[-12:]],
 limitations=['Passive valid-frame capture cannot establish MCU fault_latched or firmware version.','Host receive-batch latency is approximate; raw hardware timestamps remain in the original capture.','No physical wheel stop confirmation is inferred from target, ACK or status alone.'])
(root/'out/logs/can1-capture-progress.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))

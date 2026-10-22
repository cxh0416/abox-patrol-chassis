"""Offline analysis: match captured requests/replies, preserving request gaps and timing limits."""
import argparse
import bisect
import collections
import hashlib
import json
from pathlib import Path

def analyze(folder):
    path=folder/'frames.jsonl'
    raw=path.read_bytes();rows=[]
    for line in raw.splitlines():
        try:rows.append(json.loads(line))
        except ValueError:pass  # A live capture can end with a partially written final line.
    pending={};missing=[];pairs=[];aborts=[];clips=[]
    hb=[f['hardware_timestamp_raw'] for f in rows if f['id']==0x701 and f.get('time_flag')]
    for ordinal,f in enumerate(rows):
        p=bytes.fromhex(f['data']);c=f['id']
        if len(p)<4:continue
        if c in (0x601,0x602):
            k=(c-0x600,int.from_bytes(p[1:3],'little'),p[3])
            if k in pending:
                original,old=pending[k];t0=old['elapsed_s'];t1=f['elapsed_s'];t=old.get('hardware_timestamp_raw');n=bisect.bisect_left(hb,t) if t is not None else len(hb)
                gap=dict(node=k[0],index=hex(k[1]),sub=k[2],request=old,
                    repeated_request=f,repeat_gap_host_ms=(t1-t0)*1000,
                    next_steering_heartbeat_raw_delta=hb[n]-t if n<len(hb) else None,
                    frames_between=dict((hex(cob),count) for cob,count in collections.Counter(q['id'] for q in rows[original+1:ordinal]).items()))
                missing.append(gap)
                clips.append(dict(gap_ordinal=len(missing)-1,frames=rows[max(0,original-5):min(len(rows),ordinal+7)]))
            pending[k]=(ordinal,f)
        elif c in (0x581,0x582):
            k=(c-0x580,int.from_bytes(p[1:3],'little'),p[3]);old=pending.pop(k,None)
            if old:
                req=old[1];pairs.append(dict(node=k[0],index=hex(k[1]),host_roundtrip_ms=(f['elapsed_s']-req['elapsed_s'])*1000))
            if p[0]==0x80:aborts.append(dict(at_s=f['elapsed_s'],node=k[0],index=hex(k[1]),abort=hex(int.from_bytes(p[4:8],'little'))))
    rate=None
    timed=[r for r in rows if r.get('time_flag')]
    if timed and timed[-1]['elapsed_s']-timed[0]['elapsed_s']>10:
        rate=(timed[-1]['hardware_timestamp_raw']-timed[0]['hardware_timestamp_raw'])/(timed[-1]['elapsed_s']-timed[0]['elapsed_s'])
    return dict(source=str(path.resolve()),source_prefix_sha256=hashlib.sha256(raw).hexdigest(),prefix_bytes=len(raw),frames=len(rows),
        span_s=rows[-1]['elapsed_s']-rows[0]['elapsed_s'] if rows else 0,
        empirical_raw_timestamp_ticks_per_host_second=rate,matched_pairs=len(pairs),missing_before_repeat=missing,
        pending=[dict(node=k[0],index=hex(k[1]),request=v[1]) for k,v in pending.items()],
        abort_count=len(aborts),abort_types=dict(collections.Counter('%d:%s:%s'%(a['node'],a['index'],a['abort']) for a in aborts)),
        observed_max_matched_host_roundtrip_ms=max((p['host_roundtrip_ms'] for p in pairs),default=None),clips=clips,
        limitations=['No matching reply in this analyzer trace does not alone distinguish absent transmission, corrupted frame, analyzer loss, or driver RX/TX behavior.',
                    'Repeated request after ~60ms also agrees with MCU diagnostic retry, independently supporting missing usable reply at MCU.',
                    'Host timestamps have batching/quantization; raw tick scale is empirically fitted here, not asserted from SDK specification.',
                    'Heartbeat association is correlation, not proof of driver mailbox implementation.'])

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('folders',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    report=[analyze(f) for f in a.folders]
    with a.output.open('x',encoding='utf-8') as stream:json.dump(report,stream,indent=2,ensure_ascii=False)
    print(json.dumps([dict(source=r['source'],frames=r['frames'],missing=len(r['missing_before_repeat']),raw_ticks_per_s=r['empirical_raw_timestamp_ticks_per_host_second']) for r in report],indent=2))

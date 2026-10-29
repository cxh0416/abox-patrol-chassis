"""Offline audit of the completed passive dual-CAN capture; no hardware API."""
import collections
import datetime
import hashlib
import json
from pathlib import Path
import shutil

root = Path.cwd()
handle_path = root / 'out/logs/dual-can-capture-current.json'
handle = json.loads(handle_path.read_text(encoding='utf-8-sig'))
capture = Path(handle['output'])
metadata = json.loads((capture / 'metadata.json').read_text(encoding='utf-8'))
summary = json.loads((capture / 'summary.json').read_text(encoding='utf-8'))
assert summary['error'] is None and summary['stop_reason'] == 'operator_stop_file'
assert metadata['hardware_listen_only'] and not metadata['transmit_api_bound']
assert metadata['physical_channels'] == {'CAN1': 2, 'CAN2': 1}


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


for name, digest in json.loads((capture / 'sha256.json').read_text(encoding='utf-8')).items():
    assert sha(capture / name) == digest, name

frames = [json.loads(line) for line in (capture / 'frames.jsonl').open(encoding='utf-8')]
pending, unmatched, matched = {}, [], []
counts, writes, aborts = collections.Counter(), collections.Counter(), collections.Counter()
last_bus, gaps, shutdowns = {}, [], []
axis_range = {'throttle': [0, 0], 'steer': [0, 0]}
last_feedback, last_status, last_fault = {}, {}, {}
for f in frames:
    bus, at = f['bus'], f['elapsed_s']
    counts[(bus, f['id'])] += 1
    if bus in last_bus and at - last_bus[bus] > 1:
        gaps.append({'bus': bus, 'from_s': last_bus[bus], 'to_s': at,
                     'host_gap_s': at - last_bus[bus]})
    last_bus[bus] = at
    d = f.get('decoded', {})
    if d.get('kind') == 't12_joystick':
        for name, key in [('throttle', 'throttle_raw'), ('steer', 'steer_raw')]:
            axis_range[name][0] = min(axis_range[name][0], d[key])
            axis_range[name][1] = max(axis_range[name][1], d[key])
    if bus != 'CAN1' or d.get('kind') != 'sdo':
        continue
    action = d.get('action')
    key = (d['node'], d['index'], d['subindex'])
    if action in ('read_request', 'write_request'):
        if key in pending:
            old = pending.pop(key)
            unmatched.append({'request': old, 'repeat_request': f,
                              'repeat_host_ms': round(1000 * (at - old['elapsed_s']), 3)})
        pending[key] = f
        if action == 'write_request':
            writes[(d['node'], d['index_hex'])] += 1
            if d['index'] == 0x6040 and d.get('value') == 6:
                shutdowns.append(f)
    elif action in ('read_response', 'write_ack', 'abort') and key in pending:
        old = pending[key]
        wanted = 'read_request' if action == 'read_response' else 'write_request'
        if action == 'abort' or old['decoded']['action'] == wanted:
            pending.pop(key)
            matched.append({'request': old, 'response': f,
                            'host_reply_ms': round(1000 * (at - old['elapsed_s']), 3)})
    if action == 'abort':
        aborts[(d['node'], d['index_hex'], f['data'])] += 1
    if action == 'read_response':
        if d['index'] in (0x6069, 0x6869): last_feedback[d['index_hex']] = f
        if d['index'] == 0x6041: last_status[str(d['node'])] = f
        if d['index'] in (0x2000, 0x2001): last_fault[d['index_hex']] = f

# Associate each absent response with the immediate next identical upload and
# its response. Matching cannot distinguish a late original from retry reply.
by_request = {id(m['request']): m for m in matched}
retries = []
for item in unmatched:
    response = by_request.get(id(item['repeat_request']))
    retries.append(dict(item, retry_completed=response is not None,
                        retry_response=response['response'] if response else None))
write_matches = [m for m in matched if m['request']['decoded']['action'] == 'write_request']
write_unmatched = [r for r in unmatched if r['request']['decoded']['action'] == 'write_request']
write_pending = [r for r in pending.values() if r['decoded']['action'] == 'write_request']
assert len(write_matches) + len(write_unmatched) + len(write_pending) == sum(writes.values())
steering_retries = [r for r in retries if r['request']['decoded']['node'] == 1
                    and r['request']['decoded']['action'] == 'read_request']
recovered = [r for r in steering_retries if r['retry_completed']]
examples = []
for r in recovered:
    at = r['retry_response']['elapsed_s']
    future = next((f for f in frames if f['elapsed_s'] > at
                   and f.get('decoded', {}).get('action') == 'write_request'
                   and f['decoded']['index'] in (0x607a, 0x60ff, 0x68ff)
                   and f['elapsed_s'] < at + 10), None)
    off = [f for f in shutdowns if at <= f['elapsed_s'] < at + 1]
    examples.append({'node': 1, 'index': r['request']['decoded']['index_hex'],
                     'request_s': r['request']['elapsed_s'],
                     'retry_s': r['repeat_request']['elapsed_s'],
                     'retry_host_ms': r['repeat_host_ms'],
                     'response_s': at, 'shutdown_within_1s': len(off),
                     'next_motion_write_within_10s': future})

analysis = {
    'product': 'patrol_vehicle_chassis',
    'analyzed_at': datetime.datetime.now().astimezone().isoformat(),
    'capture': str(capture), 'capture_duration_s': summary['elapsed_s'],
    'first_frame_s': frames[0]['elapsed_s'], 'last_frame_s': frames[-1]['elapsed_s'],
    'frames': len(frames),
    'counts': {f'{bus}:0x{cob:03X}': n for (bus, cob), n in sorted(counts.items())},
    'operator_report_verbatim': '已进行了长时间测试，小车表现合格，没有安全风险',
    'operator_report_scope': 'Operator observation for this run; not a universal safety certification.',
    'installed_image': {'verified': False, 'reason': 'SWD attach failed; operator did not explicitly confirm 0.2.77.'},
    'joystick_raw_ranges': axis_range,
    'sdo_matched': len(matched), 'write_requests': sum(writes.values()),
    'write_acks': len([m for m in write_matches if m['response']['decoded']['action'] == 'write_ack']),
    'write_abort_responses': len([m for m in write_matches if m['response']['decoded']['action'] == 'abort']),
    'write_no_response_before_repeat': write_unmatched, 'write_pending_at_end': write_pending,
    'write_objects': {f'node{node}:{index}': n for (node, index), n in sorted(writes.items())},
    'read_no_response_before_repeat': retries,
    'pending_at_end': list(pending.values()),
    'steering_diagnostic_retry_count': len(steering_retries),
    'steering_diagnostic_retry_response_count': len(recovered),
    'steering_retry_examples': examples,
    'passive_40ms_alerts': summary['unanswered_sdo_events'],
    'aborts': [{'node': n, 'index': idx, 'data': raw, 'count': count}
               for (n, idx, raw), count in sorted(aborts.items())],
    'shutdown_writes': shutdowns, 'last_speed_feedback': last_feedback,
    'last_status': last_status, 'last_vendor_fault': last_fault,
    'host_observed_bus_gaps_over_1s': gaps,
    'conclusions': [
        'Operator reported acceptable physical behavior in a long-duration run.',
        'Node 1 status/position uploads without captured replies were followed by about 60ms repeat uploads and successful responses; subsequent traffic continued.',
        'Every captured write request had an ACK; the missing-write-ACK execution-verification branch was not exercised by this capture.',
        'End-of-run CAN2 cessation preceded zero writes and both shutdown writes; fresh later status responses show both nodes outside Operation Enabled. No MCU latch state is inferred.',
        'Nonzero vendor fault values and EMCY exist near capture boundaries; their causes are not determined by this passive trace.',
    ],
    'limitations': [
        'Missing captured replies do not establish driver omission rather than acquisition loss.',
        'Retry reply matching cannot distinguish late original replies from replies to a repeat upload.',
        'Host receive-batch deltas are approximate; raw adapter timestamps have no assumed unit or cross-channel synchronization.',
        'Capture includes initial and final bus silence; silence alone does not identify power-off, disconnection or a fault.',
        'No exact board firmware hash, internal latch snapshot, physical stopping distance or complete safety validation was obtained.',
        'Listen-only; no CAN transmissions, MCU reset/halt, parameter changes, enable or clear-latch actions performed by this tool.',
    ],
}
analysis_path = root / 'out/logs/write-verify-field-analysis.json'
analysis_path.write_text(json.dumps(analysis, ensure_ascii=False, indent=2), encoding='utf-8')
evidence = root / 'docs/operations/evidence' / datetime.datetime.now().strftime('%Y-%m-%d-write-verify-field-%H%M%S-%f')
evidence.mkdir()
entries = []


def archive(source, relative):
    target = evidence / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    assert not target.exists()
    shutil.copyfile(source, target)
    digest = sha(source)
    assert sha(target) == digest
    entries.append({'original_path': str(source.resolve()), 'archive_path': str(target.relative_to(root)),
                    'bytes': target.stat().st_size, 'sha256': digest})


for source in sorted(capture.iterdir()):
    if source.is_file(): archive(source, Path('raw') / source.name)
for key in ('stdout', 'stderr', 'stop_file'):
    archive(Path(handle[key]), Path('raw') / Path(handle[key]).name)
for source in [handle_path, analysis_path, Path(__file__),
               root / 'out/logs/write-verify-image-readonly.py',
               root / 'out/logs/write-verify-image-readonly-latest.json',
               Path('E:/1_work/ChassisCar_test/can1_bus_monitor.py')]:
    archive(source, Path('analysis') / source.name)
image_handle = json.loads((root / 'out/logs/write-verify-image-readonly-latest.json').read_text(encoding='utf-8'))
archive(Path(image_handle['directory']) / 'snapshot.json', Path('swd-image-check') / 'snapshot.json')
index = {'product': 'patrol_vehicle_chassis', 'created_at': datetime.datetime.now().astimezone().isoformat(),
         'capture_closed': True, 'hardware_listen_only': True, 'files': entries}
(evidence / 'index.json').write_text(json.dumps(index, ensure_ascii=False, indent=2), encoding='utf-8')
for entry in entries: assert sha(root / entry['archive_path']) == entry['sha256']
(root / 'out/logs/write-verify-field-latest.json').write_text(
    json.dumps({'evidence': str(evidence), 'analysis': str(analysis_path)}, ensure_ascii=False), encoding='utf-8')
print(json.dumps({k: analysis[k] for k in ['frames', 'capture_duration_s', 'write_requests', 'write_acks',
    'write_abort_responses', 'steering_diagnostic_retry_count', 'steering_diagnostic_retry_response_count',
    'joystick_raw_ranges']}, ensure_ascii=False, indent=2))
print('STEERING_RETRIES', json.dumps([{k: v for k, v in ex.items() if k != 'next_motion_write_within_10s'}
                                   for ex in examples], ensure_ascii=False))
print('EVIDENCE', evidence, 'FILES', len(entries))

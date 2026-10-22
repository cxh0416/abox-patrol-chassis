"""Offline evidence summary; never opens CAN or modifies original records."""
import argparse
import collections
import hashlib
import json
from pathlib import Path


def analyze(folder):
    result = json.loads((folder / 'results.json').read_text(encoding='utf-8'))
    raw = (folder / 'frames.jsonl').read_bytes()
    frames = [json.loads(line) for line in raw.splitlines()]
    received = [f for f in frames if f['direction'] == 'RX']
    counter = collections.Counter(f['id'] for f in received)
    misses = []
    for sample in result['results']:
        if sample['status'] != 'timeout':
            continue
        at = sample['elapsed_s']
        window = [f for f in frames if at <= f['elapsed_s'] < at + .3]
        misses.append(dict(sample=sample, frames=window,
                           frame_counts={hex(k): v for k, v in collections.Counter(f['id'] for f in window).items()}))
    return dict(source=str(folder.resolve()), source_sha256=hashlib.sha256(raw).hexdigest(),
                expected_heartbeat_state=result['expected_heartbeat_state'],
                sent=result['sent'], statuses=dict(collections.Counter(r['status'] for r in result['results'])),
                received_ids={hex(k): v for k, v in sorted(counter.items())},
                heartbeat_values=dict(collections.Counter(f['data'] for f in received if f['id'] == 0x701)),
                pdo_frames=counter[0x181]+counter[0x281],
                unexpected_other_master_requests=counter[0x601]+counter[0x602],
                other_transmissions=[f for f in frames if f['direction'] != 'RX' and f['direction'] != 'TX_READ_ONLY'],
                error=result['error'], missing_windows=misses,
                limitation='Host scheduling and USB reception timestamps do not measure exact bus phase; rare misses need adequate repetitions. NMT pre-operational changes driver state as well as disabling PDO.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folders', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = [analyze(folder) for folder in args.folders]
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
    print(json.dumps([{k: r[k] for k in ('source','sent','statuses','heartbeat_values','pdo_frames','unexpected_other_master_requests','error')} for r in report], indent=2))

#!/usr/bin/env python3
"""Create deterministic A/B BootState pages for a field-test baseline."""
from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


MAGIC = 0x42535632
VERSION = 3
CONFIRMED = 4
PAGE_SIZE = 0x800
FORMAT = '<IHHIIBBBBII32sII32sIBB2sI'


def crc(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def make_record(app: Path, version: str) -> bytes:
    payload = app.read_bytes()
    image_crc = crc(payload)
    version_bytes = version.encode('ascii')[:31].ljust(32, b'\0')
    record = struct.pack(
        FORMAT, MAGIC, VERSION, struct.calcsize(FORMAT), 1, CONFIRMED,
        0, 0, 0, 0, len(payload), image_crc, version_bytes,
        len(payload), image_crc, version_bytes, 0, 0, 0, b'\0\0', 0,
    )
    record = record[:-4] + struct.pack('<I', crc(record[:-4]))
    return record


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--state-a', required=True, type=Path)
    parser.add_argument('--state-b', required=True, type=Path)
    args = parser.parse_args()
    record = make_record(args.app, args.version)
    page = record + b'\xFF' * (PAGE_SIZE - len(record))
    args.state_a.parent.mkdir(parents=True, exist_ok=True)
    args.state_b.parent.mkdir(parents=True, exist_ok=True)
    args.state_a.write_bytes(page)
    args.state_b.write_bytes(b'\xFF' * PAGE_SIZE)
    print(f'BootState A: {args.state_a} size={len(page)} crc={zlib.crc32(record[:-4]) & 0xFFFFFFFF:08X}')
    print(f'BootState B: {args.state_b} size={PAGE_SIZE} erased=1')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

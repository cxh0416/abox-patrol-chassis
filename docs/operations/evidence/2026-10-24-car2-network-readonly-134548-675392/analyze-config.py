#!/usr/bin/env python3
"""Decode non-secret Config V5 fields from the read-only J-Link page dump."""

from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path


PAGE = Path(__file__).with_name("config-page.bin")
REMOTE_OFFSET = 148
TLS_RECORD_OFFSET = 580
CONFIG_MAGIC = 0x56435543
CONFIG_VERSION = 5
CONFIG_FORMAT = "<IHHI4B32s64sHH32s64s64s96siiIHbbBB2xIBB2s"


def config_checksum(data: bytes) -> int:
    value = 0x13572468
    for index, byte in enumerate(data[:400]):
        if 8 <= index < 12:
            continue
        value = (((value << 5) & 0xFFFFFFFF) ^ (value >> 27) ^ byte) & 0xFFFFFFFF
    return value


def text(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", errors="replace")


def main() -> int:
    page = PAGE.read_bytes()
    assert len(page) == 0x800
    raw = page[REMOTE_OFFSET:REMOTE_OFFSET + 400]
    fields = struct.unpack(CONFIG_FORMAT, raw)
    (
        magic, version, length, checksum,
        reserved_remote, diag_log_enabled, diag_log_level, reserved0,
        vid_raw, mqtt_host_raw, mqtt_port, diag_log_port,
        mqtt_username_raw, mqtt_password_raw, diag_log_host_raw, ota_origin_raw,
        steering_center_count, absolute_center_count, absolute_counts_per_degree_milli,
        steering_tolerance_mdeg, steering_direction, absolute_direction, calibrated,
        reserved1, steering_counts_per_degree_milli, mqtt_tls_enabled,
        mqtt_tls_profile_id, reserved2,
    ) = fields

    config_valid = (
        magic == CONFIG_MAGIC and version == CONFIG_VERSION and length == 400 and
        reserved_remote <= 1 and mqtt_tls_enabled <= 1 and diag_log_level <= 3 and
        checksum == config_checksum(raw)
    )
    record = page[TLS_RECORD_OFFSET:TLS_RECORD_OFFSET + 80]
    record_magic, record_checksum = struct.unpack_from("<II", record)
    credential_bytes = record[8:80]
    generation, = struct.unpack_from("<I", credential_bytes)
    identity_raw = credential_bytes[4:36]
    secret_raw = credential_bytes[36:69]
    auth_mode = credential_bytes[69]
    fnv = 2166136261
    for byte in credential_bytes:
        fnv = ((fnv ^ byte) * 16777619) & 0xFFFFFFFF
    record_erased = record == b"\xff" * len(record)
    record_valid = (
        record_magic == 0x50534B31 and record_checksum == fnv and
        (auth_mode == 0 or (
            auth_mode == 1 and generation != 0 and
            0 < len(identity_raw.split(b"\0", 1)[0]) < 32 and
            len(secret_raw.split(b"\0", 1)[0]) == 32
        ))
    )

    result = {
        "page_sha256": hashlib.sha256(page).hexdigest(),
        "config": {
            "valid": config_valid,
            "magic": f"0x{magic:08X}",
            "version": version,
            "length": length,
            "checksum_match": checksum == config_checksum(raw),
            "vid": text(vid_raw),
            "mqtt_host_present": bool(text(mqtt_host_raw)),
            "mqtt_port": mqtt_port,
            "mqtt_username_present": bool(text(mqtt_username_raw)),
            "mqtt_password_present": bool(text(mqtt_password_raw)),
            "mqtt_tls_enabled": mqtt_tls_enabled,
            "mqtt_tls_profile_id": mqtt_tls_profile_id,
            "calibrated": calibrated,
            "steering_center_count": steering_center_count,
            "absolute_center_count": absolute_center_count,
            "steering_counts_per_degree_milli": steering_counts_per_degree_milli,
            "absolute_counts_per_degree_milli": absolute_counts_per_degree_milli,
            "steering_direction": steering_direction,
            "absolute_direction": absolute_direction,
            "steering_tolerance_mdeg": steering_tolerance_mdeg,
            "diag_log_enabled": diag_log_enabled,
            "diag_log_level": diag_log_level,
            "diag_log_port": diag_log_port,
            "ota_origin_present": bool(text(ota_origin_raw)),
        },
        "tls_credentials": {
            "record_erased": record_erased,
            "record_valid": record_valid,
            "record_magic": f"0x{record_magic:08X}",
            "checksum_match": record_checksum == fnv,
            "auth_mode": auth_mode,
            "generation_nonzero": generation != 0,
            "identity_present": bool(identity_raw.split(b"\0", 1)[0]),
            "secret_present": bool(secret_raw.split(b"\0", 1)[0]),
        },
    }
    output = Path(__file__).with_name("config-summary.json")
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

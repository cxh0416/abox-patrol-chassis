#!/usr/bin/env python3
"""Build a 2 KiB Config V5 page for enrollment or explicit per-device credentials."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import tempfile
from pathlib import Path

PAGE_SIZE = 0x800
CONFIG_OFFSET = 148
CONFIG_SIZE = 400
CONFIG_MAGIC = 0x56435543
CONFIG_VERSION = 5
DEFAULT_TLS_PROFILE_ID = 0
STEERING_COUNTS_MILLI = 2710800
ABSOLUTE_COUNTS_MILLI = 55250
DEFAULT_HOST = ""
DEFAULT_PORT = 0
DEFAULT_USERNAME = ""
DEFAULT_PASSWORD_ENV = "OTA_MQTT_PATROL_CHASSIS_DEVICE_PASSWORD"
DEFAULT_OTA_ORIGIN = "https://ota.zxwl.ntiov.com:20443"
VID_RE = re.compile(r"^[A-Za-z0-9._-]{1,31}$")


def _ascii(value: str, field: str, maximum: int, *, topic_component: bool = False) -> bytes:
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError(f"{field} must contain ASCII characters only") from exc
    if not encoded or len(encoded) >= maximum:
        raise ValueError(f"{field} length must be 1..{maximum - 1}")
    if any(ch < 0x21 or ch > 0x7E or ch in (ord('"'), ord('\\')) for ch in encoded):
        raise ValueError(f"{field} contains characters unsafe for the EC800 AT command")
    if topic_component and any(ch in b"/+#" for ch in encoded):
        raise ValueError(f"{field} contains an MQTT topic wildcard or separator")
    return encoded


def config_checksum(config: bytes) -> int:
    if len(config) != CONFIG_SIZE:
        raise ValueError("unexpected Patrol Config V5 size")
    checksum = 0x13572468
    for index, byte in enumerate(config):
        if 8 <= index < 12:
            continue
        checksum = (((checksum << 5) & 0xFFFFFFFF) ^ (checksum >> 27) ^ byte) & 0xFFFFFFFF
    return checksum


def build_config_page(
    *,
    vid: str,
    password: str = "",
    host: str = DEFAULT_HOST,
    port: int = DEFAULT_PORT,
    username: str = DEFAULT_USERNAME,
    diag_enabled: bool = False,
    diag_host: str = "36.153.162.171",
    diag_port: int = 30080,
    diag_level: int = 2,
    ota_origin: str = DEFAULT_OTA_ORIGIN,
    steering_center_count: int = 0,
    absolute_center_count: int = 0,
    steering_counts_per_degree_milli: int = STEERING_COUNTS_MILLI,
    absolute_counts_per_degree_milli: int = ABSOLUTE_COUNTS_MILLI,
    steering_direction: int = 1,
    absolute_direction: int = 1,
    steering_tolerance_mdeg: int = 2000,
    calibrated: bool = False,
    mqtt_tls_enabled: bool = True,
    mqtt_tls_profile_id: int = DEFAULT_TLS_PROFILE_ID,
) -> bytes:
    if not VID_RE.fullmatch(vid):
        raise ValueError("VID must match [A-Za-z0-9._-]{1,31}")
    if not 0 <= port <= 65535 or not 1 <= diag_port <= 65535:
        raise ValueError("port is out of range")
    if bool(host) != bool(port) or bool(host) != bool(username) or bool(host) != bool(password):
        raise ValueError("MQTT host, port, username and password must be all empty or all set")
    if diag_level not in range(4):
        raise ValueError("diag level must be 0..3")
    if steering_direction not in (-1, 1) or absolute_direction not in (-1, 1):
        raise ValueError("calibration directions must be -1 or 1")
    if not -(1 << 31) <= steering_center_count < (1 << 31):
        raise ValueError("steering center must fit int32")
    if not 0 <= absolute_center_count <= 0xFFFFFFFF:
        raise ValueError("absolute center must fit uint32")
    if not 0 <= steering_counts_per_degree_milli <= 0xFFFFFFFF:
        raise ValueError("steering counts per degree must fit uint32")
    if not 0 <= absolute_counts_per_degree_milli <= 0xFFFFFFFF:
        raise ValueError("absolute counts per degree must fit uint32")
    if not 1 <= steering_tolerance_mdeg <= 2000:
        raise ValueError("steering tolerance must be in 1..2000 mdeg")
    if (steering_counts_per_degree_milli != STEERING_COUNTS_MILLI or
        absolute_counts_per_degree_milli != ABSOLUTE_COUNTS_MILLI or
        steering_direction != 1 or absolute_direction != 1 or steering_tolerance_mdeg != 2000):
        raise ValueError("calibration profile must match the product profile")
    if calibrated and (steering_counts_per_degree_milli <= 0 or absolute_counts_per_degree_milli <= 0):
        raise ValueError("calibrated config requires steering and absolute counts per degree")
    if not 0 <= mqtt_tls_profile_id <= 255:
        raise ValueError("MQTT TLS profile ID must fit uint8")

    vid_b = _ascii(vid, "VID", 32, topic_component=True)
    host_b = _ascii(host, "MQTT host", 64) if host else b""
    username_b = _ascii(username, "MQTT username", 32) if username else b""
    password_b = _ascii(password, "MQTT password", 64) if password else b""
    diag_host_b = _ascii(diag_host, "diagnostic host", 64)
    origin_b = _ascii(ota_origin, "OTA origin", 96)
    if not ota_origin.startswith("https://"):
        raise ValueError("OTA origin must use https://")

    config = bytearray(CONFIG_SIZE)
    struct.pack_into(
        "<IHHIBBBB",
        config,
        0,
        CONFIG_MAGIC,
        CONFIG_VERSION,
        CONFIG_SIZE,
        0,
        0,  # Reserved former remote_enabled byte in Config V5.
        int(diag_enabled),
        diag_level,
        0,
    )
    config[16 : 16 + len(vid_b)] = vid_b
    config[48 : 48 + len(host_b)] = host_b
    struct.pack_into("<HH", config, 112, port, diag_port)
    config[116 : 116 + len(username_b)] = username_b
    config[148 : 148 + len(password_b)] = password_b
    config[212 : 212 + len(diag_host_b)] = diag_host_b
    config[276 : 276 + len(origin_b)] = origin_b
    struct.pack_into(
        "<iIIHbbBB",
        config,
        372,
        steering_center_count,
        absolute_center_count,
        absolute_counts_per_degree_milli,
        steering_tolerance_mdeg,
        steering_direction,
        absolute_direction,
        int(calibrated),
        0,
    )
    struct.pack_into("<I", config, 392, steering_counts_per_degree_milli)
    struct.pack_into("<BB", config, 396, int(mqtt_tls_enabled), mqtt_tls_profile_id)
    struct.pack_into("<I", config, 8, config_checksum(config))

    page = bytearray(b"\xFF" * PAGE_SIZE)
    page[CONFIG_OFFSET : CONFIG_OFFSET + CONFIG_SIZE] = config
    return bytes(page)


def inspect_config_page(page: bytes) -> dict[str, object]:
    if len(page) != PAGE_SIZE:
        raise ValueError("Config page must be exactly 2048 bytes")
    magic, version, length, checksum = struct.unpack_from("<IHHI", page, CONFIG_OFFSET)
    if magic != CONFIG_MAGIC or version != CONFIG_VERSION or length != CONFIG_SIZE:
        raise ValueError("Config header is invalid")
    config = page[CONFIG_OFFSET : CONFIG_OFFSET + length]
    if checksum != config_checksum(config):
        raise ValueError("Config checksum is invalid")

    def string_at(offset: int, size: int) -> str:
        return config[offset : offset + size].split(b"\0", 1)[0].decode("ascii")

    mqtt_port, diag_port = struct.unpack_from("<HH", config, 112)
    steering_center, absolute_center, absolute_counts_per_degree, tolerance, steering_direction, absolute_direction, calibrated, _ = struct.unpack_from(
        "<iIIHbbBB", config, 372
    )
    steering_counts_per_degree = struct.unpack_from("<I", config, 392)[0]
    return {
        "vid": string_at(16, 32),
        "mqttHost": string_at(48, 64),
        "mqttPort": mqtt_port,
        "mqttUsername": string_at(116, 32),
        "credentialProvisioned": bool(string_at(148, 64)),
        "remoteEnabled": bool(config[12]),
        "diagnosticEnabled": bool(config[13]),
        "diagnosticLevel": config[14],
        "diagnosticHost": string_at(212, 64),
        "diagnosticPort": diag_port,
        "otaOrigin": string_at(276, 96),
        "steeringCenterCount": steering_center,
        "absoluteCenterCount": absolute_center,
        "steeringCountsPerDegreeMilli": steering_counts_per_degree,
        "absoluteCountsPerDegreeMilli": absolute_counts_per_degree,
        "steeringToleranceMdeg": tolerance,
        "steeringDirection": steering_direction,
        "absoluteDirection": absolute_direction,
        "calibrated": bool(calibrated),
        "configVersion": version,
        "mqttTlsEnabled": bool(config[396]),
        "mqttTlsProfileId": config[397],
    }


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as handle:
        temp = Path(handle.name)
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temp, path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--username", default=DEFAULT_USERNAME)
    parser.add_argument("--password-env", default=DEFAULT_PASSWORD_ENV)
    parser.add_argument("--ota-origin", default=DEFAULT_OTA_ORIGIN)
    parser.add_argument("--steering-center-count", type=int, default=0)
    parser.add_argument("--absolute-center-count", type=int, default=0)
    parser.add_argument("--steering-counts-per-degree-milli", type=int, default=STEERING_COUNTS_MILLI)
    parser.add_argument("--absolute-counts-per-degree-milli", type=int, default=ABSOLUTE_COUNTS_MILLI)
    parser.add_argument("--steering-direction", type=int, choices=(-1, 1), default=1)
    parser.add_argument("--absolute-direction", type=int, choices=(-1, 1), default=1)
    parser.add_argument("--steering-tolerance-mdeg", type=int, default=2000)
    parser.add_argument("--calibrated", action="store_true")
    parser.add_argument("--mqtt-tls-enabled", action=argparse.BooleanOptionalAction, default=True,
                        help="use TLS by default; --no-mqtt-tls-enabled is for explicit legacy/debug use")
    parser.add_argument("--mqtt-tls-profile-id", type=int, default=DEFAULT_TLS_PROFILE_ID)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    if args.output.exists() and not args.force:
        parser.error(f"output already exists: {args.output}; use --force to replace it")
    password = os.environ.get(args.password_env, "") if args.host else ""
    if password and not (args.host and args.port and args.username):
        parser.error("explicit MQTT host, port and username are required with a password")
    try:
        page = build_config_page(
            vid=args.vid, password=password, host=args.host, port=args.port,
            username=args.username, ota_origin=args.ota_origin,
            steering_center_count=args.steering_center_count,
            absolute_center_count=args.absolute_center_count,
            steering_counts_per_degree_milli=args.steering_counts_per_degree_milli,
            absolute_counts_per_degree_milli=args.absolute_counts_per_degree_milli,
            steering_direction=args.steering_direction,
            absolute_direction=args.absolute_direction,
            steering_tolerance_mdeg=args.steering_tolerance_mdeg,
            calibrated=args.calibrated,
            mqtt_tls_enabled=args.mqtt_tls_enabled,
            mqtt_tls_profile_id=args.mqtt_tls_profile_id,
        )
        details = inspect_config_page(page)
    except ValueError as exc:
        parser.error(str(exc))

    _atomic_write(args.output, page)
    manifest = {
        "format": "patrol-chassis-config-v5",
        "flashAddress": "0x0803F800",
        "size": len(page),
        "sha256": hashlib.sha256(page).hexdigest(),
        **details,
    }
    manifest_path = args.output.with_suffix(args.output.suffix + ".manifest.json")
    _atomic_write(manifest_path, (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    print(f"Config written: {args.output}")
    print(f"Manifest: {manifest_path}")
    print(f"VID={details['vid']} broker={details['mqttHost']}:{details['mqttPort']} "
          f"credential={'<redacted>' if password else 'absent'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

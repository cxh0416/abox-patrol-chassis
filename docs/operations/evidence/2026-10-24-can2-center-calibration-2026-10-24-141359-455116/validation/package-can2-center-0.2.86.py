#!/usr/bin/env python3
"""Package the CAN2 local center-calibration firmware candidate."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VERSION = "patrol-chassis-0.2.86"
BUILD = ROOT / "out/logs/build-can2-center-0.2.86"
APP = BUILD / "Patrol_Vehicle_Chassis_App.bin"
ELF = BUILD / "Patrol_Vehicle_Chassis_App.elf"
MAP = BUILD / "Patrol_Vehicle_Chassis_App.map"
BOOT = ROOT / "platform/targets/stm32f105_abox_boot/dist/ABox_Boot.bin"
BOOT_SHA256 = "981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8", newline="\n")


def source_digest(path: Path) -> str:
    return sha256(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--candidate-stage", type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    stage = args.candidate_stage.resolve()
    if evidence.exists() or stage.exists():
        raise RuntimeError("refusing to overwrite an existing evidence or candidate stage")
    evidence.mkdir(parents=True)

    app = APP.read_bytes()
    if len(app) > 0x36000:
        raise RuntimeError("App exceeds its flash region")
    marker = f"PATROL_VEHICLE_CHASSIS_FW_VERSION={VERSION}".encode("ascii")
    if marker not in app:
        raise RuntimeError("App version marker mismatch")
    initial_sp, reset_vector = struct.unpack_from("<II", app)
    if not 0x20000000 <= initial_sp <= 0x20010000 or not reset_vector & 1:
        raise RuntimeError("invalid App vectors")

    boot = BOOT.read_bytes()
    if len(boot) != 0x8000 or sha256(BOOT) != BOOT_SHA256:
        raise RuntimeError("frozen Boot mismatch")
    full = boot + app

    files = {
        "ABox_Boot.bin": boot,
        "Patrol_Vehicle_Chassis_App.bin": app,
        "Patrol_Vehicle_Chassis_App.elf": ELF.read_bytes(),
        "Patrol_Vehicle_Chassis_App.map": MAP.read_bytes(),
        "Patrol_Vehicle_Chassis_Full.bin": full,
    }
    for name, data in files.items():
        (evidence / name).write_bytes(data)

    subprocess.run([
        sys.executable, str(ROOT / "tools/make_bootstate_image.py"),
        "--app", str(APP), "--version", VERSION,
        "--state-a", str(evidence / "BootState_A.bin"),
        "--state-b", str(evidence / "BootState_B.bin"),
    ], cwd=ROOT, check=True)
    state_a = (evidence / "BootState_A.bin").read_bytes()
    state_b = (evidence / "BootState_B.bin").read_bytes()
    if len(state_a) != 0x800 or len(state_b) != 0x800 or state_b != b"\xff" * 0x800:
        raise RuntimeError("BootState generation mismatch")

    app_crc = f"{zlib.crc32(app) & 0xFFFFFFFF:08X}"
    addresses = f"""patrol_vehicle_chassis / {VERSION} / CAN2 CENTER CALIBRATION CANDIDATE

写完三个文件后再复位：
1. Patrol_Vehicle_Chassis_Full.bin -> 0x08000000 ({len(full)} bytes)
2. BootState_A.bin -> 0x0803E800 (2048 bytes)
3. BootState_B.bin -> 0x0803F000 (2048 bytes)

已有匹配冻结 Boot 时可只写：
Patrol_Vehicle_Chassis_App.bin -> 0x08008000 ({len(app)} bytes)
但仍必须写入本包 BootState_A/B。

禁止整片擦除。保留：
- 0x0803E000..0x0803E7FF 配置恢复页
- 0x0803F800..0x0803FFFF Config V5 与故障日志页

App CRC32: {app_crc}
App SHA256: {sha256(APP)}
"""
    (evidence / "FLASH_ADDRESSES.txt").write_text(addresses, encoding="utf-8-sig", newline="\n")

    key_sources = [
        "app/Core/User/motion/src/patrol_can_remote.c",
        "app/Core/User/motion/src/center_calibration.c",
        "app/Core/User/motion/include/patrol_can_remote.h",
        "app/Core/User/motion/include/center_calibration.h",
        "docs/protocols/巡防车底盘本地CAN控制协议.md",
        "can2_center_calibration/can2_center_calibration.py",
        "can2_center_calibration/README.md",
        "can2_center_calibration/run_center_calibration.ps1",
    ]
    candidate_manifest = {
        "product": "patrol_vehicle_chassis",
        "version": VERSION,
        "candidate": True,
        "steering_executor_compiled": True,
        "drive_executor_compiled": True,
        "startup_permit": False,
        "actuation_enabled": True,
        "active_car": 0,
        "commit": subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True
        ).strip(),
        "dirty": bool(subprocess.check_output(
            ["git", "-C", str(ROOT), "status", "--porcelain"], text=True
        ).strip()),
        "app_start": "0x08008000",
        "app_end": "0x0803DFFF",
        "config_recovery_page": "0x0803E000",
        "config_page": "0x0803F800",
        "size": len(app),
        "sha256": sha256(APP),
        "source_sha256": {name: source_digest(ROOT / name) for name in key_sources},
    }
    stage.mkdir(parents=True)
    shutil.copy2(APP, stage / APP.name)
    write_json(stage / "candidate.manifest.json", candidate_manifest)

    hashes = {
        path.name: sha256(path)
        for path in sorted(evidence.iterdir())
        if path.is_file()
    }
    write_json(evidence / "sha256.json", hashes)
    flash_manifest = {
        "product": "patrol_vehicle_chassis",
        "version": VERSION,
        "candidate": True,
        "configuration": "Release",
        "active_car": 0,
        "actuation_enabled": True,
        "steering_executor_compiled": True,
        "drive_executor_compiled": True,
        "app": {
            "file": APP.name, "address": "0x08008000", "size": len(app),
            "initial_sp": f"0x{initial_sp:08X}", "reset_vector": f"0x{reset_vector:08X}",
            "crc32": app_crc, "sha256": sha256(APP),
        },
        "boot": {"file": BOOT.name, "address": "0x08000000", "size": len(boot),
                 "sha256": sha256(BOOT)},
        "full": {"file": "Patrol_Vehicle_Chassis_Full.bin", "address": "0x08000000",
                 "size": len(full), "sha256": hashlib.sha256(full).hexdigest()},
        "states": [
            {"file": "BootState_A.bin", "address": "0x0803E800", "size": 0x800,
             "sha256": hashlib.sha256(state_a).hexdigest()},
            {"file": "BootState_B.bin", "address": "0x0803F000", "size": 0x800,
             "sha256": hashlib.sha256(state_b).hexdigest()},
        ],
        "board_flashed": False,
        "field_calibration_verified": False,
    }
    write_json(evidence / "flash.manifest.json", flash_manifest)
    print(json.dumps(flash_manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

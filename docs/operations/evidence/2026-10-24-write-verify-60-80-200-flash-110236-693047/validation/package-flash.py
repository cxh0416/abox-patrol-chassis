#!/usr/bin/env python3
"""Build and verify the 0.2.85 Full/App/BootState flash bundle."""

from __future__ import annotations

import hashlib
import json
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[5]
EVIDENCE = Path(__file__).resolve().parents[1]
VERSION = "patrol-chassis-0.2.85"
APP = ROOT / "out/candidates/bench-drive/Patrol_Vehicle_Chassis_App.bin"
ELF = ROOT / "app/build/BenchSteering/Patrol_Vehicle_Chassis_App.elf"
MAP = ROOT / "app/build/BenchSteering/Patrol_Vehicle_Chassis_App.map"
BOOT = ROOT / "platform/targets/stm32f105_abox_boot/dist/ABox_Boot.bin"
CANDIDATE = ROOT / "out/candidates/bench-drive/candidate.manifest.json"
APP_START = 0x08008000
BOOT_SIZE = 0x8000
STATE_PAGE_SIZE = 0x800
BOOT_SHA256 = "981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def main() -> int:
    app = APP.read_bytes()
    boot = BOOT.read_bytes()
    elf = ELF.read_bytes()
    map_file = MAP.read_bytes()
    candidate = json.loads(CANDIDATE.read_text(encoding="utf-8"))

    assert candidate["version"] == VERSION
    assert candidate["actuation_enabled"] is True
    assert candidate["active_car"] == 0
    assert candidate["size"] == len(app)
    assert candidate["sha256"].lower() == sha256(app)
    assert len(app) <= 0x36000
    assert len(boot) == BOOT_SIZE
    assert sha256(boot) == BOOT_SHA256
    assert f"PATROL_VEHICLE_CHASSIS_FW_VERSION={VERSION}".encode("ascii") in app

    initial_sp, reset_vector = struct.unpack_from("<II", app)
    assert 0x20000000 <= initial_sp <= 0x20010000
    assert reset_vector & 1 and APP_START <= reset_vector <= 0x0803DFFF

    full = boot + app
    assert len(full) == BOOT_SIZE + len(app)
    assert struct.unpack_from("<II", full, BOOT_SIZE) == (initial_sp, reset_vector)

    destinations = {
        "Patrol_Vehicle_Chassis_App.bin": app,
        "Patrol_Vehicle_Chassis_App.elf": elf,
        "Patrol_Vehicle_Chassis_App.map": map_file,
        "ABox_Boot.bin": boot,
        "Patrol_Vehicle_Chassis_Full.bin": full,
    }
    for name, data in destinations.items():
        path = EVIDENCE / name
        if path.exists():
            raise RuntimeError(f"Refusing to overwrite existing evidence file: {path}")
        path.write_bytes(data)

    subprocess.run(
        [
            sys.executable,
            "-X",
            "utf8",
            str(ROOT / "tools/make_bootstate_image.py"),
            "--app",
            str(APP),
            "--version",
            VERSION,
            "--state-a",
            str(EVIDENCE / "BootState_A.bin"),
            "--state-b",
            str(EVIDENCE / "BootState_B.bin"),
        ],
        cwd=ROOT,
        check=True,
    )

    state_a = (EVIDENCE / "BootState_A.bin").read_bytes()
    state_b = (EVIDENCE / "BootState_B.bin").read_bytes()
    state_format = "<IHHIIBBBBII32sII32sIBB2sI"
    state = struct.unpack(state_format, state_a[: struct.calcsize(state_format)])
    assert len(state_a) == len(state_b) == STATE_PAGE_SIZE
    assert state[:3] == (0x42535632, 3, struct.calcsize(state_format))
    assert state[9:12] == (len(app), zlib.crc32(app) & 0xFFFFFFFF, VERSION.encode().ljust(32, b"\0"))
    assert zlib.crc32(state_a[:108]) & 0xFFFFFFFF == state[-1]
    assert state_b == b"\xff" * STATE_PAGE_SIZE

    app_crc = f"{zlib.crc32(app) & 0xFFFFFFFF:08X}"
    addresses = f"""patrol_vehicle_chassis / {VERSION} / CANDIDATE

推荐完整安装，写完三个文件后再复位：
1. Patrol_Vehicle_Chassis_Full.bin -> 0x08000000 ({len(full)} bytes)
2. BootState_A.bin -> 0x0803E800 ({len(state_a)} bytes)
3. BootState_B.bin -> 0x0803F000 ({len(state_b)} bytes)

Full 已包含 32 KiB 冻结 Boot 和 App，不要同时重复烧录独立 App/Boot。
已有匹配冻结 Boot 时，可用：
Patrol_Vehicle_Chassis_App.bin -> 0x08008000 ({len(app)} bytes)
但仍必须写入本包配套 BootState_A/B。

禁止整片擦除。保留：
- 0x0803E000..0x0803E7FF 配置恢复页
- 0x0803F800..0x0803FFFF Config V5 与故障日志页

构建配置：
- active_car=0，使用板上 Config V5 标定
- steering/drive executor enabled
- actuation enabled

验证：
- ARM Release、栈、CAN2 栈和 Flash/RAM 布局通过
- 写后核验与故障停机专项回归通过
- State A 已按本 App 大小、CRC 和版本生成；State B 为擦除页
- 未烧录，未进行实车运动验收
"""
    (EVIDENCE / "FLASH_ADDRESSES.txt").write_text(
        addresses, encoding="utf-8-sig", newline="\n"
    )

    files = [
        "ABox_Boot.bin",
        "BootState_A.bin",
        "BootState_B.bin",
        "Patrol_Vehicle_Chassis_App.bin",
        "Patrol_Vehicle_Chassis_Full.bin",
    ]
    hashes = {name: sha256((EVIDENCE / name).read_bytes()) for name in files}
    write_json(EVIDENCE / "sha256.json", hashes)

    manifest = {
        "product": "patrol_vehicle_chassis",
        "version": VERSION,
        "candidate": True,
        "configuration": "Release",
        "active_car": 0,
        "actuation_enabled": True,
        "steering_executor_compiled": True,
        "drive_executor_compiled": True,
        "app": {
            "file": "Patrol_Vehicle_Chassis_App.bin",
            "address": "0x08008000",
            "size": len(app),
            "initial_sp": f"0x{initial_sp:08X}",
            "reset_vector": f"0x{reset_vector:08X}",
            "crc32": app_crc,
            "sha256": hashes["Patrol_Vehicle_Chassis_App.bin"],
        },
        "boot": {
            "file": "ABox_Boot.bin",
            "address": "0x08000000",
            "size": len(boot),
            "sha256": hashes["ABox_Boot.bin"],
        },
        "full": {
            "file": "Patrol_Vehicle_Chassis_Full.bin",
            "address": "0x08000000",
            "size": len(full),
            "sha256": hashes["Patrol_Vehicle_Chassis_Full.bin"],
        },
        "states": [
            {
                "file": "BootState_A.bin",
                "address": "0x0803E800",
                "sequence": 1,
                "state": "CONFIRMED",
                "size": len(state_a),
                "sha256": hashes["BootState_A.bin"],
                "app_size": len(app),
                "app_crc32": app_crc,
            },
            {
                "file": "BootState_B.bin",
                "address": "0x0803F000",
                "state": "ERASED",
                "size": len(state_b),
                "sha256": hashes["BootState_B.bin"],
            },
        ],
        "boot_state_parser_verification": True,
        "board_flashed": False,
        "field_motion_verified": False,
    }
    write_json(EVIDENCE / "flash.manifest.json", manifest)
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Validate firmware sets and replace a single managed output, with rollback."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import uuid
import zlib

DESTINATIONS = {
    "dist": "release_pair",
    "out/candidates/bench-steering": "candidate",
    "out/candidates/bench-drive": "candidate",
    "out/debug/online": "debug",
    "platform/targets/gd32f105_abox_boot/dist": "gd_frozen_boot",
}
APP = "Patrol_Vehicle_Chassis_App.bin"
BOOT = "ABox_Boot.bin"
FULL = "Patrol_Vehicle_Chassis_Full.bin"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_path(root: Path, path: Path) -> Path:
    """Reject the root itself, escapes, and any existing reparse-point ancestor."""
    root = Path(os.path.abspath(root))
    path = Path(os.path.abspath(path))
    if path == root or not path.is_relative_to(root):
        raise ValueError(f"Path outside managed root: {path}")
    for part in [path, *path.parents]:
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError(f"Reparse point is not a managed path: {part}")
    return path


def checked_tree(root: Path, path: Path) -> list[Path]:
    path = checked_path(root, path)
    found = []
    if path.is_dir():
        for child in path.iterdir():
            found.append(checked_path(root, child))
            if child.is_dir():
                found.extend(checked_tree(root, child))
    return found


def remove_tree(root: Path, path: Path) -> None:
    checked_tree(root, path)
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def validate(directory: Path, kind: str) -> None:
    if kind == "release_pair":
        validate_release_pair(directory)
        return
    if kind == "gd_frozen_boot":
        if {p.name for p in directory.iterdir()} != {BOOT, "manifest.json"}:
            raise ValueError("Unexpected GD frozen Boot artifact set")
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8-sig"))
        verifier = (Path(__file__).resolve().parents[1] /
                    "platform/targets/gd32f105_abox_boot/tools/verify_boot_artifact.py")
        spec = importlib.util.spec_from_file_location("gd_boot_verifier", verifier)
        if spec is None or spec.loader is None:
            raise ValueError("Cannot load GD Boot verifier")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        actual = module.verify(directory / BOOT)
        required = {"manifest_version": 1, "target": "gd32f105_abox_boot",
                    "mcu": "GD32F105RCT6", "hardware_contract": "abox_gd32f105_ec800_v1",
                    "boot_version": "abox-boot-2.3.0-gd", "load_address": "0x08000000",
                    "app_address": "0x08008000", "boot_state_pages": ["0x0803E800", "0x0803F000"],
                    "config_page": "0x0803F800"}
        if any(manifest.get(key) != value for key, value in required.items()):
            raise ValueError("GD frozen Boot manifest identity or layout mismatch")
        if any(str(manifest.get(key, "")).lower() != str(actual[key]).lower()
               for key in ("size", "sha256", "crc32", "boot_version")):
            raise ValueError("GD frozen Boot manifest does not match image")
        return
    manifest_name = {"release": "release.manifest.json", "candidate": "candidate.manifest.json",
                     "debug": "debug.manifest.json"}[kind]
    expected = {APP, manifest_name} if kind == "candidate" else {APP, BOOT, FULL, manifest_name}
    if {p.name for p in directory.iterdir()} != expected:
        raise ValueError(f"Unexpected artifact set: {directory}")
    m = json.loads((directory / manifest_name).read_text(encoding="utf-8-sig"))
    app = (directory / APP).read_bytes()
    if not 8 <= len(app) <= 0x36000:
        raise ValueError("App exceeds 0x08008000..0x0803DFFF")
    sp, reset = struct.unpack_from("<II", app)
    if not (0x20000000 <= sp <= 0x20010000 and sp % 8 == 0 and
            reset & 1 and 0x08008000 <= (reset & ~1) < 0x08008000 + len(app)):
        raise ValueError("Invalid App vectors")
    version = m["app_version"] if kind == "debug" else m["version"]
    if ("PATROL_VEHICLE_CHASSIS_FW_VERSION=" + version).encode("ascii") not in app:
        raise ValueError("App version marker mismatch")
    if kind == "candidate":
        if m["product"] != "patrol_vehicle_chassis" or not m["candidate"] or m["startup_permit"]:
            raise ValueError("Invalid candidate identity or startup permit")
        if len(app) != m["size"] or digest(directory / APP) != m["sha256"].lower():
            raise ValueError("Candidate hash/length mismatch")
        return
    boot = (directory / BOOT).read_bytes()
    full = (directory / FULL).read_bytes()
    if not 8 <= len(boot) <= 0x8000 or len(full) != 0x8000 + len(app):
        raise ValueError("Boot/Full length mismatch")
    boot_sp, boot_reset = struct.unpack_from("<II", boot)
    if not (0x20000000 <= boot_sp <= 0x20010000 and boot_sp % 8 == 0 and
            boot_reset & 1 and 0x08000000 <= (boot_reset & ~1) < 0x08000000 + len(boot)):
        raise ValueError("Invalid Boot vectors")
    if full[:len(boot)] != boot or full[0x8000:] != app:
        raise ValueError("Full image does not contain the exact Boot and App")
    if kind == "debug":
        if m["kind"] != "online-debug" or m["full_length"] != len(full) or m["full_sha256"].lower() != digest(directory / FULL):
            raise ValueError("Debug manifest mismatch")
        return
    if (m["product"] != "patrol_vehicle_chassis" or m["configuration"] != "Release" or
            m["product_dirty"] or m["platform_dirty"] or m["startup_motion_permit"]):
        raise ValueError("Invalid formal release identity/build state")
    if (m["app_range"] != "0x08008000..0x0803DFFF" or
            m.get("config_recovery_page") != "0x0803E000" or
            m["boot_state_pages"] != ["0x0803E800", "0x0803F000"] or m["config_page"] != "0x0803F800"):
        raise ValueError("Flash layout mismatch")
    artifacts = m["artifacts"]
    if len(artifacts) != 3 or {a["name"] for a in artifacts} != {APP, BOOT, FULL}:
        raise ValueError("Incomplete release manifest")
    for entry in artifacts:
        p = directory / entry["name"]
        address = "0x08008000" if p.name == APP else "0x08000000"
        if (entry["length"] != p.stat().st_size or entry["sha256"].lower() != digest(p) or
                entry["crc32"].lower() != f"{zlib.crc32(p.read_bytes()):08x}" or
                entry["load_address"] != address or entry["includes_device_config"] or entry["includes_boot_state"]):
            raise ValueError(f"Release manifest mismatch: {p.name}")


def validate_release_pair(directory: Path) -> None:
    """Bind each board's App to the matching frozen Boot and the same source."""
    checked_tree(directory.parent, directory)
    if {p.name for p in directory.iterdir()} != {"v1", "v2"} or any(
            not p.is_dir() or p.is_symlink() for p in directory.iterdir()):
        raise ValueError("Release root must contain exactly the v1 and v2 directories")
    root = Path(__file__).resolve().parents[1]
    verifier = root / "platform/tools/publish_release.py"
    spec = importlib.util.spec_from_file_location("abox_release_verifier", verifier)
    if spec is None or spec.loader is None:
        raise ValueError("Cannot load shared release verifier")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    manifests = {}
    for board, contract_file in (("v1", "release_contract_v1.json"), ("v2", "release_contract.json")):
        variant = directory / board
        validate(variant, "release")
        contract = json.loads((root / "tools" / contract_file).read_text(encoding="utf-8"))
        module.validate(variant, contract)
        manifests[board] = json.loads((variant / "release.manifest.json").read_text(encoding="utf-8-sig"))
    v1, v2 = manifests["v1"], manifests["v2"]
    if not v2["version"].endswith("-st") or v1["version"] != v2["version"][:-3] + "-gd":
        raise ValueError("Release board versions do not match")
    for field in ("product", "configuration", "product_commit", "product_dirty", "platform_commit",
                  "platform_dirty", "startup_motion_permit", "steering_executor_compiled",
                  "drive_executor_compiled", "actuation_enabled", "power_output_enabled", "active_car",
                  "app_range", "config_recovery_page", "boot_state_pages", "config_page"):
        if field not in v2 or v1.get(field) != v2[field]:
            raise ValueError(f"Release variants disagree on {field}")
    v1_app = (directory / "v1" / APP).read_bytes()
    v2_app = (directory / "v2" / APP).read_bytes()
    if (b"ABOX_MCU=GD32F105RCT6\0" not in v1_app or b"ABOX_MCU=STM32F105RCT6\0" in v1_app or
            b"ABOX_MCU=STM32F105RCT6\0" not in v2_app or b"ABOX_MCU=GD32F105RCT6\0" in v2_app):
        raise ValueError("Release MCU marker mismatch")


def publish(root: Path, stage: Path, destination: str) -> None:
    if destination not in DESTINATIONS:
        raise ValueError("Unsupported output directory")
    root = root.absolute()
    target = checked_path(root, root / destination)
    stage = stage.absolute()
    checked_tree(stage.parent, stage)
    if stage == target or stage.is_relative_to(target) or target.is_relative_to(stage):
        raise ValueError("Stage and output overlap")
    checked_tree(root, target)
    validate(stage, DESTINATIONS[destination])
    target.parent.mkdir(parents=True, exist_ok=True)
    token = uuid.uuid4().hex
    incoming = target.with_name(f".{target.name}-incoming-{token}")
    backup = target.with_name(f".{target.name}-backup-{token}")
    # Exclusive lock prevents competing publishers from replacing each other's backup.
    lock = target.with_name(f".{target.name}-publish.lock")
    handle = lock.open("x")
    try:
        if DESTINATIONS[destination] == "gd_frozen_boot" and target.exists():
            validate(target, "gd_frozen_boot")
            if any((target / name).read_bytes() != (stage / name).read_bytes()
                   for name in (BOOT, "manifest.json")):
                raise ValueError("Refusing to replace an existing frozen GD Boot")
            return
        try:
            shutil.copytree(stage, incoming)
            validate(incoming, DESTINATIONS[destination])
            if target.exists():
                os.replace(target, backup)
            try:
                os.replace(incoming, target)
            except BaseException:
                if backup.exists():
                    os.replace(backup, target)
                raise
            if backup.exists():
                remove_tree(root, backup)
        finally:
            if incoming.exists():
                remove_tree(root, incoming)
    finally:
        handle.close()
        lock.unlink()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--destination", choices=DESTINATIONS, required=True)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    if args.validate_only:
        validate(args.stage, DESTINATIONS[args.destination])
    else:
        publish(args.root, args.stage, args.destination)


if __name__ == "__main__":
    main()

"""Validate firmware sets and replace a single managed output, with rollback."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import uuid
import zlib

DESTINATIONS = {
    "dist": "release",
    "out/candidates/bench-steering": "candidate",
    "out/candidates/bench-drive": "candidate",
    "out/debug/online": "debug",
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
    manifest_name = {"release": "release.manifest.json", "candidate": "candidate.manifest.json",
                     "debug": "debug.manifest.json"}[kind]
    expected = {APP, manifest_name} if kind == "candidate" else {APP, BOOT, FULL, manifest_name}
    if {p.name for p in directory.iterdir()} != expected:
        raise ValueError(f"Unexpected artifact set: {directory}")
    m = json.loads((directory / manifest_name).read_text(encoding="utf-8-sig"))
    app = (directory / APP).read_bytes()
    if not 8 <= len(app) <= 0x36800:
        raise ValueError("App exceeds 0x08008000..0x0803E7FF")
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
    if (m["app_range"] != "0x08008000..0x0803E7FF" or
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
    args = parser.parse_args()
    publish(args.root, args.stage, args.destination)


if __name__ == "__main__":
    main()

from __future__ import annotations

import json
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import artifact_store as store
import clean_workspace as clean


def candidate(path: Path, version: str = "patrol-chassis-0.2.16") -> None:
    path.mkdir(parents=True)
    data = struct.pack("<II", 0x20010000, 0x08008009) + (
        "PATROL_VEHICLE_CHASSIS_FW_VERSION=" + version).encode() + b"\0"
    (path / store.APP).write_bytes(data)
    (path / "candidate.manifest.json").write_text(json.dumps({
        "product": "patrol_vehicle_chassis", "version": version, "candidate": True,
        "startup_permit": False, "size": len(data), "sha256": store.digest(path / store.APP),
    }))


def release(path: Path, debug: bool = False) -> None:
    candidate(path)
    (path / "candidate.manifest.json").unlink()
    boot = struct.pack("<II", 0x20010000, 0x08000009) + b"boot" * 4
    app = (path / store.APP).read_bytes()
    (path / store.BOOT).write_bytes(boot)
    full = boot + bytes(0x8000 - len(boot)) + app
    (path / store.FULL).write_bytes(full)
    if debug:
        manifest = {"kind": "online-debug", "app_version": "patrol-chassis-0.2.16",
                    "full_length": len(full), "full_sha256": store.digest(path / store.FULL)}
    else:
        manifest = {"product": "patrol_vehicle_chassis", "version": "patrol-chassis-0.2.16",
                    "configuration": "Release", "product_dirty": False, "platform_dirty": False,
                    "startup_motion_permit": False, "app_range": "0x08008000..0x0803DFFF", "config_recovery_page": "0x0803E000",
                    "boot_state_pages": ["0x0803E800", "0x0803F000"], "config_page": "0x0803F800",
                    "artifacts": [{"name": name, "length": (path / name).stat().st_size,
                                   "sha256": store.digest(path / name),
                                   "crc32": f"{zlib.crc32((path / name).read_bytes()):08x}",
                                   "load_address": "0x08008000" if name == store.APP else "0x08000000",
                                   "includes_device_config": False, "includes_boot_state": False}
                                  for name in (store.APP, store.BOOT, store.FULL)]}
    name = "debug.manifest.json" if debug else "release.manifest.json"
    (path / name).write_text(json.dumps(manifest))


def gd_frozen_boot(path: Path) -> None:
    path.mkdir(parents=True)
    image = bytearray(0x8000)
    struct.pack_into("<II", image, 0, 0x20010000, 0x08000009)
    struct.pack_into("<IHHIHHH", image, 0x7F00, 0x32564241, 1, 256, 15, 2, 3, 0)
    image[0x7F14:0x7F14 + len(b"abox_gd32f105")] = b"abox_gd32f105"
    image[0x7F34:0x7F34 + len(b"abox-boot-2.3.0-gd")] = b"abox-boot-2.3.0-gd"
    struct.pack_into("<I", image, 0x7FFC, zlib.crc32(image[0x7F00:0x7FFC]))
    (path / store.BOOT).write_bytes(image)
    manifest = {"manifest_version": 1, "target": "gd32f105_abox_boot",
                "mcu": "GD32F105RCT6", "hardware_contract": "abox_gd32f105_ec800_v1",
                "boot_version": "abox-boot-2.3.0-gd", "load_address": "0x08000000",
                "app_address": "0x08008000", "boot_state_pages": ["0x0803E800", "0x0803F000"],
                "config_page": "0x0803F800", "size": len(image),
                "crc32": f"{zlib.crc32(image):08X}", "sha256": hashlib.sha256(image).hexdigest()}
    (path / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")


def release_pair(path: Path) -> None:
    platform = Path(__file__).resolve().parents[1] / "platform/targets"
    for board, target, mcu, version, boot_version, hardware in (
        ("v1", "gd32f105_abox_boot", "GD32F105RCT6", "patrol-chassis-0.2.16-gd", "abox-boot-2.3.0-gd", "gd32f105_ec800_v1"),
        ("v2", "stm32f105_abox_boot", "STM32F105RCT6", "patrol-chassis-0.2.16", "abox-boot-2.3.0", "stm32f105_ec800_v1"),
    ):
        variant = path / board
        variant.mkdir(parents=True)
        app = (struct.pack("<II", 0x20010000, 0x08008009) +
               ("PATROL_VEHICLE_CHASSIS_FW_VERSION=" + version).encode() + b"\0" +
               ("ABOX_MCU=" + mcu).encode() + b"\0")
        boot = (platform / target / "dist/ABox_Boot.bin").read_bytes()
        for name, data in ((store.APP, app), (store.BOOT, boot), (store.FULL, boot + app)):
            (variant / name).write_bytes(data)
        manifest = {"manifest_version": 3, "product": "patrol_vehicle_chassis", "version": version,
                    "configuration": "Release", "board": board, "hardware": hardware, "boot_version": boot_version,
                    "product_commit": "test-product", "platform_commit": "test-platform",
                    "product_dirty": False, "platform_dirty": False, "startup_motion_permit": False,
                    "steering_executor_compiled": True, "drive_executor_compiled": True,
                    "actuation_enabled": True, "power_output_enabled": True, "active_car": 0,
                    "app_range": "0x08008000..0x0803DFFF", "config_recovery_page": "0x0803E000",
                    "boot_state_pages": ["0x0803E800", "0x0803F000"], "config_page": "0x0803F800",
                    "artifacts": [{"name": name, "length": (variant / name).stat().st_size,
                                   "sha256": store.digest(variant / name),
                                   "crc32": f"{zlib.crc32((variant / name).read_bytes()):08x}",
                                   "load_address": "0x08008000" if name == store.APP else "0x08000000",
                                   "includes_device_config": False, "includes_boot_state": False}
                                  for name in (store.APP, store.BOOT, store.FULL)]}
        (variant / "release.manifest.json").write_text(json.dumps(manifest), encoding="utf-8")


class WorkspaceMaintenanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "docs/operations/evidence").mkdir(parents=True)

    def test_candidate_replaces_whole_set_and_rejects_invalid_stage(self):
        stage = self.root / "stage"
        candidate(stage)
        target = self.root / "out/candidates/bench-drive"
        target.mkdir(parents=True)
        (target / "old.bin").write_bytes(b"previous")
        store.publish(self.root, stage, "out/candidates/bench-drive")
        self.assertEqual({p.name for p in target.iterdir()}, {store.APP, "candidate.manifest.json"})
        original = store.digest(target / store.APP)
        (stage / store.APP).write_bytes(b"broken")
        with self.assertRaises(ValueError):
            store.publish(self.root, stage, "out/candidates/bench-drive")
        self.assertEqual(original, store.digest(target / store.APP))

    def test_failed_swap_restores_previous_set_and_unlocks(self):
        stage = self.root / "stage"
        candidate(stage)
        target = self.root / "out/candidates/bench-drive"
        target.mkdir(parents=True)
        (target / "old.bin").write_bytes(b"previous")
        replace = os.replace

        def fail_install(src, dst):
            if "incoming" in str(src):
                raise OSError("simulated Windows rename failure")
            return replace(src, dst)

        with patch.object(store.os, "replace", side_effect=fail_install):
            with self.assertRaises(OSError):
                store.publish(self.root, stage, "out/candidates/bench-drive")
        self.assertEqual((target / "old.bin").read_bytes(), b"previous")
        self.assertEqual(list(target.parent.iterdir()), [target])
        store.publish(self.root, stage, "out/candidates/bench-drive")

    def test_formal_package_layout_hash_and_dirty_gates(self):
        stage = self.root / "stage"
        release_pair(stage)
        store.publish(self.root, stage, "dist")
        target = self.root / "dist"
        self.assertEqual({p.name for p in target.iterdir()}, {"v1", "v2"})
        baseline = {p.relative_to(target).as_posix(): store.digest(p) for p in target.rglob("*") if p.is_file()}
        mp = stage / "v2/release.manifest.json"
        original = json.loads(mp.read_text())
        for field, value in [("config_page", "0x0803F000"), ("product_dirty", True),
                             ("configuration", "Debug")]:
            with self.subTest(field=field):
                mp.write_text(json.dumps(dict(original, **{field: value})))
                with self.assertRaises(ValueError):
                    store.publish(self.root, stage, "dist")
                self.assertEqual(baseline, {p.relative_to(target).as_posix(): store.digest(p) for p in target.rglob("*") if p.is_file()})
        mp.write_text(json.dumps(original))
        full = stage / "v1" / store.FULL
        broken = bytearray(full.read_bytes())
        broken[0x8000] ^= 1
        full.write_bytes(broken)
        with self.assertRaises(ValueError):
            store.publish(self.root, stage, "dist")
        self.assertEqual(baseline, {p.relative_to(target).as_posix(): store.digest(p) for p in target.rglob("*") if p.is_file()})

    def test_release_pair_rejects_wrong_board_boot_and_keeps_previous_release(self):
        stage = self.root / "stage"
        release_pair(stage)
        store.publish(self.root, stage, "dist")
        target = self.root / "dist"
        original = store.digest(target / "v1" / store.BOOT)
        v1 = stage / "v1"
        boot = (stage / "v2" / store.BOOT).read_bytes()
        app = (v1 / store.APP).read_bytes()
        (v1 / store.BOOT).write_bytes(boot)
        (v1 / store.FULL).write_bytes(boot + app)
        mp = v1 / "release.manifest.json"
        manifest = json.loads(mp.read_text(encoding="utf-8"))
        for entry in manifest["artifacts"]:
            data = (v1 / entry["name"]).read_bytes()
            entry.update(length=len(data), sha256=hashlib.sha256(data).hexdigest(), crc32=f"{zlib.crc32(data):08x}")
        mp.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "frozen Boot mismatch"):
            store.publish(self.root, stage, "dist")
        self.assertEqual(store.digest(target / "v1" / store.BOOT), original)

    def test_release_pair_rejects_extra_root_files_and_mismatched_policy(self):
        stage = self.root / "stage"
        release_pair(stage)
        store.publish(self.root, stage, "dist")
        extra = stage / "release.manifest.json"
        extra.write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "exactly the v1 and v2 directories"):
            store.publish(self.root, stage, "dist")
        extra.unlink()
        mp = stage / "v1/release.manifest.json"
        manifest = json.loads(mp.read_text(encoding="utf-8"))
        manifest["actuation_enabled"] = False
        mp.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "variants disagree on actuation_enabled"):
            store.publish(self.root, stage, "dist")
        self.assertEqual({p.name for p in (self.root / "dist").iterdir()}, {"v1", "v2"})

    def test_debug_output_stays_outside_formal_directory(self):
        stage = self.root / "stage"
        release(stage, debug=True)
        formal = self.root / "dist"
        formal.mkdir()
        (formal / "untouched").write_text("formal")
        store.publish(self.root, stage, "out/debug/online")
        self.assertEqual((formal / "untouched").read_text(), "formal")
        self.assertEqual(len(list((self.root / "out/debug/online").iterdir())), 4)

    def test_gd_frozen_boot_is_verified_and_cannot_be_silently_replaced(self):
        stage = self.root / "stage"
        gd_frozen_boot(stage)
        destination = "platform/targets/gd32f105_abox_boot/dist"
        store.publish(self.root, stage, destination)
        target = self.root / destination
        original = store.digest(target / store.BOOT)
        store.publish(self.root, stage, destination)  # identical replay is safe
        image = bytearray((stage / store.BOOT).read_bytes())
        image[0x100] = 1
        (stage / store.BOOT).write_bytes(image)
        manifest_path = stage / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest.update(sha256=hashlib.sha256(image).hexdigest(), crc32=f"{zlib.crc32(image):08X}")
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Refusing to replace"):
            store.publish(self.root, stage, destination)
        self.assertEqual(original, store.digest(target / store.BOOT))
        self.assertEqual({p.name for p in target.parent.iterdir()}, {"dist"})

        manifest["mcu"] = "STM32F105RCT6"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "identity or layout"):
            store.publish(self.root, stage, destination)
        self.assertEqual(original, store.digest(target / store.BOOT))

    def test_copy_corruption_leaves_previous_output(self):
        stage = self.root / "stage"
        candidate(stage)
        target = self.root / "out/candidates/bench-drive"
        target.mkdir(parents=True)
        (target / "old.bin").write_bytes(b"previous")
        copy = store.shutil.copytree

        def corrupt(src, dst):
            copy(src, dst)
            (dst / store.APP).write_bytes(b"corrupt copy")

        with patch.object(store.shutil, "copytree", side_effect=corrupt):
            with self.assertRaises(ValueError):
                store.publish(self.root, stage, "out/candidates/bench-drive")
        self.assertEqual((target / "old.bin").read_bytes(), b"previous")

    def test_boundary_and_windows_junction_are_rejected(self):
        with self.assertRaises(ValueError):
            store.remove_tree(self.root, self.root / "../outside")
        with self.assertRaises(ValueError):
            store.remove_tree(self.root, self.root)
        outside = self.root / "protected"
        outside.mkdir()
        (outside / "keep").write_text("keep")
        linked = self.root / "tmp"
        if os.name == "nt":
            subprocess.run(["pwsh", "-NoProfile", "-Command",
                            "New-Item -ItemType Junction -Path $env:MAINTENANCE_LINK -Target $env:MAINTENANCE_TARGET | Out-Null"],
                           env=dict(os.environ, MAINTENANCE_LINK=str(linked), MAINTENANCE_TARGET=str(outside)),
                           check=True, capture_output=True)
        else:
            linked.symlink_to(outside, target_is_directory=True)
        try:
            with self.assertRaises(ValueError):
                clean.make_plan(self.root, False, False)
            self.assertTrue((outside / "keep").exists())
        finally:
            if os.name == "nt":
                linked.rmdir()
            else:
                linked.unlink()

    def test_preview_archive_then_cleanup_is_repeatable(self):
        out = self.root / "out"
        out.mkdir()
        record = out / "trace.json"
        record.write_bytes(b'{"measured":123}\n')
        (out / "edit-once.py").write_text("# disposable")
        (self.root / "docs/operations/evidence/summary.json").write_text(
            '{"source":"out/trace.json","missing":"out/missing.json"}')
        candidate(out / "bench-patrol-chassis-0.2.9", "patrol-chassis-0.2.9")
        plan = clean.make_plan(self.root, True, False)
        self.assertTrue(record.exists())
        self.assertFalse((self.root / clean.ARCHIVE).exists())
        clean.execute(self.root, plan)
        index = clean.verify_archive(self.root)
        self.assertEqual(index["entries"][0]["sha256"], next(e["sha256"] for e in plan["files"] if e["path"] == "out/trace.json"))
        self.assertIn("out/missing.json", index["missing_references"])
        self.assertFalse(record.exists())
        self.assertTrue((out / "candidates/bench-steering" / store.APP).exists())
        second = clean.make_plan(self.root, True, False)
        clean.execute(self.root, second)
        self.assertEqual(second["files"], [])

    def test_archive_failure_preserves_originals(self):
        (self.root / "out").mkdir()
        record = self.root / "out/trace.json"
        record.write_text('{"measured":123}')
        plan = clean.make_plan(self.root, True, False)
        with patch.object(clean, "verify_archive", side_effect=ValueError("bad archive")):
            with self.assertRaises(ValueError):
                clean.execute(self.root, plan)
        self.assertTrue(record.exists())

    def test_evidence_in_logs_is_not_disposable(self):
        log = self.root / "out/logs/measured.jsonl"
        log.parent.mkdir(parents=True)
        log.write_text('{"rpm":12}')
        (self.root / "docs/operations/evidence/summary.json").write_text(
            '{"source":"out/logs/measured.jsonl"}')
        with self.assertRaises(ValueError):
            clean.make_plan(self.root, False, False)
        self.assertTrue(log.exists())

    def test_temp_version_only_preserves_logs_and_legacy_evidence_bytes(self):
        old = self.root / "temp-version" / "candidate.json"
        old.parent.mkdir()
        old.write_text('{"candidate":true}', encoding="utf-8")
        log = self.root / "out/logs/measured.jsonl"
        log.parent.mkdir(parents=True)
        log.write_text('{"rpm":12}', encoding="utf-8")
        evidence = self.root / "docs/operations/evidence/legacy.json"
        original = '{"note":"现场","source":"out/logs/measured.jsonl"}'.encode("gb18030")
        evidence.write_bytes(original)
        plan = clean.make_plan(self.root, False, False, temp_version_only=True)
        self.assertEqual(plan["delete"], ["temp-version"])
        self.assertTrue(old.exists())
        clean.execute(self.root, plan)
        self.assertFalse(old.exists())
        self.assertTrue(log.exists())
        self.assertEqual(evidence.read_bytes(), original)

    def test_root_build_only_preserves_app_and_platform_builds(self):
        for path in ("build/cache", "app/build/cache", "platform/build/cache"):
            file = self.root / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("rebuildable", encoding="utf-8")
        plan = clean.make_plan(self.root, False, True, root_build_only=True)
        self.assertEqual(plan["delete"], ["build"])
        clean.execute(self.root, plan)
        self.assertFalse((self.root / "build").exists())
        self.assertTrue((self.root / "app/build/cache").exists())
        self.assertTrue((self.root / "platform/build/cache").exists())

    def test_snapshot_change_and_protected_path_abort(self):
        (self.root / "tmp").mkdir()
        p = self.root / "tmp/file.txt"
        p.write_text("before")
        plan = clean.make_plan(self.root, False, False)
        p.write_text("after")
        with self.assertRaises(ValueError):
            clean.execute(self.root, plan)
        self.assertTrue(p.exists())
        with self.assertRaises(ValueError):
            clean.execute(self.root, {"delete": ["dist"], "files": []})

    def test_document_links_and_archived_evidence_references(self):
        root = Path(__file__).resolve().parents[1]
        index = clean.verify_archive(root)
        saved = {e["original_path"] for e in index["entries"]}
        declared_missing = set(index["missing_references"])
        for doc in [root / "README.md", *(root / "docs").rglob("*.md")]:
            if "archive" in doc.parts:
                continue  # Original vendor input stays byte-for-byte unchanged.
            relative = doc.relative_to(root).parts
            if relative[:3] == ("docs", "operations", "evidence") and len(relative) > 5 and relative[4] == "source":
                # Frozen source links refer to their ORIGINAL checkout location, not the evidence directory.
                # Verify immutable bytes against the package index instead of treating snapshots as live runbooks.
                package = root.joinpath(*relative[:4])
                snapshot_index = json.loads((package / "index.json").read_text(encoding="utf-8-sig"))
                name = doc.relative_to(package).as_posix()
                entry = next(item for item in snapshot_index["artifacts"] if item["path"] == name)
                self.assertEqual(hashlib.sha256(doc.read_bytes()).hexdigest(), entry["sha256"])
                continue
            for target in re.findall(r"\]\(([^)]+)\)", doc.read_text(encoding="utf-8-sig")):
                if "://" in target or target.startswith("#"):
                    continue
                with self.subTest(document=str(doc), target=target):
                    destination = (doc.parent / target.split("#")[0]).resolve()
                    old_tool = root / "can2_center_calibration/README.md"
                    if relative[:3] == ("docs", "operations", "evidence") and destination == old_tool:
                        # Frozen evidence retains its original link; the evidence index navigates to the moved tool.
                        destination = root / "tools/can2_center_calibration/README.md"
                    self.assertTrue(destination.exists())
        for summary in (root / "docs/operations/evidence").glob("*.json"):
            for reference in clean.references(summary.read_text(encoding="utf-8-sig")):
                with self.subTest(summary=summary.name, reference=reference):
                    self.assertTrue((root / reference).exists() or reference in saved or
                                    reference in declared_missing or
                                    any(p.startswith(reference.rstrip("/") + "/") for p in saved))


if __name__ == "__main__":
    unittest.main()

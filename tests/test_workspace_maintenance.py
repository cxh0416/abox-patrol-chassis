from __future__ import annotations

import json
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


def candidate(path: Path, version: str = "patrol-chassis-0.2.15") -> None:
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
        manifest = {"kind": "online-debug", "app_version": "patrol-chassis-0.2.15",
                    "full_length": len(full), "full_sha256": store.digest(path / store.FULL)}
    else:
        manifest = {"product": "patrol_vehicle_chassis", "version": "patrol-chassis-0.2.15",
                    "configuration": "Release", "product_dirty": False, "platform_dirty": False,
                    "startup_motion_permit": False, "app_range": "0x08008000..0x0803E7FF",
                    "boot_state_pages": ["0x0803E800", "0x0803F000"], "config_page": "0x0803F800",
                    "artifacts": [{"name": name, "length": (path / name).stat().st_size,
                                   "sha256": store.digest(path / name),
                                   "crc32": f"{zlib.crc32((path / name).read_bytes()):08x}",
                                   "load_address": "0x08008000" if name == store.APP else "0x08000000",
                                   "includes_device_config": False, "includes_boot_state": False}
                                  for name in (store.APP, store.BOOT, store.FULL)]}
    name = "debug.manifest.json" if debug else "release.manifest.json"
    (path / name).write_text(json.dumps(manifest))


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
        release(stage)
        store.publish(self.root, stage, "dist")
        target = self.root / "dist"
        baseline = {p.name: store.digest(p) for p in target.iterdir()}
        mp = stage / "release.manifest.json"
        original = json.loads(mp.read_text())
        for field, value in [("config_page", "0x0803F000"), ("product_dirty", True),
                             ("configuration", "Debug")]:
            with self.subTest(field=field):
                mp.write_text(json.dumps(dict(original, **{field: value})))
                with self.assertRaises(ValueError):
                    store.publish(self.root, stage, "dist")
                self.assertEqual(baseline, {p.name: store.digest(p) for p in target.iterdir()})
        mp.write_text(json.dumps(original))
        full = stage / store.FULL
        broken = bytearray(full.read_bytes())
        broken[0x8000] ^= 1
        full.write_bytes(broken)
        with self.assertRaises(ValueError):
            store.publish(self.root, stage, "dist")
        self.assertEqual(baseline, {p.name: store.digest(p) for p in target.iterdir()})

    def test_debug_output_stays_outside_formal_directory(self):
        stage = self.root / "stage"
        release(stage, debug=True)
        formal = self.root / "dist"
        formal.mkdir()
        (formal / "untouched").write_text("formal")
        store.publish(self.root, stage, "out/debug/online")
        self.assertEqual((formal / "untouched").read_text(), "formal")
        self.assertEqual(len(list((self.root / "out/debug/online").iterdir())), 4)

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
            for target in re.findall(r"\]\(([^)]+)\)", doc.read_text(encoding="utf-8-sig")):
                if "://" in target or target.startswith("#"):
                    continue
                with self.subTest(document=str(doc), target=target):
                    self.assertTrue((doc.parent / target.split("#")[0]).exists())
        for summary in (root / "docs/operations/evidence").glob("*.json"):
            for reference in clean.references(summary.read_text(encoding="utf-8-sig")):
                with self.subTest(summary=summary.name, reference=reference):
                    self.assertTrue((root / reference).exists() or reference in saved or
                                    reference in declared_missing or
                                    any(p.startswith(reference.rstrip("/") + "/") for p in saved))


if __name__ == "__main__":
    unittest.main()

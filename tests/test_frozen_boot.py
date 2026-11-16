"""Verify the two board-specific frozen Boot images and their identities."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import artifact_store


class FrozenBootTests(unittest.TestCase):
    def test_gd_v1_frozen_identity_and_hash(self):
        target = ROOT / "platform/targets/gd32f105_abox_boot/dist"
        artifact_store.validate(target, "gd_frozen_boot")
        manifest = json.loads((target / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["sha256"],
                         "3eb4df8bda5c8a99c338dfe93a289acd43a4b94bf432b5785ec10ea90340f237")

    def test_stm_v2_frozen_identity_and_release_contract(self):
        target = ROOT / "platform/targets/stm32f105_abox_boot"
        image = target / "dist/ABox_Boot.bin"
        subprocess.run([sys.executable, str(target / "tools/verify_boot_artifact.py"), str(image)],
                       check=True, capture_output=True, text=True)
        manifest = json.loads((target / "dist/manifest.json").read_text(encoding="utf-8-sig"))
        contract = json.loads((ROOT / "tools/release_contract.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["mcu"], "STM32F105RCT6")
        self.assertEqual(hashlib.sha256(image.read_bytes()).hexdigest(), manifest["sha256"])
        self.assertEqual(manifest["sha256"].lower(), contract["frozen_boot_sha256"].lower())


if __name__ == "__main__":
    unittest.main()

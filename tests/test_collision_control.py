from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from product_layout import include_args

ROOT = Path(__file__).resolve().parents[1]


class CollisionControlTests(unittest.TestCase):
    def test_real_control_and_enabled_zero_hold(self):
        with tempfile.TemporaryDirectory(prefix="patrol-collision-") as directory:
            binary = Path(directory) / "collision.exe"
            command = [shutil.which("gcc") or "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                       *include_args(), str(ROOT / "tests/collision_control_cases.c"),
                       *[str(ROOT / "app/Core/User/motion/src" / name) for name in
                         ("chassis_control.c", "bench_drive.c", "bench_steering.c", "steering_angle.c",
                          "patrol_canopen.c")], "-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

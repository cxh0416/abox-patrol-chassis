"""Exercise autonomous return through real product control and runtime code."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from product_layout import include_args

ROOT = Path(__file__).resolve().parents[1]


class AutoCenterRuntimeTests(unittest.TestCase):
    def test_idle_and_expired_control_return(self):
        with tempfile.TemporaryDirectory(prefix="patrol-auto-center-") as directory:
            binary = Path(directory) / "auto_center.exe"
            sources = ["center_calibration.c", "patrol_runtime.c", "bench_steering.c",
                       "bench_drive.c", "steering_angle.c", "patrol_canopen.c",
                       "chassis_control.c", "patrol_auto_center.c"]
            result = subprocess.run([
                shutil.which("gcc") or "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-format", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-DPATROL_STEERING_EXECUTOR_ENABLED=1", "-DPATROL_DRIVE_EXECUTOR_ENABLED=1",
                "-DPATROL_CHASSIS_ACTUATION_ENABLED=1", *include_args(private=("motion",)),
                "-I" + str(ROOT / "shared/include"), str(ROOT / "tests/auto_center_runtime_cases.c"),
                *[str(ROOT / "app/Core/User/motion/src" / name) for name in sources],
                "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

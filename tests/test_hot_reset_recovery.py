"""Verify hot-reset shutdown recovery through the production runtime."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from product_layout import include_args

ROOT = Path(__file__).resolve().parents[1]


class HotResetRecoveryTests(unittest.TestCase):
    def run_case(self, actuation):
        with tempfile.TemporaryDirectory(prefix="patrol-hot-reset-") as directory:
            binary = Path(directory) / "recovery.exe"
            sources = ["center_calibration.c", "patrol_runtime.c", "bench_steering.c",
                       "bench_drive.c", "steering_angle.c", "patrol_canopen.c",
                       "chassis_control.c", "patrol_auto_center.c", "patrol_fault_stop.c"]
            result = subprocess.run([
                shutil.which("gcc") or "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-format", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-DPATROL_STEERING_EXECUTOR_ENABLED=1", "-DPATROL_DRIVE_EXECUTOR_ENABLED=1",
                f"-DPATROL_CHASSIS_ACTUATION_ENABLED={actuation}", *include_args(private=("motion",)),
                "-I" + str(ROOT / "shared/include"), str(ROOT / "tests/hot_reset_recovery_cases.c"),
                *[str(ROOT / "app/Core/User/motion/src" / name) for name in sources],
                "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_enabled_shutdown_and_mailbox_fairness(self):
        self.run_case(1)

    def test_disabled_build_cannot_write(self):
        self.run_case(0)

"""Regress the observed old-center startup/calibration deadlock with real C control."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from product_layout import include_args

ROOT = Path(__file__).resolve().parents[1]


class StartupCalibrationRuntimeTests(unittest.TestCase):
    def test_old_center_bootstrap_maintenance_and_range_protection(self):
        sources = ["center_calibration.c", "patrol_runtime.c", "bench_steering.c",
                   "bench_drive.c", "steering_angle.c", "patrol_canopen.c",
                   "chassis_control.c", "patrol_auto_center.c", "patrol_fault_stop.c"]
        with tempfile.TemporaryDirectory(prefix="patrol-startup-calibration-") as directory:
            binary = Path(directory) / "startup.exe"
            command = [shutil.which("gcc") or "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-Wno-format", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                       "-DPATROL_STEERING_EXECUTOR_ENABLED=1", "-DPATROL_DRIVE_EXECUTOR_ENABLED=1",
                       "-DPATROL_CHASSIS_ACTUATION_ENABLED=1", *include_args(private=("motion",)),
                       "-I" + str(ROOT / "shared/include"),
                       str(ROOT / "tests/startup_calibration_runtime_cases.c"),
                       *[str(ROOT / "app/Core/User/motion/src" / source) for source in sources],
                       "-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

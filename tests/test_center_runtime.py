"""Compile the complete production calibration and runtime modules with host ports."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CenterRuntimeTests(unittest.TestCase):
    def test_production_center_runtime(self):
        gcc = shutil.which("gcc")
        self.assertIsNotNone(gcc, "host GCC is required for safety regression tests")
        with tempfile.TemporaryDirectory(prefix="patrol-center-host-") as directory:
            binary = Path(directory) / "center_runtime.exe"
            sources = ["center_calibration.c", "patrol_runtime.c", "bench_steering.c",
                       "bench_drive.c", "steering_angle.c", "patrol_canopen.c"]
            command = [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-format",
                       "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                       "-DPATROL_CHASSIS_BENCH_STEERING_ENABLED=1",
                       "-I" + str(ROOT / "app/Core/User/Inc"),
                       "-I" + str(ROOT / "app/Core/User/Src"),
                       "-I" + str(ROOT / "shared/include"),
                       str(ROOT / "tests/center_runtime_cases.c"),
                       *[str(ROOT / "app/Core/User/Src" / name) for name in sources],
                       "-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

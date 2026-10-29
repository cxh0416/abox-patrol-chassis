"""Exercise the traction-only brake through production control and executors."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from product_layout import include_args

ROOT = Path(__file__).resolve().parents[1]


class DriveBrakeRuntimeTests(unittest.TestCase):
    def test_production_drive_brake(self):
        gcc = shutil.which('gcc')
        self.assertIsNotNone(gcc)
        sources = ['center_calibration.c', 'patrol_runtime.c', 'bench_steering.c',
                   'bench_drive.c', 'steering_angle.c', 'patrol_canopen.c',
                   'patrol_auto_center.c', 'chassis_control.c', 'patrol_fault_stop.c']
        with tempfile.TemporaryDirectory(prefix='patrol-brake-host-') as directory:
            binary = Path(directory) / 'drive_brake.exe'
            command = [gcc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-format',
                       '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                       '-DPATROL_STEERING_EXECUTOR_ENABLED=1',
                       '-DPATROL_DRIVE_EXECUTOR_ENABLED=1',
                       '-DPATROL_CHASSIS_ACTUATION_ENABLED=1',
                       *include_args(private=('motion',)),
                       '-I' + str(ROOT / 'shared/include'),
                       str(ROOT / 'tests/drive_brake_runtime_cases.c'),
                       *[str(ROOT / 'app/Core/User/motion/src' / name) for name in sources],
                       '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from product_layout import include_args

ROOT = Path(__file__).resolve().parents[1]


class StopSchedulerTests(unittest.TestCase):
    def test_production_stop_arbitration(self):
        with tempfile.TemporaryDirectory() as directory:
            path = ROOT / 'tests/stop_scheduler_cases.c'
            binary = Path(directory) / 'stop.exe'
            sources = ['bench_steering.c', 'bench_drive.c', 'steering_angle.c',
                       'patrol_canopen.c', 'patrol_diagnostics.c', 'patrol_runtime.c', 'center_calibration.c', 'patrol_auto_center.c', 'patrol_fault_stop.c']
            result = subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-Wall',
                '-Wextra', '-Werror', '-Wno-format', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                '-DPATROL_CHASSIS_ACTUATION_ENABLED=1', '-I' + str(ROOT / 'app/Core/User/motion/private'), '-I' + str(ROOT / 'shared/include'), *include_args(), str(path),
                *[str(ROOT / 'app/Core/User/motion/src' / s) for s in sources], '-o', str(binary)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

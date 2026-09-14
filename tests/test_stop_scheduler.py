from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StopSchedulerTests(unittest.TestCase):
    def test_production_stop_arbitration(self):
        with tempfile.TemporaryDirectory() as directory:
            path = ROOT / 'tests/stop_scheduler_cases.c'
            binary = Path(directory) / 'stop.exe'
            sources = ['bench_steering.c', 'bench_drive.c', 'steering_angle.c',
                       'patrol_canopen.c', 'patrol_diagnostics.c', 'patrol_runtime.c', 'center_calibration.c']
            result = subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-Wall',
                '-Wextra', '-Werror', '-Wno-format', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-I' + str(ROOT / 'app/Core/User/Src'), '-I' + str(ROOT / 'shared/include'), '-I' + str(ROOT / 'app/Core/User/Inc'), str(path),
                *[str(ROOT / 'app/Core/User/Src' / s) for s in sources], '-o', str(binary)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

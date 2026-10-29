from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from product_layout import include_args

ROOT = Path(__file__).resolve().parents[1]


class CanRemoteTests(unittest.TestCase):
    def test_real_can2_and_control_service(self):
        gcc = shutil.which('gcc')
        self.assertIsNotNone(gcc)
        with tempfile.TemporaryDirectory(prefix='patrol-can2-host-') as directory:
            binary = Path(directory) / 'can_remote.exe'
            command = [gcc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-format',
                       '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                       '-DPATROL_CHASSIS_ACTUATION_ENABLED=1',
                       '-I' + str(ROOT / 'tests/bench_stubs'), *include_args(),
                       '-I' + str(ROOT / 'shared/include'),
                       '-I' + str(ROOT / 'platform/components/cjson'),
                       *['-I' + str(p) for p in (ROOT / 'platform/components').glob('*/include')],
                       str(ROOT / 'tests/can_remote_test.c'),
                       str(ROOT / 'app/Core/User/motion/src/patrol_can_remote.c'),
                       str(ROOT / 'app/Core/User/motion/src/patrol_phy_remote.c'),
                       str(ROOT / 'app/Core/User/motion/src/chassis_control.c'),
                       str(ROOT / 'app/Core/User/cloud/src/remote_service.c'),
                       str(ROOT / 'app/Core/User/common/src/fixed_point.c'),
                       str(ROOT / 'app/Core/User/board/src/vehicle_power_output.c'), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

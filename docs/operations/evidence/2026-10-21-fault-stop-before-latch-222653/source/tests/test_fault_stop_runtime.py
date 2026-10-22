"""Fault-stop regression through real control service, runtime and both motor executors."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from product_layout import include_args

ROOT = Path(__file__).resolve().parents[1]


class FaultStopRuntimeTests(unittest.TestCase):
    def test_real_fault_stop_before_latch(self):
        sources = ['patrol_runtime.c', 'patrol_fault_stop.c', 'center_calibration.c',
                   'chassis_control.c', 'bench_steering.c', 'bench_drive.c',
                   'steering_angle.c', 'patrol_canopen.c', 'patrol_auto_center.c']
        with tempfile.TemporaryDirectory(prefix='patrol-fault-stop-') as directory:
            binary = Path(directory) / 'fault_stop.exe'
            command = [shutil.which('gcc') or 'gcc', '-std=c11', '-Wall', '-Wextra',
                       '-Werror', '-Wno-format', '-ffunction-sections', '-fdata-sections',
                       '-Wl,--gc-sections', '-DPATROL_CHASSIS_ACTUATION_ENABLED=1',
                       '-DPATROL_STEERING_EXECUTOR_ENABLED=1', '-DPATROL_DRIVE_EXECUTOR_ENABLED=1',
                       *include_args(private=('motion',)), '-I'+str(ROOT/'tests/bench_stubs'),
                       '-I'+str(ROOT/'shared/include'), '-I'+str(ROOT/'platform/components/cjson'),
                       *['-I'+str(p) for p in (ROOT/'platform/components').glob('*/include')],
                       str(ROOT/'tests/fault_stop_runtime_cases.c'),
                       *[str(ROOT/'app/Core/User/motion/src'/name) for name in sources],
                       str(ROOT/'app/Core/User/cloud/src/remote_service.c'),
                       str(ROOT/'app/Core/User/cloud/src/cloud_protocol.c'),
                       str(ROOT/'app/Core/User/cloud/src/chassis_cloud_protocol.c'),
                       str(ROOT/'app/Core/User/common/src/fixed_point.c'),
                       str(ROOT/'platform/components/cloud/src/abox_cloud_internal_v2.c'),
                       str(ROOT/'platform/components/mqtt/src/abox_mqtt_digest.c'),
                       str(ROOT/'platform/components/mqtt/src/abox_sha256.c'),
                       str(ROOT/'platform/components/mqtt/src/abox_mqtt_v4.c'),
                       str(ROOT/'platform/components/ec800/src/abox_tls_secret_record.c'),
                       str(ROOT/'platform/components/cjson/cJSON.c'),
                       '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            print(result.stdout, end='')

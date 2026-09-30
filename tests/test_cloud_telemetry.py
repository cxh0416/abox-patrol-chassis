from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from product_layout import include_args

ROOT = Path(__file__).resolve().parents[1]


class TelemetryTests(unittest.TestCase):
    def test_production_transmit_scheduler(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'test.exe'
            subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-Wall', '-Wextra',
                            '-Werror', *include_args(),
                            '-I' + str(ROOT / 'tests/bench_stubs'),
                            '-I' + str(ROOT / 'shared/include'),
                            '-I' + str(ROOT / 'app/Core/User/cloud/private'),
                            '-I' + str(ROOT / 'platform/components/core/include'),
                            '-I' + str(ROOT / 'platform/components/ec800/include'),
                            '-I' + str(ROOT / 'platform/components/mqtt/include'),
                            '-I' + str(ROOT / 'platform/components/cloud/include'),
                            str(ROOT / 'tests/cloud_telemetry_scheduler_cases.c'),
                            str(ROOT / 'app/Core/User/cloud/src/cloud_tx.c'),
                            str(ROOT / 'platform/components/cloud/src/abox_cloud_service.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_activity_expiry_and_tick_wrap(self):
        code = r'''
#include <assert.h>
#include "cloud_telemetry.h"
int main(void) {
    CloudTelemetry s = {0};
    assert(!CloudTelemetry_Update(&s, 0, 0) && !s.fast);
    CloudTelemetry_Wake(&s, 120000);
    assert(CloudTelemetry_Update(&s, 120000, 0) && s.fast);
    assert(!CloudTelemetry_Update(&s, 149999, 0) && s.fast);
    assert(!CloudTelemetry_Update(&s, 150000, 0) && !s.fast);
    /* An active calibration stays fast without dependence on browser packets. */
    assert(CloudTelemetry_Update(&s, 200000, 1));
    CloudTelemetry_Update(&s, 900000, 1);
    CloudTelemetry_Update(&s, 929999, 0);
    assert(s.fast);
    CloudTelemetry_Update(&s, 930000, 0);
    assert(!s.fast);
    CloudTelemetry_Wake(&s, UINT32_MAX - 10000U);
    assert(CloudTelemetry_Update(&s, 1000, 0) && s.fast);
    CloudTelemetry_Update(&s, 20000, 0);
    assert(!s.fast);
    /* An expired lease must not revive on a later tick wrap. */
    CloudTelemetry_Update(&s, UINT32_MAX - 10000U, 0);
    assert(!s.fast);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            binary = Path(directory) / 'test.exe'
            source.write_text(code, encoding='utf-8')
            subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-Wall', '-Wextra',
                            '-Werror', *include_args(),
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

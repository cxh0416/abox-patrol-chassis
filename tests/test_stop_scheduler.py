from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StopSchedulerTests(unittest.TestCase):
    def test_production_stop_arbitration(self):
        source = (ROOT / 'app/Core/User/Src/vcu_rtos.c').read_text(encoding='utf-8')
        start = source.index('            /* Alternate execution and diagnostics, never overlap pending transactions. */')
        end = source.index('            taskEXIT_CRITICAL();', start)
        wrapper = ('static uint8_t take(uint32_t now, PatrolCanFrame* out) {\n'
                   'uint8_t have_request=0, bench_frame=0; PatrolCanFrame request;\n' +
                   source[start:end] + '\n(void)bench_frame; *out=request; return have_request; }\n')
        tests = (ROOT / 'tests/stop_scheduler_cases.c').read_text(encoding='utf-8')
        code = tests.replace('/* PRODUCTION_SCHEDULER */', wrapper)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'stop.c'
            binary = Path(directory) / 'stop.exe'
            path.write_text(code, encoding='utf-8')
            sources = ['bench_steering.c', 'bench_drive.c', 'steering_angle.c',
                       'patrol_canopen.c', 'patrol_diagnostics.c']
            result = subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-Wall',
                '-Wextra', '-Werror', '-I' + str(ROOT / 'app/Core/User/Inc'), str(path),
                *[str(ROOT / 'app/Core/User/Src' / s) for s in sources], '-o', str(binary)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

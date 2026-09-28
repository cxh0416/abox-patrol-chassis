"""Exercise the same Bus-Off recovery state machine used by the MCU."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CanRecoveryTests(unittest.TestCase):
    def test_recovery_needs_new_stable_feedback(self):
        gcc = shutil.which("gcc")
        self.assertIsNotNone(gcc, "host GCC is required for safety regression tests")
        source = r'''
#include <assert.h>
#include "patrol_can_recovery.h"
int main(void) {
    PatrolCanRecovery state = {0};
    assert(!PatrolCanRecovery_Observe(&state, 100, 90, 100, 1));
    assert(!PatrolCanRecovery_Observe(&state, 110, 101, 100, 1));
    assert(!PatrolCanRecovery_Observe(&state, 120, 102, 100, 1));
    assert(!PatrolCanRecovery_Observe(&state, 130, 103, 100, 1));
    assert(!PatrolCanRecovery_Observe(&state, 1090, 103, 100, 0));
    assert(state.samples == 3);
    assert(!PatrolCanRecovery_Observe(&state, 1100, 104, 100, 1));
    assert(PatrolCanRecovery_Observe(&state, 1110, 105, 100, 1));
    PatrolCanRecovery_Reset(&state); /* Power loss or another Bus-Off. */
    assert(!PatrolCanRecovery_Observe(&state, 1200, 106, 100, 1));
    assert(!PatrolCanRecovery_Observe(&state, 1210, 107, 100, 1));
    assert(!PatrolCanRecovery_Observe(&state, 1220, 108, 100, 1));
    assert(!PatrolCanRecovery_Observe(&state, 2199, 108, 100, 1));
    assert(PatrolCanRecovery_Observe(&state, 2200, 109, 100, 1));
    PatrolCanRecovery_Reset(&state);
    assert(!PatrolCanRecovery_Observe(&state, 2300, 109, 2300, 1));
    assert(!PatrolCanRecovery_Observe(&state, 2310, 2301, 2300, 1));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="patrol-can-recovery-") as directory:
            source_path = Path(directory) / "recovery.c"
            binary = Path(directory) / "recovery.exe"
            source_path.write_text(source, encoding="utf-8", newline="\n")
            built = subprocess.run([gcc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I" + str(ROOT / "app/Core/User/system/include"), str(source_path),
                "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

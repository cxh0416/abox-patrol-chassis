"""The encoder stack gate must use the current UART implementation."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools/verify_encoder_stack.py"
COMMON = {
    "Patrol_EncoderTask", "ABoxSerialTransaction_Begin", "ABoxUart_SendBlocking",
    "ABoxUart_StartRx", "ABoxDmaResource_Claim", "ABoxCritical_Enter",
    "ABoxUart_AbortRx", "stop_rx", "ABoxDmaResource_Release",
    "ABoxSerialTransaction_Init", "ABoxUart_SetEvents", "PatrolRuntime_BeginEncoderCycle",
    "PatrolDiagnostics_Snapshot", "ABoxSerialTransaction_Take", "ABoxUart_Irq",
    "finish_rx", "emit_rx", "on_rx",
}


class EncoderStackCheckerTests(unittest.TestCase):
    def run_checker(self, target, *, missing=None, oversized=False):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "CMakeCache.txt").write_text(f"ABOX_HARDWARE:STRING={target}\n", encoding="utf-8")
            source = build / "current.c"
            source.write_text("/* live source */\n", encoding="utf-8")
            names = set(COMMON)
            if target.startswith("gd32"):
                names.update(("tick", "ABoxGdMonotonic_GetMs", "UART4_IRQHandler"))
            else:
                names.update(("get_tick", "HAL_GetTick", "ABoxStm32Monotonic_GetMs", "UART5_IRQHandler"))
            if missing:
                names.remove(missing)
            lines = [f"{source.as_posix()}:1:1:{name}\t{800 if oversized and name == 'Patrol_EncoderTask' else 8}\tstatic\n"
                     for name in sorted(names)]
            (build / "current.su").write_text("".join(lines), encoding="utf-8")
            # This file can remain in an incremental build after its source is removed.
            (build / "stale.su").write_text(
                f"{(build / 'removed.c').as_posix()}:1:1:ABoxUart_Irq\t9000\tstatic\n", encoding="utf-8")
            return subprocess.run([sys.executable, str(CHECKER), str(build)], capture_output=True, text=True)

    def test_both_targets_ignore_stale_frames(self):
        for target in ("gd32f105_ec800_v1", "stm32f105_ec800_v1"):
            with self.subTest(target=target):
                result = self.run_checker(target)
                self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
                self.assertIn("PASS encoder UART IRQ stack", result.stdout)

    def test_stale_file_cannot_supply_missing_irq_evidence(self):
        result = self.run_checker("gd32f105_ec800_v1", missing="ABoxUart_Irq")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ABoxUart_Irq", result.stderr + result.stdout)

    def test_real_stack_excess_fails(self):
        result = self.run_checker("stm32f105_ec800_v1", oversized=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAIL encoder stack", result.stderr + result.stdout)


if __name__ == "__main__":
    unittest.main()

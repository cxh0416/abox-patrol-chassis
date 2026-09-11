"""Execute the production center state machine with host hardware/clock stubs.

Extract complete functions and the actual CANopen integration blocks, rather
than copying a second implementation. The ARM build separately verifies the
full RTOS translation unit. This test is not physical-motion validation.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CenterRuntimeTests(unittest.TestCase):
    def test_production_center_runtime(self):
        source = (ROOT / "app/Core/User/Src/vcu_rtos.c").read_text(encoding="utf-8")
        gcc = shutil.which("gcc")
        self.assertIsNotNone(gcc, "host GCC is required for safety regression tests")
        types = source[source.index("typedef enum {\n    CENTER_CAL_IDLE"):
                       source.index("#ifndef PATROL_CHASSIS_BENCH_STEERING_ENABLED")]
        names = ["center_reason_name", "center_gate_mask", "center_reject_locked",
                 "center_feedback_ready", "center_update_relative", "center_capture_failure_locked",
                 "center_calibration_fail", "center_calibration_expire_idle",
                 "VcuRtos_CenterCalibrationStart", "center_session_matches",
                 "VcuRtos_CenterCalibrationKeepAlive", "VcuRtos_CenterCalibrationJog",
                 "VcuRtos_CenterCalibrationCommit", "VcuRtos_CenterCalibrationCancel",
                 "VcuRtos_FormatCenterCalibration", "center_calibration_tick"]
        functions = []
        for name in names:
            match = re.search(r"^(?:static )?(?:const char\*|uint8_t|uint32_t|void) " +
                              re.escape(name) + r"\([^;{]*\)\s*\n\{.*?^}\n", source, re.M | re.S)
            self.assertIsNotNone(match, name)
            functions.append(match.group(0))
        gates_start = source.index("            g_feedback_gate_mask =")
        gates_end = source.index("            feedback.power_ready =", gates_start)
        reset_start = source.index("            if (!VehiclePowerOutput_IsOn()) {")
        reset_end = source.index("            uint8_t normal_safe", reset_start)
        cut_start = source.index("        if (g_bench.cut_power || g_drive.cut_power) {")
        cut_end = source.index("        if (VehiclePowerOutput_IsOn() &&", cut_start)
        # Verify that the same production session predicate owns idle polling.
        self.assertIn("g_diagnostics.motion_priority = center_session_active() ||", source)
        self.assertIn("(g_coordinate.valid || center_session_active())", source)
        wrappers = "\nstatic void update_coordinate(uint32_t now) {\n" + (
            "PatrolDiagnosticsSnapshot snapshot = mock_snapshot; ChassisFeedback feedback = {0};\n"
            "const VcuRemoteConfig_t* cfg = &mock_config; feedback.calibrated = mock_config.calibrated;\n"
        ) + source[gates_start:gates_end] + "\n}\n"
        wrappers += "static void apply_reset(uint32_t now) {\n" + source[reset_start:reset_end] + "\n}\n"
        wrappers += "static void apply_cut(uint32_t now) {\n" + source[cut_start:cut_end] + "\n}\n"
        prefix = (ROOT / "tests/center_runtime_stubs.h").read_text(encoding="utf-8")
        tests = (ROOT / "tests/center_runtime_cases.c").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory(prefix="patrol-center-host-") as directory:
            generated = Path(directory) / "center_runtime.c"
            generated.write_text(prefix + types + "\n".join(functions) + wrappers + tests, encoding="utf-8")
            binary = Path(directory) / "center_runtime.exe"
            sources = ["bench_steering.c", "bench_drive.c", "steering_angle.c", "patrol_canopen.c"]
            command = [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-format",
                       "-I" + str(ROOT / "app/Core/User/Inc"),
                       "-I" + str(ROOT / "shared/include"),
                       str(generated), *[str(ROOT / "app/Core/User/Src" / name) for name in sources],
                       "-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()

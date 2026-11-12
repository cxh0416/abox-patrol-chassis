from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PowerLedgerTests(unittest.TestCase):
    def test_power_ledger_fsm(self):
        user = ROOT / "app/Core/User"
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            def cmake_path(path):
                return str(path).replace("\\", "/")
            (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(patrol_power_ledger_test C)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("{cmake_path(ROOT / 'platform')}" platform)
add_executable(patrol_power_ledger_test
  "{cmake_path(ROOT / 'tests/patrol_power_ledger_fsm_test.c')}"
  "{cmake_path(user / 'cloud/src/patrol_power_ledger_fsm.c')}"
  "{cmake_path(user / 'cloud/src/chassis_cloud_protocol.c')}")
target_include_directories(patrol_power_ledger_test PRIVATE
  "{cmake_path(user / 'cloud/include')}" "{cmake_path(user / 'motion/include')}")
target_link_libraries(patrol_power_ledger_test PRIVATE abox::ufs_journal abox::mqtt_request)
add_executable(patrol_power_integration_test
  "{cmake_path(ROOT / 'tests/patrol_power_ledger_integration_test.c')}"
  "{cmake_path(user / 'cloud/src/patrol_power_ledger.c')}"
  "{cmake_path(user / 'cloud/src/patrol_power_ledger_fsm.c')}"
  "{cmake_path(user / 'cloud/src/chassis_cloud_protocol.c')}")
target_include_directories(patrol_power_integration_test PRIVATE
  "{cmake_path(user / 'cloud/private')}" "{cmake_path(ROOT / 'shared/include')}"
  {''.join(chr(34) + cmake_path(path) + chr(34) + ' ' for path in user.glob('*/include'))})
target_link_libraries(patrol_power_integration_test PRIVATE abox::ufs_journal abox::mqtt_request abox::cloud_service)
add_executable(patrol_power_log_test "{cmake_path(ROOT / 'tests/power_ledger_log_capacity_test.c')}")
target_link_libraries(patrol_power_log_test PRIVATE abox::log_ring)
add_executable(public_control_dispatch_test
  "{cmake_path(ROOT / 'tests/public_control_dispatch_test.c')}"
  "{cmake_path(user / 'cloud/src/cloud_public_control.c')}"
  "{cmake_path(user / 'cloud/src/chassis_cloud_protocol.c')}")
target_include_directories(public_control_dispatch_test PRIVATE
  "{cmake_path(user / 'cloud/private')}" "{cmake_path(ROOT / 'shared/include')}"
  {''.join(chr(34) + cmake_path(path) + chr(34) + ' ' for path in user.glob('*/include'))}
  {''.join(chr(34) + cmake_path(path) + chr(34) + ' ' for path in (ROOT / 'platform/components').glob('*/include'))}
  "{cmake_path(ROOT / 'tests/bench_stubs')}")
target_compile_options(public_control_dispatch_test PRIVATE -ffunction-sections -fdata-sections)
target_link_options(public_control_dispatch_test PRIVATE -Wl,--gc-sections)
target_link_libraries(public_control_dispatch_test PRIVATE abox::mqtt_request)
''', encoding="utf-8", newline="\n")
            build = source / "build"
            suffix = ".exe" if os.name == "nt" else ""
            for command in (["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCMAKE_C_COMPILER=" + (shutil.which("gcc") or "gcc")],
                            ["cmake", "--build", str(build), "--target", "patrol_power_ledger_test", "patrol_power_integration_test", "patrol_power_log_test", "public_control_dispatch_test"],
                            [str(build / ("patrol_power_ledger_test" + suffix))],
                            [str(build / ("patrol_power_integration_test" + suffix))],
                            [str(build / ("patrol_power_log_test" + suffix))],
                            [str(build / ("public_control_dispatch_test" + suffix))]):
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MqttTrialManagerTests(unittest.TestCase):
    def test_real_phase_and_restore(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "mqtt_trial_manager_test.exe"
            command = [shutil.which("gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I" + str(ROOT / "tests/mqtt_trial_stubs"),
                       "-I" + str(ROOT / "app/Core/User/mqtt/include"),
                       "-I" + str(ROOT / "app/Core/User/system/include"),
                       "-I" + str(ROOT / "shared/include"),
                       "-I" + str(ROOT / "platform/components/ec800/include"),
                       "-I" + str(ROOT / "platform/components/core/include"),
                       "-I" + str(ROOT / "platform/components/cloud/include"),
                       "-I" + str(ROOT / "platform/components/mqtt/include"),
                       str(ROOT / "tests/mqtt_trial_manager_test.c"),
                       str(ROOT / "app/Core/User/mqtt/src/mqtt_trial_manager.c"),
                       str(ROOT / "platform/components/mqtt/src/abox_mqtt_trial.c"),
                       str(ROOT / "platform/components/mqtt/src/abox_mqtt_trial_executor.c"),
                       str(ROOT / "platform/components/core/src/abox_config_store.c"),
                       str(ROOT / "platform/components/cloud/src/abox_maintenance_service.c"),
                       "-o", str(exe)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()

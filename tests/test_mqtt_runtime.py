from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MqttRuntimeTests(unittest.TestCase):
    def test_runtime_security_chain(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "mqtt_runtime_test.exe"
            sources = [
                ROOT / "tests/mqtt_runtime_test.c",
                ROOT / "app/Core/User/mqtt/src/mqtt_runtime.c",
                ROOT / "platform/components/mqtt/src/abox_mqtt_runtime.c",
                ROOT / "platform/components/ec800/src/abox_ec800_at.c",
                ROOT / "platform/components/ec800/src/abox_ec800_command_port.c",
                ROOT / "platform/components/ec800/src/abox_ec800_ufs.c",
                ROOT / "platform/components/ec800/src/abox_ec800_tls.c",
                ROOT / "platform/components/mqtt/src/abox_mqtt_tls.c",
            ]
            command = [shutil.which("gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I" + str(ROOT / "tests/mqtt_runtime_stubs"),
                       "-I" + str(ROOT / "app/Core/User/mqtt/include"),
                       "-I" + str(ROOT / "platform/components/ec800/include"),
                       "-I" + str(ROOT / "platform/components/mqtt/include"),
                       *map(str, sources), "-o", str(exe)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()

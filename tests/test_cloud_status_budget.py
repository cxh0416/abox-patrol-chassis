import json
import math
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CloudStatusBudgetTests(unittest.TestCase):
    def test_real_encoder_keeps_complete_state_within_public_budgets(self):
        user = ROOT / "app/Core/User"
        platform = ROOT / "platform/components"
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "cloud_status_budget.exe"
            command = ["gcc", "-std=c11", "-O1", "-flto", "-fwhole-program",
                       "-Wall", "-Wextra", "-Werror",
                       "-I" + str(ROOT / "shared/include"),
                       "-I" + str(user / "cloud/private")]
            command += ["-I" + str(path) for path in user.glob("*/include")]
            command += ["-I" + str(path) for path in platform.glob("*/include")]
            command += ["-I" + str(ROOT / "tests/bench_stubs")]
            command += [str(ROOT / "tests/cloud_status_budget_test.c"),
                        str(user / "common/src/fixed_point.c"), "-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            packets = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual(len(packets), 4)
            for line in result.stdout.splitlines():
                raw_payload = line.split('"payload":', 1)[1][:-2]
                self.assertLessEqual(len(line.encode('utf-8')), 1024)
                self.assertLessEqual(len(raw_payload.encode('utf-8')), 768)
            field_types = {
                "reportReason": str, "motionState": str,
                "targetDriveRpm": int, "targetSteeringDeg": (int, float),
                "leftActualRpm": int, "rightActualRpm": int,
                "steeringActualDeg": (int, float), "absoluteActualDeg": (int, float),
                "steeringOnline": bool, "driveOnline": bool, "encoderOnline": bool,
                "canBusOff": bool, "driveFault": bool, "feedbackFresh": bool,
                "calibrated": bool, "steeringProfileReady": bool, "steeringRangeValid": bool,
                "powerReady": bool, "powerOutputOn": bool, "otaBusy": bool, "safetyReady": bool,
                "stopReason": int, "faultLatched": bool, "steeringBoundary": int,
                "faultRecordVersion": int,
            }
            for packet in packets:
                data = packet["data"]
                payload = data["payload"]
                self.assertEqual(packet["version"], "4.0")
                self.assertEqual(data["reportType"], "state")
                self.assertEqual(data["reportName"], "chassis_status")
                self.assertLessEqual(len(json.dumps(payload, separators=(",", ":")).encode()), 768)
                self.assertLessEqual(field_types.keys(), payload.keys())
                for field, expected in field_types.items():
                    with self.subTest(field=field):
                        if isinstance(expected, tuple):
                            self.assertIn(type(payload[field]), expected)
                            self.assertTrue(math.isfinite(payload[field]))
                        else:
                            self.assertIs(type(payload[field]), expected)
                self.assertIn(payload["reportReason"], {"periodic", "reconnect", "state_changed",
                                                       "fault_raised", "fault_cleared", "safety_stop"})
                self.assertIn(payload["motionState"], {"fault", "reversing_wait", "driving", "stopped"})
                self.assertIn(payload["steeringBoundary"], {-1, 0, 1})
                self.assertEqual(payload["faultRecordVersion"], 1)
                self.assertFalse(payload["powerOutputOn"])
                self.assertFalse(payload["safetyReady"])
                self.assertEqual(len(data["reportId"].split("-")[0]), 31)
            self.assertEqual(len(packets[-1]["data"]["requestId"]), 64)
            print("maximum encoded state:", max(len(line.encode()) for line in result.stdout.splitlines()), "bytes")


if __name__ == "__main__":
    unittest.main()

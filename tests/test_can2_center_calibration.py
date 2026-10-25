import importlib.util
from pathlib import Path
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "can2_center_calibration" / "can2_center_calibration.py"
SPEC = importlib.util.spec_from_file_location("can2_center_calibration", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class FakeBus:
    def __init__(self):
        self.rx = []
        self.power = False
        self.phase = 0
        self.token = 0
        self.driver = -1464
        self.saved = -1464
        self.saved_offset = 0
        self.active = False
        self.calibrated = True
        self.fault_latched = False
        self.session_active = False
        self.calibration_prepared = False
        self.transmitted = []

    def transmit(self, can_id, data):
        self.transmitted.append((can_id, data))
        if can_id == MODULE.MOTION_COMMAND:
            _, operation, sequence, _, _ = struct.unpack("<BBHhh", data)
            if operation == MODULE.MOTION_POWER_ON:
                self.power = True
            elif operation == MODULE.MOTION_POWER_OFF:
                self.power = False
            elif operation == MODULE.MOTION_RESET_FAULT:
                self.fault_latched = False
            flags = ((1 if self.power else 0) | (2 if self.calibrated else 0) | 4 |
                     (8 if self.fault_latched else 0) | (32 if self.session_active else 0))
            self._frames(
                (MODULE.MOTION_ACK, MODULE.MOTION_ANGLES, MODULE.MOTION_WHEELS),
                bytes([1, 0, sequence & 0xFF, sequence >> 8, flags, 0, 1, 0]),
                bytes([sequence & 0xFF, sequence >> 8, 0, 0, 0, 0, 1, 1]),
                bytes([sequence & 0xFF, sequence >> 8, 0, 0, 0, 0, 0, 0]),
            )
            return
        if can_id != MODULE.CAL_COMMAND:
            raise AssertionError(f"unexpected ID 0x{can_id:X}")
        _, operation, sequence, token, argument = struct.unpack("<BBHHh", data)
        result = MODULE.CAL_OK
        if operation == MODULE.CAL_PREPARE:
            self.calibration_prepared = True
        elif operation == MODULE.CAL_START:
            if not self.calibration_prepared:
                result = MODULE.CAL_SAFETY
            self.calibration_prepared = False
            self.token = 77
            self.phase = 1
            self.active = True
        elif operation in (MODULE.CAL_QUERY, MODULE.CAL_JOG, MODULE.CAL_COMMIT, MODULE.CAL_CANCEL):
            if token not in (0, self.token):
                result = MODULE.CAL_TOKEN
            elif operation == MODULE.CAL_QUERY and token != 0 and not self.active:
                result = MODULE.CAL_STATE
            elif operation == MODULE.CAL_JOG:
                self.phase = 2
                self.driver += argument
            elif operation == MODULE.CAL_COMMIT:
                self.phase = 7
                self.saved = self.driver + self.saved_offset
                self.calibrated = True
                self.active = False
                self.token = 0
            elif operation == MODULE.CAL_CANCEL:
                self.phase = 0
                self.active = False
                self.calibration_prepared = False
        flags = (1 if self.active else 0) | (2 if self.calibrated else 0) | (4 if self.power else 0)
        ack = bytes([
            1, result, sequence & 0xFF, sequence >> 8, self.phase, 0,
            self.token & 0xFF, self.token >> 8,
        ])
        data_frame = (
            bytes([sequence & 0xFF, sequence >> 8])
            + struct.pack("<iH", self.driver, 0)
        )
        status = (
            bytes([sequence & 0xFF, sequence >> 8])
            + struct.pack("<iBB", self.saved, flags, 0)
        )
        self._frames((MODULE.CAL_ACK, MODULE.CAL_DATA, MODULE.CAL_STATUS),
                     ack, data_frame, status)

    def _frames(self, identifiers, *frames):
        for can_id, frame in zip(identifiers, frames):
            self.rx.append((can_id, frame))

    def receive(self, timeout_ms):
        del timeout_ms
        return self.rx.pop(0) if self.rx else None


class Can2CenterCalibrationTests(unittest.TestCase):
    def test_current_center_sequence_uses_same_state_machine(self):
        bus = FakeBus()
        client = MODULE.Can2CalibrationClient(bus)
        result = MODULE.calibrate_current(client, reset_fault=False, assume_yes=True)
        self.assertEqual(result["captured_driver_center"], -1464)
        self.assertEqual(result["saved_driver_center"], -1464)
        self.assertEqual(result["center_readback_delta"], 0)
        self.assertTrue(result["calibrated"])
        operations = []
        for can_id, data in bus.transmitted:
            if can_id == MODULE.CAL_COMMAND:
                operations.append(struct.unpack("<BBHHh", data)[1])
        self.assertEqual(
            operations,
            [MODULE.CAL_QUERY, MODULE.CAL_PREPARE, MODULE.CAL_START, MODULE.CAL_QUERY,
             MODULE.CAL_COMMIT, MODULE.CAL_QUERY, MODULE.CAL_QUERY],
        )
        self.assertFalse(bus.power)

    def test_status_only_does_not_start_session(self):
        bus = FakeBus()
        client = MODULE.Can2CalibrationClient(bus)
        status = client.query()
        self.assertEqual(status.phase_name, "IDLE")
        self.assertFalse(status.active)
        self.assertEqual(status.saved_center, -1464)
        self.assertEqual(len(bus.transmitted), 1)

    def test_default_channel_matches_current_can2_wiring(self):
        args = MODULE.build_parser().parse_args(["status"])
        self.assertEqual(args.channel, 0)

    def test_reset_fault_is_a_separate_non_calibration_command(self):
        bus = FakeBus()
        bus.rx = []
        bus.power = True
        bus.calibrated = True
        bus.fault_latched = True
        client = MODULE.Can2CalibrationClient(bus)
        status = MODULE.reset_motion_fault(client)
        self.assertFalse(status.fault_latched)
        operations = [
            struct.unpack("<BBHhh", data)[1]
            for can_id, data in bus.transmitted if can_id == MODULE.MOTION_COMMAND
        ]
        self.assertEqual(operations, [MODULE.MOTION_POWER_OFF, MODULE.MOTION_RESET_FAULT, MODULE.MOTION_QUERY])
        self.assertFalse(any(can_id == MODULE.CAL_COMMAND for can_id, _ in bus.transmitted))

    def test_active_remote_session_blocks_before_commit_prompt(self):
        bus = FakeBus()
        bus.rx = []
        bus.power = True
        bus.session_active = True
        client = MODULE.Can2CalibrationClient(bus)
        with self.assertRaisesRegex(MODULE.Can2CalibrationError, "T12 A switch"):
            MODULE.calibrate_current(client, reset_fault=False, assume_yes=True)
        calibration_operations = [
            struct.unpack("<BBHHh", data)[1]
            for can_id, data in bus.transmitted if can_id == MODULE.CAL_COMMAND
        ]
        self.assertEqual(calibration_operations, [MODULE.CAL_QUERY])

    def test_saved_center_mismatch_is_rejected(self):
        bus = FakeBus()
        bus.rx = []
        bus.power = True
        bus.saved_offset = 6
        client = MODULE.Can2CalibrationClient(bus)
        with self.assertRaisesRegex(MODULE.Can2CalibrationError, "does not match captured center"):
            MODULE.calibrate_current(client, reset_fault=False, assume_yes=True)
        self.assertFalse(bus.power)


if __name__ == "__main__":
    unittest.main()

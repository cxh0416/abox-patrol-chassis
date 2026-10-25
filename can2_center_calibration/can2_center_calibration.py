#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Run center calibration through the A-BOX CAN2 maintenance service.

This script uses only the MCU CAN2 protocol. It never writes motor parameters
or Config V5 directly. START/JOG/COMMIT/CANCEL execute the same firmware state
machine used by the cloud maintenance page.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import struct
import sys
import time
from dataclasses import asdict, dataclass
from typing import Callable, Iterable


SCRIPT_VERSION = "1.1.0"
VCI_USBCAN2 = 4
MOTION_COMMAND = 0x520
MOTION_ACK = 0x521
MOTION_ANGLES = 0x522
MOTION_WHEELS = 0x523
CAL_COMMAND = 0x530
CAL_ACK = 0x531
CAL_DATA = 0x532
CAL_STATUS = 0x533

MOTION_QUERY = 0
MOTION_POWER_OFF = 4
MOTION_POWER_ON = 5
MOTION_RESET_FAULT = 6

CAL_QUERY = 0
CAL_START = 1
CAL_JOG = 2
CAL_COMMIT = 3
CAL_CANCEL = 4
CAL_PREPARE = 5

CAL_OK = 0
CAL_INVALID = 1
CAL_SAFETY = 2
CAL_TOKEN = 3
CAL_SEQUENCE = 4
CAL_STALE = 5
CAL_STATE = 6

PHASE_NAMES = {
    0: "IDLE",
    1: "READY",
    2: "JOGGING",
    3: "STOPPING",
    4: "COMMIT_STABLE",
    5: "ENCODER_WRITE",
    6: "ENCODER_VERIFY",
    7: "COMPLETE",
    8: "FAILED",
    9: "START_STABLE",
    10: "EXPIRED",
}

REASON_NAMES = {
    0: "NONE",
    1: "READY_LEASE_TIMEOUT",
    2: "PHASE_TIMEOUT",
    3: "GATE_NOT_READY",
    4: "RELATIVE_MISMATCH",
    5: "STEERING_EXECUTOR_FAULT",
    6: "COMMIT_EXECUTOR_BUSY",
    7: "COMMIT_STEERING_ENABLED",
    8: "CONFIG_CLEAR_FAILED",
    9: "ENCODER_TX_FAILED",
    10: "ENCODER_VERIFY_MISMATCH",
    11: "CONFIG_SAVE_FAILED",
    12: "WRONG_PHASE",
    13: "SESSION_MISMATCH",
    14: "INVALID_JOG_DELTA",
    15: "JOG_LIMIT",
    16: "ALREADY_ACTIVE",
    17: "ENCODER_OUT_OF_RANGE",
    18: "EXECUTOR_BUSY",
    19: "POSITION_EPOCH_RESET",
}

RESULT_NAMES = {
    0: "OK",
    1: "INVALID",
    2: "SAFETY",
    3: "TOKEN",
    4: "SEQUENCE",
    5: "STALE",
    6: "STATE",
}


class Can2CalibrationError(RuntimeError):
    pass


class Can2ResponseTimeout(Can2CalibrationError):
    """A CAN2 command was sent, but its correlated reply group was incomplete."""

    def __init__(self, sequence: int, expected: set[int], found: set[int],
                 mismatched_sequences: set[int]):
        self.sequence = sequence
        self.expected = expected
        self.found = found
        self.missing = expected - found
        self.mismatched_sequences = mismatched_sequences
        found_text = ", ".join(f"0x{can_id:03X}" for can_id in sorted(found)) or "none"
        missing_text = ", ".join(f"0x{can_id:03X}" for can_id in sorted(self.missing)) or "none"
        super().__init__(
            f"Incomplete reply for sequence {sequence}: expected "
            f"{sorted(f'0x{x:03X}' for x in expected)}, received [{found_text}], "
            f"missing [{missing_text}]"
        )


class VciCanObj(ctypes.Structure):
    _fields_ = [
        ("ID", ctypes.c_uint),
        ("TimeStamp", ctypes.c_uint),
        ("TimeFlag", ctypes.c_byte),
        ("SendType", ctypes.c_byte),
        ("RemoteFlag", ctypes.c_byte),
        ("ExternFlag", ctypes.c_byte),
        ("DataLen", ctypes.c_byte),
        ("Data", ctypes.c_ubyte * 8),
        ("Reserved", ctypes.c_ubyte * 3),
    ]


class VciInitConfig(ctypes.Structure):
    _fields_ = [
        ("AccCode", ctypes.c_uint),
        ("AccMask", ctypes.c_uint),
        ("Reserved", ctypes.c_uint),
        ("Filter", ctypes.c_ubyte),
        ("Timing0", ctypes.c_ubyte),
        ("Timing1", ctypes.c_ubyte),
        ("Mode", ctypes.c_ubyte),
    ]


class Canalyst:
    """Minimal CANalyst-II wrapper using its installed ControlCAN.dll."""

    TIMING_500K = (0x00, 0x1C)

    def __init__(self, channel: int, dll_path: str | None, device_index: int = 0):
        self.channel = channel
        self.device_index = device_index
        self.dll = self._load(dll_path)
        self._bind()
        self.opened = False

    @staticmethod
    def _load(dll_path: str | None):
        candidates = []
        if dll_path:
            candidates.append(dll_path)
        candidates.extend([
            "ControlCAN.dll",
            str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "SysWOW64" / "ControlCAN.dll"),
            str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "ControlCAN.dll"),
        ])
        last_error = None
        for candidate in candidates:
            try:
                return ctypes.WinDLL(candidate)
            except OSError as exc:
                last_error = exc
        raise Can2CalibrationError(
            "Cannot load ControlCAN.dll. Install/select the CANalyst-II driver and pass --dll. "
            f"Last error: {last_error}"
        )

    def _bind(self):
        dll = self.dll
        dll.VCI_OpenDevice.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
        dll.VCI_OpenDevice.restype = ctypes.c_uint
        dll.VCI_CloseDevice.argtypes = [ctypes.c_uint, ctypes.c_uint]
        dll.VCI_CloseDevice.restype = ctypes.c_uint
        dll.VCI_InitCAN.argtypes = [
            ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(VciInitConfig)
        ]
        dll.VCI_InitCAN.restype = ctypes.c_uint
        dll.VCI_StartCAN.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
        dll.VCI_StartCAN.restype = ctypes.c_uint
        dll.VCI_Transmit.argtypes = [
            ctypes.c_uint, ctypes.c_uint, ctypes.c_uint,
            ctypes.POINTER(VciCanObj), ctypes.c_uint
        ]
        dll.VCI_Transmit.restype = ctypes.c_uint
        dll.VCI_Receive.argtypes = [
            ctypes.c_uint, ctypes.c_uint, ctypes.c_uint,
            ctypes.POINTER(VciCanObj), ctypes.c_uint, ctypes.c_int
        ]
        dll.VCI_Receive.restype = ctypes.c_uint

    def open(self):
        if self.dll.VCI_OpenDevice(VCI_USBCAN2, self.device_index, 0) != 1:
            raise Can2CalibrationError("VCI_OpenDevice failed; close other CAN tools and check USB")
        config = VciInitConfig()
        config.AccCode = 0
        config.AccMask = 0xFFFFFFFF
        config.Filter = 0
        config.Timing0, config.Timing1 = self.TIMING_500K
        config.Mode = 0
        if self.dll.VCI_InitCAN(
            VCI_USBCAN2, self.device_index, self.channel, ctypes.byref(config)
        ) != 1:
            self.close()
            raise Can2CalibrationError(f"VCI_InitCAN failed on analyzer channel {self.channel + 1}")
        if self.dll.VCI_StartCAN(VCI_USBCAN2, self.device_index, self.channel) != 1:
            self.close()
            raise Can2CalibrationError(f"VCI_StartCAN failed on analyzer channel {self.channel + 1}")
        self.opened = True

    def close(self):
        if self.dll is not None and self.opened:
            self.dll.VCI_CloseDevice(VCI_USBCAN2, self.device_index)
            self.opened = False

    def transmit(self, can_id: int, data: bytes):
        if len(data) != 8:
            raise Can2CalibrationError("A-BOX CAN2 maintenance commands require DLC=8")
        frame = VciCanObj()
        frame.ID = can_id
        frame.SendType = 0
        frame.RemoteFlag = 0
        frame.ExternFlag = 0
        frame.DataLen = 8
        for index, value in enumerate(data):
            frame.Data[index] = value
        if self.dll.VCI_Transmit(
            VCI_USBCAN2, self.device_index, self.channel, ctypes.byref(frame), 1
        ) != 1:
            raise Can2CalibrationError(f"CAN transmit failed for 0x{can_id:03X}")

    def receive(self, timeout_ms: int):
        frame = VciCanObj()
        count = self.dll.VCI_Receive(
            VCI_USBCAN2, self.device_index, self.channel,
            ctypes.byref(frame), 1, int(timeout_ms)
        )
        if count != 1:
            return None
        return frame.ID, bytes(frame.Data[index] for index in range(frame.DataLen & 0x0F))


@dataclass
class MotionStatus:
    result: int
    sequence: int
    flags: int
    stop_reason: int
    feedback_age_ms: int
    steering_online: bool
    drive_online: bool
    left_rpm: int
    right_rpm: int

    @property
    def power_on(self) -> bool:
        return bool(self.flags & 0x01)

    @property
    def calibrated(self) -> bool:
        return bool(self.flags & 0x02)

    @property
    def motion_ready(self) -> bool:
        return bool(self.flags & 0x04)

    @property
    def fault_latched(self) -> bool:
        return bool(self.flags & 0x08)

    @property
    def local_control(self) -> bool:
        return bool(self.flags & 0x10)

    @property
    def session_active(self) -> bool:
        return bool(self.flags & 0x20)


@dataclass
class CalibrationStatus:
    result: int
    sequence: int
    phase: int
    reason: int
    token: int
    driver_raw: int
    encoder_raw: int
    saved_center: int
    active: bool
    calibrated: bool
    power_on: bool

    @property
    def phase_name(self) -> str:
        return PHASE_NAMES.get(self.phase, f"UNKNOWN_{self.phase}")

    @property
    def reason_name(self) -> str:
        return REASON_NAMES.get(self.reason, f"UNKNOWN_{self.reason}")

    @property
    def result_name(self) -> str:
        return RESULT_NAMES.get(self.result, f"UNKNOWN_{self.result}")

    def to_dict(self):
        data = asdict(self)
        data["phase_name"] = self.phase_name
        data["reason_name"] = self.reason_name
        data["result_name"] = self.result_name
        return data


class Can2CalibrationClient:
    def __init__(self, bus, response_timeout: float = 1.0, query_retries: int = 3,
                 retry_delay_s: float = 0.05):
        if response_timeout <= 0:
            raise ValueError("response_timeout must be positive")
        if query_retries < 1:
            raise ValueError("query_retries must be at least 1")
        if retry_delay_s < 0:
            raise ValueError("retry_delay_s must not be negative")
        self.bus = bus
        self.motion_sequence = 0
        self.cal_sequence = 0
        self.response_timeout = response_timeout
        self.query_retries = query_retries
        self.retry_delay_s = retry_delay_s

    @staticmethod
    def _u16(data: bytes, offset: int) -> int:
        return struct.unpack_from("<H", data, offset)[0]

    @staticmethod
    def _i32(data: bytes, offset: int) -> int:
        return struct.unpack_from("<i", data, offset)[0]

    def _next_motion_sequence(self) -> int:
        self.motion_sequence = (self.motion_sequence + 1) & 0xFFFF
        if self.motion_sequence == 0:
            self.motion_sequence = 1
        return self.motion_sequence

    def _next_cal_sequence(self) -> int:
        self.cal_sequence = (self.cal_sequence + 1) & 0xFFFF
        if self.cal_sequence == 0:
            self.cal_sequence = 1
        return self.cal_sequence

    def _wait_frames(self, expected: Iterable[int], sequence: int,
                     timeout: float | None = None):
        expected = set(expected)
        found = {}
        mismatched_sequences = set()
        if timeout is None:
            timeout = self.response_timeout
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            frame = self.bus.receive(max(1, min(50, int(remaining * 1000))))
            if frame is None:
                continue
            can_id, data = frame
            if can_id not in expected or len(data) != 8:
                continue
            if can_id in (MOTION_ANGLES, MOTION_WHEELS, CAL_DATA, CAL_STATUS):
                response_sequence = self._u16(data, 0)
            else:
                response_sequence = self._u16(data, 2)
            if response_sequence != sequence:
                mismatched_sequences.add(response_sequence)
                continue
            found[can_id] = data
            if set(found) == expected:
                return found
        raise Can2ResponseTimeout(
            sequence, expected, set(found), mismatched_sequences
        )

    def _retry_read(self, exchange: Callable[[], object], attempts: int | None = None):
        count = self.query_retries if attempts is None else max(1, attempts)
        for attempt in range(count):
            try:
                return exchange()
            except Can2ResponseTimeout:
                if attempt + 1 >= count:
                    raise
                time.sleep(self.retry_delay_s)
        raise AssertionError("unreachable")

    def motion(self, operation: int, rpm: int = 0, steering_mdeg: int = 0,
               timeout: float | None = None) -> MotionStatus:
        sequence = self._next_motion_sequence()
        payload = struct.pack("<BBHhh", 1, operation, sequence, rpm, steering_mdeg)
        self.bus.transmit(MOTION_COMMAND, payload)
        frames = self._wait_frames((MOTION_ACK, MOTION_ANGLES, MOTION_WHEELS), sequence, timeout)
        ack, angles, wheels = frames[MOTION_ACK], frames[MOTION_ANGLES], frames[MOTION_WHEELS]
        return MotionStatus(
            result=ack[1],
            sequence=sequence,
            flags=ack[4],
            stop_reason=ack[5],
            feedback_age_ms=self._u16(ack, 6),
            steering_online=bool(angles[6]),
            drive_online=bool(angles[7]),
            left_rpm=struct.unpack_from("<h", wheels, 2)[0],
            right_rpm=struct.unpack_from("<h", wheels, 4)[0],
        )

    def motion_status(self, attempts: int | None = None) -> MotionStatus:
        return self._retry_read(lambda: self.motion(MOTION_QUERY), attempts)

    def power_on(self) -> MotionStatus:
        return self.motion(MOTION_POWER_ON)

    def power_off(self) -> MotionStatus:
        return self.motion(MOTION_POWER_OFF)

    def reset_fault(self) -> MotionStatus:
        return self.motion(MOTION_RESET_FAULT)

    def calibration(self, operation: int, token: int = 0, argument_mdeg: int = 0,
                    timeout: float | None = None) -> CalibrationStatus:
        sequence = self._next_cal_sequence()
        payload = struct.pack("<BBHHh", 1, operation, sequence, token, argument_mdeg)
        self.bus.transmit(CAL_COMMAND, payload)
        frames = self._wait_frames((CAL_ACK, CAL_DATA, CAL_STATUS), sequence, timeout)
        ack, data, status = frames[CAL_ACK], frames[CAL_DATA], frames[CAL_STATUS]
        flags = status[6]
        return CalibrationStatus(
            result=ack[1],
            sequence=sequence,
            phase=ack[4],
            reason=ack[5],
            token=self._u16(ack, 6),
            driver_raw=self._i32(data, 2),
            encoder_raw=self._u16(data, 6),
            saved_center=self._i32(status, 2),
            active=bool(flags & 0x01),
            calibrated=bool(flags & 0x02),
            power_on=bool(flags & 0x04),
        )

    def query(self, token: int = 0, attempts: int | None = None) -> CalibrationStatus:
        return self._retry_read(
            lambda: self.calibration(CAL_QUERY, token=token), attempts
        )


def _require_ok(status: CalibrationStatus, action: str):
    if status.result != CAL_OK:
        raise Can2CalibrationError(
            f"{action} rejected: result={status.result_name}, "
            f"phase={status.phase_name}, reason={status.reason_name}"
        )


def _wait_phase(client: Can2CalibrationClient, token: int, wanted: int, timeout_s: float):
    deadline = time.monotonic() + timeout_s
    last = None
    while time.monotonic() < deadline:
        last = client.query(token)
        if last.phase == wanted and (last.result == CAL_OK or
                                     (wanted == 7 and last.result == CAL_TOKEN)):
            return last
        if last.phase in (8, 10):
            raise Can2CalibrationError(
                f"Calibration ended in {last.phase_name}: {last.reason_name}"
            )
        if last.result not in (CAL_OK, CAL_STATE):
            raise Can2CalibrationError(
                f"Calibration query failed: result={last.result_name}, "
                f"phase={last.phase_name}, reason={last.reason_name}"
            )
        time.sleep(0.1)
    state = last.phase_name if last else "NO_RESPONSE"
    raise Can2CalibrationError(f"Timeout waiting for {PHASE_NAMES[wanted]}; current phase={state}")


def _prepare_calibration(client: Can2CalibrationClient):
    """PREPARE is idempotent, so an incomplete reply may be retried safely."""
    prepared = client._retry_read(
        lambda: client.calibration(CAL_PREPARE), client.query_retries
    )
    _require_ok(prepared, "PREPARE")
    return prepared


def _start_calibration(client: Can2CalibrationClient, deadline: float):
    """START is non-idempotent; recover its token through a read-only query."""
    while True:
        try:
            started = client.calibration(CAL_START)
        except Can2ResponseTimeout as exc:
            print(f"WARNING: START reply incomplete ({exc}); checking session state")
            observed = client.query()
            if observed.token:
                print(f"Recovered START session token: {observed.token}")
                return observed
            started = None
        if started is not None and started.result == CAL_OK:
            return started
        motion = client.motion_status()
        if motion.fault_latched:
            raise Can2CalibrationError("A motion fault became latched before calibration START")
        if motion.session_active:
            raise Can2CalibrationError("Another control session became active before calibration START")
        if time.monotonic() >= deadline:
            if started is None:
                raise Can2CalibrationError("START result could not be recovered")
            _require_ok(started, "START")
        time.sleep(0.1)


def _commit_calibration(client: Can2CalibrationClient, token: int):
    """COMMIT is never replayed; recover only by observing the state machine."""
    try:
        committed = client.calibration(CAL_COMMIT, token=token)
    except Can2ResponseTimeout as exc:
        print(f"WARNING: COMMIT reply incomplete ({exc}); checking calibration state")
        observed = client.query(token)
        if observed.phase == 1:
            raise Can2CalibrationError(
                "COMMIT reply was incomplete and the state remains READY; "
                "do not replay COMMIT automatically. Check the vehicle and rerun explicitly."
            ) from exc
        if observed.phase not in (4, 5, 6, 7):
            raise Can2CalibrationError(
                f"COMMIT reply was incomplete; unexpected phase "
                f"{observed.phase_name}. Do not replay COMMIT automatically."
            ) from exc
        print(f"Recovered COMMIT from calibration phase {observed.phase_name}")
        return observed, True
    _require_ok(committed, "COMMIT")
    return committed, False


def calibrate_current(client: Can2CalibrationClient, reset_fault: bool, assume_yes: bool):
    initial = client.query()
    _require_ok(initial, "Initial status")
    motion = client.motion_status()
    print(json.dumps({"initial": initial.to_dict(), "motion": asdict(motion)}, ensure_ascii=False, indent=2))

    if motion.session_active:
        raise Can2CalibrationError(
            "Another control session is active. Release the T12 A switch or stop the other "
            "remote-control script, wait at least 1 second, and rerun."
        )
    if motion.fault_latched:
        if not reset_fault:
            raise Can2CalibrationError(
                "A motion fault is latched. Stop the vehicle and verify the cause; "
                "for drive execution faults confirm Node 2 0x6061 reads 3, then use "
                "`-ResetFault` with the PowerShell wrapper or `--reset-fault` with Python."
            )
        off = client.power_off()
        if off.result != 0:
            raise Can2CalibrationError(f"POWER_OFF rejected: result={off.result}")
        reset = client.reset_fault()
        if reset.result != 0:
            raise Can2CalibrationError(f"RESET_FAULT rejected: result={reset.result}")
        motion = client.motion_status()
        if motion.fault_latched:
            raise Can2CalibrationError("RESET_FAULT was accepted but the motion fault remains latched")

    try:
        if not assume_yes:
            answer = input(
                "确认转向轮已经在机械中位、车辆已架空或具备急停，输入 YES 开始提交: "
            ).strip()
            if answer != "YES":
                raise Can2CalibrationError("Operator did not confirm mechanical center")

        _prepare_calibration(client)
        if not motion.power_on:
            powered = client.power_on()
            if powered.result != 0 or not powered.power_on:
                raise Can2CalibrationError(f"POWER_ON rejected: result={powered.result}")

        started = _start_calibration(client, time.monotonic() + 3.0)
        if started.token == 0:
            raise Can2CalibrationError("START succeeded without a session token")
        token = started.token
        print(f"Session token: {token}. Waiting for READY...")
        ready = _wait_phase(client, token, 1, 10.0)
        captured = ready.driver_raw
        print(f"READY: current 0x6064={captured}, encoder={ready.encoder_raw}. Committing...")

        _, commit_reply_recovered = _commit_calibration(client, token)
        completed = _wait_phase(client, token, 7, 20.0)
        final = client.query()
        _require_ok(final, "Final status")
        if not final.calibrated:
            raise Can2CalibrationError("Final status is not calibrated=true")
        if abs(final.saved_center - captured) > 5:
            raise Can2CalibrationError(
                f"Saved center {final.saved_center} does not match captured center {captured} "
                "within 5 counts"
            )

        summary = {
            "captured_driver_center": captured,
            "saved_driver_center": final.saved_center,
            "center_readback_delta": final.saved_center - captured,
            "encoder_raw": completed.encoder_raw,
            "calibrated": final.calibrated,
            "phase": final.phase_name,
            "result": final.result_name,
            "commit_reply_recovered": commit_reply_recovered,
            "script_version": SCRIPT_VERSION,
        }
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        return summary
    finally:
        try:
            client.power_off()
        except Can2CalibrationError as exc:
            print(f"WARNING: final POWER_OFF failed: {exc}", file=sys.stderr)


def interactive(client: Can2CalibrationClient):
    token = 0
    print("Commands: status, power-on, power-off, reset-fault, start, jog <mdeg>, commit, watch, cancel, quit")
    try:
        while True:
            try:
                line = input("can2-cal> ").strip().lower()
            except EOFError:
                break
            if not line:
                continue
            parts = line.split()
            command = parts[0]
            try:
                if command in ("quit", "exit"):
                    break
                if command == "status":
                    result = client.query(token)
                elif command == "power-on":
                    result = client.power_on()
                elif command == "power-off":
                    result = client.power_off()
                elif command == "reset-fault":
                    result = client.reset_fault()
                elif command == "start":
                    result = client.calibration(CAL_START)
                    if result.result == CAL_OK:
                        token = result.token
                elif command == "jog":
                    if len(parts) != 2:
                        raise Can2CalibrationError("usage: jog <mdeg>, allowed ±200/±1000/±5000")
                    result = client.calibration(CAL_JOG, token, int(parts[1]))
                elif command == "commit":
                    result = client.calibration(CAL_COMMIT, token)
                elif command == "cancel":
                    result = client.calibration(CAL_CANCEL, token)
                    if result.result == CAL_OK:
                        token = 0
                elif command == "watch":
                    while True:
                        result = client.query(token)
                        print(json.dumps(result.to_dict(), ensure_ascii=False))
                        if result.result != CAL_OK or not result.active:
                            break
                        time.sleep(0.2)
                    continue
                else:
                    raise Can2CalibrationError(f"unknown command: {command}")
                print(json.dumps(asdict(result), ensure_ascii=False, indent=2))
            except (Can2CalibrationError, ValueError) as exc:
                print(f"ERROR: {exc}", file=sys.stderr)
    finally:
        if token:
            try:
                client.calibration(CAL_CANCEL, token)
            except Can2CalibrationError as exc:
                print(f"WARNING: calibration cancel on exit failed: {exc}", file=sys.stderr)
        try:
            client.power_off()
        except Can2CalibrationError as exc:
            print(f"WARNING: power off on exit failed: {exc}", file=sys.stderr)


def reset_motion_fault(client: Can2CalibrationClient):
    off = client.power_off()
    if off.result != 0:
        raise Can2CalibrationError(f"POWER_OFF rejected: result={off.result}")
    reset = client.reset_fault()
    if reset.result != 0:
        raise Can2CalibrationError(f"RESET_FAULT rejected: result={reset.result}")
    time.sleep(0.2)
    status = client.motion_status()
    if status.fault_latched:
        raise Can2CalibrationError("RESET_FAULT was accepted but the motion fault remains latched")
    print(json.dumps(asdict(status), ensure_ascii=False, indent=2))
    return status


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--channel", type=int, default=0,
                        help="CANalyst-II physical channel: 0=CH1 (default for current CAN2 wiring), 1=CH2")
    parser.add_argument("--device-index", type=int, default=0)
    parser.add_argument("--dll", help="path to ControlCAN.dll")
    parser.add_argument("--response-timeout", type=float, default=1.0,
                        help="seconds to wait for one complete three-frame reply (default: 1.0)")
    parser.add_argument("--query-retries", type=int, default=3,
                        help="read-only query attempts when a reply is incomplete (default: 3)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status", help="query calibration and current 0x6064 values")
    current = sub.add_parser("calibrate-current", help="start, wait READY, commit current position, power off")
    current.add_argument("--reset-fault", action="store_true",
                         help="explicitly power off and clear a latched software motion fault first")
    current.add_argument("--yes", action="store_true",
                         help="skip the mechanical-center confirmation prompt")
    sub.add_parser("reset-fault", help="power off and clear the software motion-fault latch only")
    sub.add_parser("interactive", help="interactive start/jog/commit/cancel workflow")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.channel not in (0, 1):
        raise SystemExit("--channel must be 0 (CH1) or 1 (CH2)")
    if args.response_timeout <= 0:
        raise SystemExit("--response-timeout must be positive")
    if args.query_retries < 1:
        raise SystemExit("--query-retries must be at least 1")
    bus = Canalyst(args.channel, args.dll, args.device_index)
    client = Can2CalibrationClient(
        bus,
        response_timeout=args.response_timeout,
        query_retries=args.query_retries,
    )
    try:
        bus.open()
        if args.command == "status":
            calibration = client.query()
            motion = client.motion_status()
            print(json.dumps(
                {"calibration": calibration.to_dict(), "motion": asdict(motion)},
                ensure_ascii=False, indent=2))
            return 0 if calibration.result == CAL_OK and not motion.fault_latched else 1
        if args.command == "calibrate-current":
            calibrate_current(client, args.reset_fault, args.yes)
            return 0
        if args.command == "reset-fault":
            reset_motion_fault(client)
            return 0
        interactive(client)
        return 0
    except Can2CalibrationError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        bus.close()


if __name__ == "__main__":
    raise SystemExit(main())

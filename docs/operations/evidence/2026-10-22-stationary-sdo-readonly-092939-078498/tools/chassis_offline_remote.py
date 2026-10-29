#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Chassis keyboard control through the A-BOX MCU CAN2 command service.

Normal WASD control uses chassis_can2_keyboard.py and protocol IDs 0x520..0x523.
The MCU owns the CAN1 drives, runtime coordinates, dual feedback, reversal
confirmation and a 300 ms lease. Defaults: 500 rpm / 25 degrees.
P=power on, E=arm at neutral, WASD=targets, SPACE=stop/disarm, O=off,
R=reset while off, ESC=stop/off/exit. Focus loss disarms.

The legacy CANopen classes below are retained only for existing diagnostic
and explicit parameter-maintenance tools. They are not the normal motion path.
They require a separate diagnostic transport and do not work through the typed
CAN2 endpoint. Never enable a transparent motion gateway to bypass MCU gates.
"""
import argparse
import ctypes
from dataclasses import dataclass
import json
import math
import os
from pathlib import Path
import struct
import sys
import time
from typing import Optional

# ----------------------------------------------------------------------------
# CANalyst-II (ControlCAN.dll) binding
# ----------------------------------------------------------------------------
VCI_USBCAN2 = 4

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

class CanalystError(RuntimeError):
    pass


class CarProfileError(CanalystError):
    pass


@dataclass(frozen=True)
class CarProfile:
    number: int
    tag: str
    steering_center_count: Optional[int]
    absolute_center_count: int

    @property
    def ready(self):
        return self.steering_center_count is not None


DEFAULT_CAR_PROFILES_PATH = Path(__file__).with_name("car_profiles.json")


def _unique_json_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise CarProfileError("duplicate JSON key: %s" % key)
        result[key] = value
    return result


def load_car_profiles(path=None):
    """Load PC calibration references, separate from the MCU Flash Config V5."""
    source = Path(path) if path is not None else DEFAULT_CAR_PROFILES_PATH
    try:
        data = json.loads(source.read_text(encoding="utf-8-sig"),
                          object_pairs_hook=_unique_json_object)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise CarProfileError("cannot read car profiles %s: %s" % (source, exc)) from exc
    if not isinstance(data, dict):
        raise CarProfileError("car profile document must be a JSON object")
    if type(data.get("schema_version")) is not int or data["schema_version"] != 1:
        raise CarProfileError("car profile schema_version must be 1")
    if data.get("product") != "patrol_vehicle_chassis":
        raise CarProfileError("car profile product must be patrol_vehicle_chassis")
    records = data.get("cars")
    if not isinstance(records, list) or len(records) != 4:
        raise CarProfileError("car profiles must contain exactly cars 1..4")
    profiles = {}
    required = {"car", "tag", "steering_center_count", "absolute_center_count"}
    for record in records:
        if not isinstance(record, dict) or not required.issubset(record):
            raise CarProfileError("car profile requires car, tag and both center counts")
        number = record["car"]
        if type(number) is not int or number not in (1, 2, 3, 4) or number in profiles:
            raise CarProfileError("car profiles require unique integer car numbers 1..4")
        if record["tag"] != "CAR%d" % number:
            raise CarProfileError("car %d tag must be CAR%d" % (number, number))
        center = record["steering_center_count"]
        if center is not None and (type(center) is not int or not -(2**31) <= center < 2**31):
            raise CarProfileError("CAR%d steering_center_count must be int32 or null" % number)
        absolute = record["absolute_center_count"]
        if type(absolute) is not int or absolute != 0:
            raise CarProfileError("CAR%d absolute_center_count must be integer 0" % number)
        profiles[number] = CarProfile(number, record["tag"], center, absolute)
    return profiles


def show_car_profile(profile):
    """Describe the selected PC reference and missing data without opening CAN."""
    print("Profile file: %s" % DEFAULT_CAR_PROFILES_PATH)
    print("Record: car=%d, tag=%s" % (profile.number, profile.tag))
    center = (str(profile.steering_center_count) if profile.ready
              else "<TODO_%s_STEERING_CENTER> (JSON null)" % profile.tag)
    print("  steering_center_count = %s" % center)
    print("    Signed INT32 count from Node 1 object 0x6064 at this car's confirmed mechanical center.")
    print("    A startup position without mechanical-center confirmation is not calibration data.")
    print("  absolute_center_count = %d" % profile.absolute_center_count)
    print("    Required encoder zero after mechanical-zero calibration; not proof it was completed.")
    print("  driver_limits = read 0x201A (minimum) / 0x2019 (maximum), in controller counts.")
    print("    Driver limits are not stored or initialized by this profile.")
    if not profile.ready:
        print("CONTROL DISABLED: replace this car's steering_center_count null with verified data.")
    else:
        print("Reference recorded; current vehicle identity, coordinates and safety gates remain to be verified.")
    print("Offline profile display only; CAN is not opened and no motor commands are sent.")


class Canalyst:
    """Thin ctypes wrapper around ControlCAN.dll (CANalyst-II)."""

    # 500 kbps for the SJA1000-compatible CANalyst-II: BTR0=0x00, BTR1=0x1C.
    TIMING_500K = (0x00, 0x1C)

    def __init__(self, channel=1, dll_path=None):
        self.channel = channel
        self.dev_type = VCI_USBCAN2
        self.dev_index = 0
        self.dll = None
        self._load_dll(dll_path)

    def _load_dll(self, dll_path):
        candidates = []
        if dll_path:
            candidates.append(dll_path)
        candidates += [
            "ControlCAN.dll",
            os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "SysWOW64", "ControlCAN.dll"),
            os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "ControlCAN.dll"),
        ]
        last = None
        for path in candidates:
            try:
                self.dll = ctypes.WinDLL(path)
                break
            except OSError as exc:  # pragma: no cover - exercised on the host PC
                last = exc
        if self.dll is None:
            raise CanalystError(
                "Cannot load ControlCAN.dll (install the CANalyst-II driver). "
                "Tried: %s (%s)" % (candidates, last))
        self._bind()

    def _bind(self):
        dll = self.dll
        dll.VCI_OpenDevice.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
        dll.VCI_OpenDevice.restype = ctypes.c_uint
        dll.VCI_CloseDevice.argtypes = [ctypes.c_uint, ctypes.c_uint]
        dll.VCI_CloseDevice.restype = ctypes.c_uint
        dll.VCI_InitCAN.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint,
                                    ctypes.POINTER(VciInitConfig)]
        dll.VCI_InitCAN.restype = ctypes.c_uint
        dll.VCI_StartCAN.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
        dll.VCI_StartCAN.restype = ctypes.c_uint
        dll.VCI_ResetCAN.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
        dll.VCI_ResetCAN.restype = ctypes.c_uint
        dll.VCI_Transmit.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint,
                                     ctypes.POINTER(VciCanObj), ctypes.c_uint]
        dll.VCI_Transmit.restype = ctypes.c_uint
        dll.VCI_Receive.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint,
                                    ctypes.POINTER(VciCanObj), ctypes.c_uint, ctypes.c_int]
        dll.VCI_Receive.restype = ctypes.c_uint

    def open(self):
        if self.dll.VCI_OpenDevice(self.dev_type, self.dev_index, 0) != 1:
            raise CanalystError("VCI_OpenDevice failed: is the CANalyst-II connected?")
        cfg = VciInitConfig()
        cfg.AccCode = 0x00000000
        cfg.AccMask = 0xFFFFFFFF
        cfg.Filter = 0          # receive all
        cfg.Timing0, cfg.Timing1 = self.TIMING_500K
        cfg.Mode = 0            # normal
        if self.dll.VCI_InitCAN(self.dev_type, self.dev_index, self.channel,
                                ctypes.byref(cfg)) != 1:
            self.close()
            raise CanalystError("VCI_InitCAN failed on channel %d" % (self.channel + 1))
        if self.dll.VCI_StartCAN(self.dev_type, self.dev_index, self.channel) != 1:
            self.close()
            raise CanalystError("VCI_StartCAN failed on channel %d" % (self.channel + 1))

    def close(self):
        if self.dll is not None:
            self.dll.VCI_CloseDevice(self.dev_type, self.dev_index)

    def transmit(self, cob_id, data):
        obj = VciCanObj()
        obj.ID = cob_id
        obj.SendType = 0
        obj.RemoteFlag = 0
        obj.ExternFlag = 0       # 0 = standard 11-bit frame
        obj.DataLen = len(data) & 0x0F
        for i, b in enumerate(data[:8]):
            obj.Data[i] = b
        sent = self.dll.VCI_Transmit(self.dev_type, self.dev_index, self.channel,
                                     ctypes.byref(obj), 1)
        return sent == 1

    def receive(self, timeout_ms):
        """Return (cob_id, bytes) or None."""
        obj = VciCanObj()
        n = self.dll.VCI_Receive(self.dev_type, self.dev_index, self.channel,
                                 ctypes.byref(obj), 1, int(timeout_ms))
        if n != 1:
            return None
        data = bytes(obj.Data[i] for i in range(obj.DataLen & 0x0F))
        return obj.ID, data


class MockCanalyst(Canalyst):
    """No-hardware stand-in so the control logic can be exercised with --dry-run."""

    def __init__(self, channel=1, dll_path=None, car_profile=None):
        self.channel = channel
        self.dll = None
        self._rx = []
        self._objects = {}
        self._status = {1: 0x0040, 2: 0x0040}  # SwitchOnDisabled per node
        self._center = car_profile.steering_center_count if car_profile and car_profile.ready else 0

    def _load_dll(self, dll_path):
        pass

    def open(self):
        print("[dry-run] CAN device opened (channel %d)" % (self.channel + 1))

    def close(self):
        print("[dry-run] CAN device closed")

    def transmit(self, cob_id, data):
        print("[dry-run] TX id=0x%03X data=%s" % (cob_id, data.hex().upper()))
        if cob_id in (0x601, 0x602):
            node = cob_id - 0x600
            cmd, idx_lo, idx_hi, sub = data[0], data[1], data[2], data[3]
            index = idx_lo | (idx_hi << 8)
            if cmd == 0x40:  # read
                value = self._mock_read(node, index, sub)
                width = len(value)
                head = {1: 0x4F, 2: 0x4B, 3: 0x47, 4: 0x43}[width]
                reply = bytes([head, idx_lo, idx_hi, sub]) + value
                self._rx.append((0x580 + node, reply))
            elif cmd in (0x2F, 0x2B, 0x23):
                width = {0x2F: 1, 0x2B: 2, 0x23: 4}[cmd]
                self._objects[node, index, sub] = data[4:4 + width]
                if index == 0x6040 and len(data) >= 6:
                    cw = data[4] | (data[5] << 8)
                    if cw == 6:
                        self._status[node] = 0x1001 if node == 2 else 0x0021
                    elif cw == 7:
                        self._status[node] = 0x1003 if node == 2 else 0x0023
                    elif cw == 15:
                        self._status[node] = 0x1007 if node == 2 else 0x0027
                    elif cw == 0x3F or cw == 0x2F:
                        pass
                self._rx.append((0x580 + node, bytes([0x60, idx_lo, idx_hi, sub, 0, 0, 0, 0])))
        return True

    def receive(self, timeout_ms):
        if self._rx:
            return self._rx.pop(0)
        return None

    def _mock_read(self, node, index, sub):
        if (node, index, sub) in self._objects:
            return self._objects[node, index, sub]
        if node == 2 and index in (0x2000, 0x2001):
            return b"\x00\x00"
        if index in (0x6040, 0x603F):
            return struct.pack("<H", 0)
        if index == 0x6060:
            return bytes([1 if node == 1 else 3])
        if node == 1 and index == 0x607A:
            return struct.pack("<i", 0)
        if node == 1 and index == 0x6064:
            return struct.pack("<i", self._center)
        if node == 1 and index == 0x2019:
            return struct.pack("<i", self._center + 81324)
        if node == 1 and index == 0x201A:
            return struct.pack("<i", self._center - 81324)
        if node == 1 and index == 0x6061:
            return b"\x01"
        if node == 1 and index in (0x6080, 0x6081):
            return struct.pack("<i", 150)
        if node == 1 and index == 0x6041:
            return struct.pack("<H", self._status[1])
        if node == 2 and index == 0x6061:
            return b"\x03"
        if node == 2 and index == 0x6041:
            return struct.pack("<H", self._status[2])
        if node == 2 and index in (0x6069, 0x6869):
            return struct.pack("<i", 0)
        return b"\x00"


# ----------------------------------------------------------------------------
# CANopen master (mirrors the product firmware sequences)
# ----------------------------------------------------------------------------
NODE_STEER = 1
NODE_DRIVE = 2

STEER_COUNTS_PER_DEG = 2710.8
STEER_MAX_ANGLE_DEG = 30.0
STEER_LIMIT_ANGLE_DEG = 30.5

SDO_TIMEOUT_MS = 150


def steering_limit_pair(runtime_center):
    if type(runtime_center) is not int:
        raise CanalystError("confirmed runtime center must be an int32 count")
    margin = int(STEER_COUNTS_PER_DEG * STEER_LIMIT_ANGLE_DEG)
    low, high = runtime_center - margin, runtime_center + margin
    if low < -(2**31) or high >= 2**31:
        raise CanalystError("confirmed runtime center's limit pair overflows int32")
    return low, high


class CanopenMaster:
    def __init__(self, bus, car_profile=None):
        self.bus = bus
        self.car_profile = car_profile
        self.steer_initial_position = None

    # -- low level ----------------------------------------------------------
    def transmit(self, cob_id, data):
        return self.bus.transmit(cob_id, data)

    def _wait(self, node, index, subindex, expect_read, timeout_ms=SDO_TIMEOUT_MS):
        deadline = time.monotonic() + timeout_ms / 1000.0
        while time.monotonic() < deadline:
            frame = self.bus.receive(int(max(1, (deadline - time.monotonic()) * 1000)))
            if frame is None:
                continue
            cob_id, data = frame
            if cob_id != 0x580 + node or len(data) < 4:
                continue
            if data[1] != (index & 0xFF) or data[2] != ((index >> 8) & 0xFF) or data[3] != subindex:
                continue
            if data[0] == 0x80:
                abort = int.from_bytes(data[4:8], "little") if len(data) >= 8 else 0
                return ("abort", abort)
            if expect_read:
                widths = {0x4F: 1, 0x4B: 2, 0x47: 3, 0x43: 4}
                if data[0] in widths and len(data) >= 4 + widths[data[0]]:
                    return ("value", data[4:4 + widths[data[0]]])
            else:
                if data[0] == 0x60:
                    return ("ack", None)
        return ("timeout", None)

    def sdo_write(self, node, index, subindex, value_bytes):
        cmd = {1: 0x2F, 2: 0x2B, 4: 0x23}[len(value_bytes)]
        data = bytes([cmd, index & 0xFF, (index >> 8) & 0xFF, subindex]) + value_bytes
        data = (data + b"\x00" * 8)[:8]
        if not self.transmit(0x600 + node, data):
            return False, "transmit failed"
        result, extra = self._wait(node, index, subindex, expect_read=False)
        if result == "ack":
            return True, None
        if result == "abort":
            return False, "SDO abort 0x%08X" % extra
        return False, "SDO write timeout"

    def sdo_read(self, node, index, subindex=0, deadline=None):
        timeout_ms = SDO_TIMEOUT_MS
        if deadline is not None:
            timeout_ms = min(timeout_ms, int((deadline - time.monotonic()) * 1000))
            if timeout_ms <= 0:
                return None, "deadline expired"
        data = bytes([0x40, index & 0xFF, (index >> 8) & 0xFF, subindex]) + b"\x00" * 4
        if not self.transmit(0x600 + node, data):
            return None, "transmit failed"
        result, extra = self._wait(node, index, subindex, expect_read=True, timeout_ms=timeout_ms)
        if result == "value":
            return extra, None
        if result == "abort":
            return None, "SDO abort 0x%08X" % extra
        return None, "SDO read timeout"

    def sdo_write_u8(self, node, index, subindex, value):
        return self.sdo_write(node, index, subindex, bytes([value & 0xFF]))

    def sdo_write_u16(self, node, index, subindex, value):
        return self.sdo_write(node, index, subindex, struct.pack("<H", value & 0xFFFF))

    def sdo_write_i32(self, node, index, subindex, value):
        return self.sdo_write(node, index, subindex, struct.pack("<i", int(value)))

    def nmt(self, command, node):
        self.transmit(0x000, bytes([command, node]))

    def _expect_u16(self, node, index, subindex, mask, expected, label):
        raw, err = self.sdo_read(node, index, subindex)
        if err:
            raise CanalystError("%s read failed: %s" % (label, err))
        value = struct.unpack("<H", raw)[0]
        if (value & mask) != expected or (value & 0x08):
            raise CanalystError("%s unexpected status 0x%04X" % (label, value))

    def _expect_u8(self, node, index, subindex, expected, label):
        raw, err = self.sdo_read(node, index, subindex)
        if err:
            raise CanalystError("%s read failed: %s" % (label, err))
        if raw[0] != expected:
            raise CanalystError("%s got 0x%02X, expected 0x%02X" % (label, raw[0], expected))

    # -- drive (Node 2) -----------------------------------------------------
    def drive_enable(self):
        self.nmt(0x80, NODE_DRIVE)                       # enter pre-operational
        self._write(self.sdo_write_u8(NODE_DRIVE, 0x6060, 0, 3), "drive 6060=3")
        # 6060(OperationMode) 才是权威；6061(DisplayMode) 在该驱动器固件上回 1，
        # 与厂商“速度环低 4 位=1”一致，不能拿它当 6060 来校验。
        self._expect_u8(NODE_DRIVE, 0x6060, 0, 3, "drive mode")
        self._write(self.sdo_write_i32(NODE_DRIVE, 0x60FF, 0, 0), "drive zero left")
        self._write(self.sdo_write_i32(NODE_DRIVE, 0x68FF, 0, 0), "drive zero right")
        self._write(self.sdo_write_u16(NODE_DRIVE, 0x6040, 0, 6), "drive shutdown")
        self._expect_u16(NODE_DRIVE, 0x6041, 0, 0x4F, 1, "drive ready")
        self._write(self.sdo_write_u16(NODE_DRIVE, 0x6040, 0, 7), "drive switch-on")
        self._expect_u16(NODE_DRIVE, 0x6041, 0, 0x4F, 3, "drive switched-on")
        self._write(self.sdo_write_u16(NODE_DRIVE, 0x6040, 0, 15), "drive enable")
        self._expect_u16(NODE_DRIVE, 0x6041, 0, 0x4F, 7, "drive enabled")

    def drive_set_speed(self, rpm):
        # Vehicle-forward = left positive, right negative (right wheel mirror-mounted).
        self._write(self.sdo_write_i32(NODE_DRIVE, 0x60FF, 0, rpm), "drive left target")
        self._write(self.sdo_write_i32(NODE_DRIVE, 0x68FF, 0, -rpm), "drive right target")

    def drive_disable(self):
        self._write(self.sdo_write_u16(NODE_DRIVE, 0x6040, 0, 6), "drive shutdown")

    def drive_actual_speeds(self, deadline=None):
        left, le = self.sdo_read(NODE_DRIVE, 0x6069, 0, deadline=deadline)
        right, re = self.sdo_read(NODE_DRIVE, 0x6869, 0, deadline=deadline)
        if le or re or len(left) != 4 or len(right) != 4:
            return None
        return struct.unpack("<i", left)[0], struct.unpack("<i", right)[0]

    # -- steering (Node 1) --------------------------------------------------
    def steering_enable(self, profile_rpm):
        if self.car_profile is None or not self.car_profile.ready:
            raise CanalystError("select a car with a recorded mechanical center before enabling steering")
        self.steer_min = self._read_i32(NODE_STEER, 0x201A, "steer min limit")
        self.steer_max = self._read_i32(NODE_STEER, 0x2019, "steer max limit")
        self.steer_initial_position = self._read_i32(NODE_STEER, 0x6064, "steer current position")
        self.center = self.car_profile.steering_center_count
        self._validate_steering_limits()
        if not 1 <= profile_rpm <= 300:
            raise CanalystError("steer profile speed must be 1..300 rpm")
        print("  steer %s recorded-center=%d initial-position=%d limits=[%d, %d]"
              % (self.car_profile.tag, self.center, self.steer_initial_position,
                 self.steer_min, self.steer_max))
        self.nmt(0x01, NODE_STEER)                       # start remote node
        self._write(self.sdo_write_u8(NODE_STEER, 0x6060, 0, 1), "steer 6060=1")
        self._expect_u8(NODE_STEER, 0x6061, 0, 1, "steer mode")
        self._write(self.sdo_write_i32(NODE_STEER, 0x6080, 0, 300), "steer max speed")
        self._write(self.sdo_write_i32(NODE_STEER, 0x6081, 0, profile_rpm), "steer profile speed")
        # Hold the actual startup position. Selecting a profile never commands homing.
        self._write(self.sdo_write_i32(NODE_STEER, 0x607A, 0, self.steer_initial_position),
                    "steer hold position")
        self._write(self.sdo_write_u16(NODE_STEER, 0x6040, 0, 6), "steer shutdown")
        self._expect_u16(NODE_STEER, 0x6041, 0, 0x6F, 0x21, "steer ready")
        self._write(self.sdo_write_u16(NODE_STEER, 0x6040, 0, 7), "steer switch-on")
        self._expect_u16(NODE_STEER, 0x6041, 0, 0x6F, 0x23, "steer switched-on")
        self._write(self.sdo_write_u16(NODE_STEER, 0x6040, 0, 15), "steer enable")
        self._expect_u16(NODE_STEER, 0x6041, 0, 0x6F, 0x27, "steer enabled")

    def steering_set_angle(self, angle_deg):
        self._validate_steering_limits()
        angle_deg = max(-STEER_MAX_ANGLE_DEG, min(STEER_MAX_ANGLE_DEG, angle_deg))
        requested = int(round(self.center + angle_deg * STEER_COUNTS_PER_DEG))
        count = max(self.steer_min, min(self.steer_max, requested))
        print("  steer request=%d target=%d center=%d limits=[%d, %d]%s"
              % (requested, count, self.center, self.steer_min, self.steer_max,
                 " CLAMPED" if count != requested else ""))
        self._write(self.sdo_write_i32(NODE_STEER, 0x607A, 0, count), "steer target")
        # bit 5 keeps immediate set-point replacement; clear bit 4 afterwards.
        self._write(self.sdo_write_u16(NODE_STEER, 0x6040, 0, 0x3F), "steer trigger")
        self._write(self.sdo_write_u16(NODE_STEER, 0x6040, 0, 0x2F), "steer clear-trigger")

    def steering_stop(self):
        self._write(self.sdo_write_u16(NODE_STEER, 0x6040, 0, 6), "steer shutdown")

    def restore_steering_limits(self, runtime_center):
        """Maintenance only: apply a confirmed current-epoch center to RAM limits.

        This never enables a node, writes a target, saves EEP or changes calibration.
        A profile's historical count is deliberately not used as runtime_center.
        """
        if self.car_profile is None or not self.car_profile.ready:
            raise CanalystError("select a calibrated car before restoring limits")
        low, high = steering_limit_pair(runtime_center)
        print("RAM limits plan: center=%d minimum=%d maximum=%d"
              % (runtime_center, low, high))
        # Stop both channels independently, even if one request fails.
        _disable_outputs(self, strict=True)
        deadline = time.monotonic() + 2.0
        self.wait_for_standstill(deadline)
        self._require_disabled(wait=True)
        actual = self._read_i32(NODE_STEER, 0x6064, "steer current position")
        if abs(actual - runtime_center) > STEER_COUNTS_PER_DEG * STEER_MAX_ANGLE_DEG:
            raise CanalystError("actual position is outside the confirmed center's +/-30 degree envelope")
        old_low = self._read_i32(NODE_STEER, 0x201A, "old minimum limit")
        old_high = self._read_i32(NODE_STEER, 0x2019, "old maximum limit")
        if old_low >= old_high and (old_low, old_high) != (0, 0):
            raise CanalystError("unexpected invalid old limits; only the known zero/zero pair can be restored")
        print("Old RAM limits: minimum=%d maximum=%d actual=%d" % (old_low, old_high, actual))
        # Avoid an inverted intermediate pair, including equal-zero factory limits.
        order = ((0x2019, high), (0x201A, low)) if low >= old_high else ((0x201A, low), (0x2019, high))
        try:
            for index, value in order:
                self._require_disabled()
                if abs(self._read_i32(NODE_STEER, 0x6064, "stationary steering position") - actual) > 27:
                    raise CanalystError("steering position moved during limit maintenance")
                self._write(self.sdo_write_i32(NODE_STEER, index, 0, value), "RAM limit 0x%04X" % index)
                if self._read_i32(NODE_STEER, index, "RAM limit readback") != value:
                    raise CanalystError("RAM limit readback mismatch at 0x%04X" % index)
            if (self._read_i32(NODE_STEER, 0x201A, "minimum readback"),
                    self._read_i32(NODE_STEER, 0x2019, "maximum readback")) != (low, high):
                raise CanalystError("final RAM limit pair mismatch")
            self._require_disabled()
        except BaseException:
            print("RAM limit restoration incomplete or uncertain. Keep outputs disabled; "
                  "re-read both limits before any later operation. No automatic retry or EEP save.")
            raise
        print("RAM limits read back; both nodes remain disabled. Mechanical motion is untested.")
        print("Power cycling invalidates this coordinate confirmation; verify again before use.")

    def _read_integer(self, node, index, width, signed=False, deadline=None):
        raw, err = self.sdo_read(node, index, deadline=deadline)
        if err or len(raw) != width:
            raise CanalystError("node %d 0x%04X read failed: %s" %
                                (node, index, err or "invalid width"))
        return int.from_bytes(raw, "little", signed=signed)

    def _require_disabled(self, wait=False):
        deadline = time.monotonic() + (0.5 if wait else 0)
        for node in (NODE_STEER, NODE_DRIVE):
            while True:
                status = self._read_integer(node, 0x6041, 2)
                enabled = (status & (0x6F if node == NODE_STEER else 0x4F)) == (0x27 if node == NODE_STEER else 7)
                if status & 8 or (enabled and time.monotonic() >= deadline):
                    raise CanalystError("node %d is faulted or still enabled: 0x%04X" % (node, status))
                if not enabled:
                    state = status & (0x6F if node == NODE_STEER else 0x4F)
                    if state not in ((0x21, 0x23, 0x40) if node == NODE_STEER else (1, 3, 0x40)):
                        raise CanalystError("node %d has unknown disabled state: 0x%04X" % (node, status))
                    break
                time.sleep(0.02)
        for index in (0x2000, 0x2001):
            if self._read_integer(NODE_DRIVE, index, 2):
                raise CanalystError("drive vendor fault at 0x%04X; no fault reset attempted" % index)

    def wait_for_standstill(self, deadline):
        confirmations = 0
        while deadline - time.monotonic() >= 0.001:
            speeds = self.drive_actual_speeds(deadline=deadline)
            if time.monotonic() >= deadline:
                break
            if speeds is None:
                raise CanalystError("fresh paired drive feedback unavailable; no read retry")
            if all(abs(value) <= 20 for value in speeds):
                confirmations += 1
                if confirmations == 3:
                    return
            else:
                confirmations = 0
            time.sleep(min(0.02, max(0, deadline - time.monotonic())))
        raise CanalystError("zero-speed confirmation failed within 2 s")

    def poll_motion_feedback(self):
        """Bounded driver checks; independent encoder/MCU OTA gates remain separate."""
        deadline = time.monotonic() + 0.5
        for node, mask, expected in ((NODE_STEER, 0x6F, 0x27), (NODE_DRIVE, 0x4F, 7)):
            status = self._read_integer(node, 0x6041, 2, deadline=deadline)
            if status & 8 or status & mask != expected:
                raise CanalystError("node %d no longer enabled or faulted: 0x%04X" % (node, status))
        position = self._read_integer(NODE_STEER, 0x6064, 4, signed=True, deadline=deadline)
        if abs(position - self.center) > STEER_COUNTS_PER_DEG * STEER_LIMIT_ANGLE_DEG:
            raise CanalystError("steering feedback outside +/-30.5 degree protection envelope")
        if self.drive_actual_speeds(deadline=deadline) is None:
            raise CanalystError("fresh paired drive feedback unavailable")
        for index in (0x2000, 0x2001):
            if self._read_integer(NODE_DRIVE, index, 2, deadline=deadline):
                raise CanalystError("drive vendor fault at 0x%04X" % index)
        if time.monotonic() >= deadline:
            raise CanalystError("driver feedback collection exceeded 500 ms")

    def steering_diagnostics(self):
        """Read Node 1 only; never send NMT, enable, targets or limit writes."""
        fields = (
            (0x6040, "controlword", 2, False),
            (0x6041, "statusword", 2, False),
            (0x6060, "requested_mode", 1, True),
            (0x6061, "displayed_mode", 1, True),
            (0x6064, "actual_position", 4, True),
            (0x607A, "target_position", 4, True),
            (0x201A, "minimum_limit", 4, True),
            (0x2019, "maximum_limit", 4, True),
            (0x6080, "maximum_speed", 4, False),
            (0x6081, "profile_speed", 4, False),
            (0x603F, "error_code", 2, False),
            (0x1001, "error_register", 1, False),
        )
        values = {}
        print("[steer diag] Node 1 SDO reads only; values describe this snapshot.")
        for index, label, width, signed in fields:
            raw, err = self.sdo_read(NODE_STEER, index, 0)
            if err:
                # Some vendor objects are optional. Keep the remaining reads.
                print("  0x%04X %-18s ERROR %s" % (index, label, err))
                continue
            if len(raw) != width:
                print("  0x%04X %-18s ERROR width=%d expected=%d"
                      % (index, label, len(raw), width))
                continue
            value = int.from_bytes(raw, "little", signed=signed)
            values[index] = value
            print("  0x%04X %-18s %d (0x%s)"
                  % (index, label, value, raw[::-1].hex().upper()))

        if 0x6041 in values:
            status = values[0x6041]
            print("  state=0x%02X enabled=%s fault=%s setpoint_ack=%s "
                  "target_reached=%s internal_limit=%s"
                  % (status & 0x6F, (status & 0x6F) == 0x27,
                     bool(status & 0x08), bool(status & 0x1000),
                     bool(status & 0x0400), bool(status & 0x0800)))
        if all(index in values for index in (0x201A, 0x2019, 0x6064)):
            low, high, actual = values[0x201A], values[0x2019], values[0x6064]
            print("  limits_valid=%s actual_in_limits=%s"
                  % (low < high, low < high and low <= actual <= high))
        if 0x607A in values and 0x6064 in values:
            delta = values[0x607A] - values[0x6064]
            print("  target_minus_actual=%d counts (%.3f deg at configured ratio)"
                  % (delta, delta / STEER_COUNTS_PER_DEG))
        print("  A stopped script normally leaves Node 1 disabled; this alone is not a fault.")
        return values

    # -- helpers ------------------------------------------------------------
    def _validate_steering_limits(self):
        if self.steer_min >= self.steer_max:
            raise CanalystError(
                "invalid steering limits: 201A=%d, 2019=%d; refusing motion. "
                "Confirm this vehicle's calibration and restore valid limits first."
                % (self.steer_min, self.steer_max))
        if not self.steer_min <= self.center <= self.steer_max:
            raise CanalystError(
                "recorded steering center %d is outside limits [%d, %d]; refusing motion"
                % (self.center, self.steer_min, self.steer_max))
        if (self.steer_initial_position is not None and
                not self.steer_min <= self.steer_initial_position <= self.steer_max):
            raise CanalystError(
                "steering initial position %d is outside limits [%d, %d]; refusing motion"
                % (self.steer_initial_position, self.steer_min, self.steer_max))

    def _read_i32(self, node, index, label):
        raw, err = self.sdo_read(node, index, 0)
        if err or len(raw) != 4:
            raise CanalystError("%s read failed: %s" % (label, err or "invalid width"))
        return struct.unpack("<i", raw)[0]

    @staticmethod
    def _write(result, label):
        ok, err = result
        if not ok:
            raise CanalystError("%s failed: %s" % (label, err))
        print("  ok  %s" % label)


# ----------------------------------------------------------------------------
# Direction mapping (edit here to change the key layout)
# ----------------------------------------------------------------------------
# Standard WASD: a = left, d = right. "wa" = forward + left, "wd" = forward + right.
# NOTE: the original request labelled these two combos the other way around;
# flip the 'wa' and 'wd' entries below if you want the literal labels instead.
def direction_table(speed, turn_speed, angle):
    return {
        frozenset("w"):  ("forward",        speed,      0.0),
        frozenset("s"):  ("backward",       -speed,     0.0),
        frozenset("a"):  ("left",           0,           -angle),
        frozenset("d"):  ("right",          0,           angle),
        frozenset("wa"): ("forward+left",   speed,      -angle),
        frozenset("wd"): ("forward+right",  speed,       angle),
        frozenset("sa"): ("backward+left",  -speed,     -angle),
        frozenset("sd"): ("backward+right", -speed,      angle),
        frozenset(" "):  ("brake",          0,          0.0),
    }


# ----------------------------------------------------------------------------
# Keyboard (Windows GetAsyncKeyState for true simultaneous key detection)
# ----------------------------------------------------------------------------
def keys_held():
    """Return a frozenset of currently held control keys (lower-case letters/space)."""
    if sys.platform != "win32":
        raise CanalystError("This tool requires Windows (ControlCAN.dll + GetAsyncKeyState).")
    user32 = ctypes.windll.user32
    held = set()
    for key, vk in (("w", ord("W")), ("a", ord("A")), ("s", ord("S")), ("d", ord("D")),
                    (" ", 0x20)):
        if user32.GetAsyncKeyState(vk) & 0x8000:
            held.add(key)
    return frozenset(held)


def escape_pressed():
    return sys.platform == "win32" and bool(ctypes.windll.user32.GetAsyncKeyState(0x1B) & 0x8000)


def wait_for_key(key):
    """Block until a single key is pressed (used for the startup confirmation)."""
    if sys.platform == "win32":
        import msvcrt
        while True:
            if msvcrt.kbhit():
                ch = msvcrt.getwch().lower()
                if ch == key:
                    return
    else:  # pragma: no cover - Windows-only tool
        raise CanalystError("Windows required for interactive input.")


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------
#可在此修改速度和转向角度
def main(argv=None):
    parser = argparse.ArgumentParser(description="Chassis car offline CAN2 keyboard remote")
    parser.add_argument("--car", type=int, choices=(1, 2, 3, 4),
                        help="vehicle profile number; required for control, optional for read-only diagnosis")
    parser.add_argument("--channel", type=int, default=1,
                        help="CANalyst-II channel index: 0 = channel 1, 1 = channel 2 (default)")
    parser.add_argument("--speed", type=int, default=500, help="forward/backward speed in rpm (default: 500)")
    parser.add_argument("--turn-speed", type=int, default=80,
                        help="legacy compatibility option; A/D currently request zero drive speed")
    parser.add_argument("--angle", type=float, default=25.0, help="turn steering angle in degrees (default: 25)")
    parser.add_argument("--profile-rpm", type=int, default=150, help="steering profile speed (6081)")
    parser.add_argument("--dll", help="path to ControlCAN.dll")
    parser.add_argument("--dry-run", action="store_true", help="exercise logic without hardware")
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--diagnose-steering", action="store_true",
                         help="read Node 1 state only, then exit without enabling either motor")
    actions.add_argument("--show-car-profile", action="store_true",
                         help="show --car reference and missing data, then exit without opening CAN")
    actions.add_argument("--can2-status", action="store_true",
                         help="query MCU CAN2 status only; no power, enable or motion request")
    actions.add_argument("--restore-steering-limits", action="store_true",
                         help="maintenance only: stop/disable nodes, restore RAM limits, read back and exit")
    parser.add_argument("--confirmed-runtime-center", type=int,
                        help="operator-confirmed mechanical center in THIS power cycle's driver coordinates; "
                             "required only with --restore-steering-limits; never inferred from a profile")
    args = parser.parse_args(argv)

    if args.channel not in (0, 1):
        parser.error("--channel must be 0 (channel 1) or 1 (channel 2)")
    if not 0 < args.speed <= 500:
        parser.error("--speed must be 1..500 rpm")
    if not 0 < args.turn_speed <= 500:
        parser.error("--turn-speed must be 1..500 rpm")
    if not math.isfinite(args.angle) or not 0 < args.angle <= 25:
        parser.error("--angle must be positive and within 0..25 degrees")
    if not 1 <= args.profile_rpm <= 300:
        parser.error("--profile-rpm must be 1..300 rpm")
    if args.restore_steering_limits != (args.confirmed_runtime_center is not None):
        parser.error("--restore-steering-limits and --confirmed-runtime-center must be used together")
    if args.restore_steering_limits:
        try:
            low, high = steering_limit_pair(args.confirmed_runtime_center)
        except CanalystError as exc:
            parser.error(str(exc))
        print("Maintenance plan: confirmed runtime center=%d, RAM limits=[%d, %d]; no motion or EEP save."
              % (args.confirmed_runtime_center, low, high))

    selected_car = None
    if args.car is None and not (args.diagnose_steering or args.can2_status):
        parser.error("--car is required for control or profile display; choose 1..4")
    if args.car is not None:
        try:
            selected_car = load_car_profiles()[args.car]
        except CarProfileError as exc:
            if not args.diagnose_steering:
                parser.error(str(exc))
            print("Car profile unavailable; continuing read-only diagnosis: %s" % exc)
        if selected_car is not None:
            if not args.diagnose_steering and not args.show_car_profile and not selected_car.ready:
                parser.error(
                    "%s has no recorded calibration center; control is disabled. "
                    "Set steering_center_count for car=%d in %s after calibration; "
                    "inspect with --car %d --show-car-profile."
                    % (selected_car.tag, selected_car.number, DEFAULT_CAR_PROFILES_PATH,
                       selected_car.number))
            print("Selected %s: recorded steering center=%s, absolute center=%d"
                  % (selected_car.tag, selected_car.steering_center_count,
                     selected_car.absolute_center_count))
            print("Profile selection does not identify the connected car or verify startup coordinates.")

    if args.show_car_profile:
        show_car_profile(selected_car)
        return 0

    if not args.diagnose_steering and not args.restore_steering_limits:
        from chassis_can2_keyboard import run, dry_run
        return dry_run(args) if args.dry_run else run(args)

    table = direction_table(args.speed, args.turn_speed, args.angle)
    print("Direction mapping (WASD):")
    for combo, (name, rpm, angle) in sorted(table.items(), key=lambda kv: sorted(kv[0])):
        if combo == frozenset(" "):
            print("  [space]      brake")
        else:
            print("  %-12s %-16s speed=%+4d rpm  steer=%+5.1f deg"
                  % ("+".join(sorted(combo)), name, rpm, angle))
    print()

    try:
        bus = (MockCanalyst(args.channel, car_profile=selected_car) if args.dry_run
               else Canalyst(args.channel, args.dll))
        master = CanopenMaster(bus, car_profile=selected_car)
    except CanalystError as exc:
        print("Device init failed: %s" % exc)
        return 1
    if args.diagnose_steering:
        try:
            bus.open()
            values = master.steering_diagnostics()
            return 0 if 0x6041 in values else 1
        except Exception as exc:
            print("Steering diagnosis failed: %s" % exc)
            return 1
        finally:
            bus.close()
    if args.restore_steering_limits:
        opened = False
        try:
            bus.open()
            opened = True
            master.restore_steering_limits(args.confirmed_runtime_center)
            return 0
        except (Exception, KeyboardInterrupt) as exc:
            print("RAM limit maintenance failed: %s" % exc)
            return 1
        finally:
            if opened:
                _disable_outputs(master)
            bus.close()
    return 1  # All supported actions return above.


def _safe_reverse(master, new_rpm, still_requested=None):
    """Zero speed, confirm |left|<=20 and |right|<=20 for 3 fresh samples, then apply."""
    print("direction change: zeroing speed first ...")
    deadline = time.monotonic() + 2.0
    try:
        master.drive_set_speed(0)
        master.wait_for_standstill(deadline)
    except BaseException:
        print("reversal failed: disabling drive; control loop must exit")
        master.drive_disable()
        raise
    if still_requested is not None and not still_requested():
        print("  direction request released during braking; keeping zero speed")
        return 0
    print("  applying speed %+d rpm" % new_rpm)
    master.drive_set_speed(new_rpm)
    return new_rpm


def _disable_outputs(master, strict=False):
    """Attempt each zero/disable separately so one failed SDO cannot skip another."""
    actions = (
        ("drive zero left", lambda: master.sdo_write_i32(NODE_DRIVE, 0x60FF, 0, 0)),
        ("drive zero right", lambda: master.sdo_write_i32(NODE_DRIVE, 0x68FF, 0, 0)),
        ("drive shutdown", lambda: master.sdo_write_u16(NODE_DRIVE, 0x6040, 0, 6)),
        ("steer shutdown", lambda: master.sdo_write_u16(NODE_STEER, 0x6040, 0, 6)),
    )
    failures = []
    for label, action in actions:
        try:
            master._write(action(), label)
        except Exception as exc:
            print("Shutdown error (%s): %s" % (label, exc))
            failures.append(label)
    if strict and failures:
        raise CanalystError("shutdown requests failed: " + ", ".join(failures))


if __name__ == "__main__":
    raise SystemExit(main())

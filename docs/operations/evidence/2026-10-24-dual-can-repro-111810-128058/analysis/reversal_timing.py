#!/usr/bin/env python3
"""Measure commanded drive-direction reversal timing from the CAN capture."""

from __future__ import annotations

import json
import struct
from pathlib import Path


FRAMES = Path(__file__).resolve().parents[1] / "frames.jsonl"


def main() -> int:
    commands: list[tuple[float, int]] = []
    rpm_samples: list[tuple[float, int, int]] = []

    for line in FRAMES.open(encoding="utf-8"):
        frame = json.loads(line)
        if frame["bus"] != "CAN1":
            continue
        decoded = frame.get("decoded") or {}
        if decoded.get("kind") != "sdo":
            continue
        if frame["id"] == 0x602 and decoded.get("index") == 0x60FF and decoded.get("action") == "write_request":
            raw = bytes.fromhex(frame["data"].replace(" ", ""))
            commands.append((float(frame["elapsed_s"]), struct.unpack("<i", raw[4:8])[0]))
        elif frame["id"] == 0x582 and decoded.get("index") == 0x6069 and decoded.get("action") == "read_response":
            rpm_samples.append((float(frame["elapsed_s"]), int(decoded["value"]), 0))
        elif frame["id"] == 0x582 and decoded.get("index") == 0x6869 and decoded.get("action") == "read_response":
            rpm_samples.append((float(frame["elapsed_s"]), int(decoded["value"]), 1))

    transitions = []
    previous_direction = 0
    last_nonzero_s: float | None = None
    zeros: list[float] = []
    for elapsed, value in commands:
        current_direction = (value > 0) - (value < 0)
        if current_direction == 0:
            zeros.append(elapsed)
            continue
        if previous_direction and current_direction != previous_direction:
            transitions.append(
                {
                    "last_old_direction_s": last_nonzero_s,
                    "first_zero_s": zeros[0] if zeros else None,
                    "first_new_direction_s": elapsed,
                    "old_direction": previous_direction,
                    "new_direction": current_direction,
                    "direction_to_direction_s": (
                        elapsed - last_nonzero_s if last_nonzero_s is not None else None
                    ),
                }
            )
        zeros = []
        last_nonzero_s = elapsed
        previous_direction = current_direction

    for item in transitions:
        zero_s = item["first_zero_s"]
        end_s = item["first_new_direction_s"]
        if zero_s is None:
            item["both_le_20_s"] = None
            continue
        latest = [None, None]
        stopped_s = None
        for elapsed, value, wheel in rpm_samples:
            if elapsed < zero_s:
                continue
            if elapsed >= end_s:
                break
            latest[wheel] = value
            if latest[0] is not None and latest[1] is not None and abs(latest[0]) <= 20 and abs(latest[1]) <= 20:
                stopped_s = elapsed
                break
        item["both_le_20_s"] = stopped_s

    print(f"drive target writes: {len(commands)}")
    print(f"direction reversals: {len(transitions)}")
    for item in transitions:
        zero_text = "none" if item["first_zero_s"] is None else f"{item['first_zero_s']:.3f}"
        print(
            f"last {item['last_old_direction_s']:.3f}s zero {zero_text} "
            f"opposite {item['first_new_direction_s']:.3f}s "
            f"{item['old_direction']:+d}->{item['new_direction']:+d} "
            f"total {item['direction_to_direction_s']:.3f}s "
            f"both_le_20={item['both_le_20_s']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

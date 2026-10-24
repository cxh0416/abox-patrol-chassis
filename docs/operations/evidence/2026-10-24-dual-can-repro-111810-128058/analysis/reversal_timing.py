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
    last_same_direction: float | None = None
    zeros: list[float] = []
    for elapsed, value in commands:
        current_direction = (value > 0) - (value < 0)
        if current_direction == 0:
            zeros.append(elapsed)
            continue
        if previous_direction and current_direction != previous_direction:
            transitions.append(
                {
                    "last_old_direction_s": last_same_direction,
                    "first_zero_s": min(zeros) if zeros else None,
                    "first_new_direction_s": elapsed,
                    "old_direction": previous_direction,
                    "new_direction": current_direction,
                    "direction_to_direction_s": (
                        elapsed - last_same_direction if last_same_direction is not None else None
                    ),
                }
            )
        if current_direction != previous_direction or last_same_direction is None:
            zeros = []
            last_same_direction = elapsed
        previous_direction = current_direction

    print(f"drive target writes: {len(commands)}")
    print(f"direction reversals: {len(transitions)}")
    for item in transitions:
        zero_text = "none" if item["first_zero_s"] is None else f"{item['first_zero_s']:.3f}"
        print(
            f"last {item['last_old_direction_s']:.3f}s zero {zero_text} "
            f"opposite {item['first_new_direction_s']:.3f}s "
            f"{item['old_direction']:+d}->{item['new_direction']:+d} "
            f"total {item['direction_to_direction_s']:.3f}s"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

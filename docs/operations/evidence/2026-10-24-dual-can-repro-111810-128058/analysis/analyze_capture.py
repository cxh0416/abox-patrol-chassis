#!/usr/bin/env python3
"""Summarize the dual-channel CAN capture without modifying raw evidence."""

from __future__ import annotations

import collections
import json
from pathlib import Path


EVIDENCE = Path(__file__).resolve().parents[1]
FRAMES = EVIDENCE / "frames.jsonl"
EVENTS = EVIDENCE / "events.jsonl"
SUMMARY = EVIDENCE / "summary.json"
OUTPUT = Path(__file__).resolve().parent / "capture-summary.json"
GAP_IDS = {
    ("CAN1", 0x181),
    ("CAN1", 0x281),
    ("CAN1", 0x701),
    ("CAN1", 0x702),
    ("CAN2", 0x10A),
    ("CAN2", 0x10B),
}
def main() -> int:
    total = 0
    bus_counts: collections.Counter[str] = collections.Counter()
    id_counts: collections.Counter[str] = collections.Counter()
    first_seen: dict[str, float] = {}
    last_seen: dict[str, float] = {}
    last_data: dict[str, str] = {}
    gaps: list[dict[str, object]] = []
    previous: dict[tuple[str, int], float] = {}
    emcy: list[dict[str, object]] = []
    bootups: collections.Counter[tuple[float, int]] = collections.Counter()
    nmt: list[dict[str, object]] = []
    write_counts: collections.Counter[str] = collections.Counter()

    for line in FRAMES.open(encoding="utf-8"):
        frame = json.loads(line)
        elapsed = float(frame["elapsed_s"])
        bus = str(frame["bus"])
        can_id = int(frame["id"])
        key = (bus, can_id)
        label = f"{bus}.{can_id:03X}"
        total += 1
        bus_counts[bus] += 1
        id_counts[label] += 1
        first_seen.setdefault(label, elapsed)
        last_seen[label] = elapsed
        last_data[label] = str(frame["data"])

        if key in GAP_IDS:
            if key in previous and elapsed - previous[key] > 0.25:
                gaps.append({
                    "bus": bus,
                    "id": f"0x{can_id:03X}",
                    "last_s": previous[key],
                    "next_s": elapsed,
                    "gap_s": elapsed - previous[key],
                })
            previous[key] = elapsed

        decoded = frame.get("decoded") or {}
        kind = decoded.get("kind")
        action = decoded.get("action")
        if kind == "emcy":
            emcy.append({
                "elapsed_s": elapsed,
                "node": decoded["node"],
                "error_code": decoded["error_code"],
                "error_register": decoded["error_register"],
                "raw": frame["data"],
            })
        elif kind == "heartbeat" and decoded.get("bootup"):
            bootups[(round(elapsed, 3), int(decoded["node"]))] += 1
        elif kind == "nmt":
            nmt.append({
                "elapsed_s": elapsed,
                "command": decoded["command"],
                "node": decoded["node"],
            })
        elif kind == "sdo":
            node = int(decoded["node"])
            index = int(decoded["index"])
            subindex = int(decoded["subindex"])
            if action == "write_request":
                write_counts[f"{node}:0x{index:04X}:{subindex}"] += 1

    summary = json.loads(SUMMARY.read_text(encoding="utf-8"))
    events = [
        event
        for event in (json.loads(line) for line in EVENTS.open(encoding="utf-8"))
        if event.get("kind") != "snapshot"
    ]
    grouped_bootups = [
        {"elapsed_s": elapsed, "node": node, "frame_count": count}
        for (elapsed, node), count in sorted(bootups.items())
    ]
    result = {
        "capture": {
            "duration_s": max(last_seen.values()),
            "total_frames": total,
            "frames_by_bus": dict(sorted(bus_counts.items())),
        },
        "ids": {
            label: {
                "count": id_counts[label],
                "first_s": first_seen[label],
                "last_s": last_seen[label],
                "last_data": last_data[label],
            }
            for label in sorted(id_counts)
        },
        "gaps_over_250ms": gaps,
        "sdo": {
            "completed": summary["sdo_completed"],
            "abort_events": summary["sdo_aborts"],
            "unanswered_events": summary["unanswered_sdo_events"],
            "maximum_observed_reply_ms": summary["max_observed_reply_ms"],
            "event_details": events,
            "write_requests_by_object": dict(sorted(write_counts.items())),
        },
        "emcy": emcy,
        "bootups": grouped_bootups,
        "nmt": nmt,
    }
    OUTPUT.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "capture": result["capture"],
                "gaps_over_250ms": gaps,
                "sdo": result["sdo"],
                "emcy": emcy,
                "bootups": grouped_bootups,
                "nmt": nmt,
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

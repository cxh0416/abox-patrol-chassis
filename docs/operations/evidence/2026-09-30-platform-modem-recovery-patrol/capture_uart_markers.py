import json
import time

import serial

events = []
counts = {"RDY": 0, "QMTOPEN": 0, "QMTCONN": 0, "QMTSUB": 0, "MQTT_READY": 0}
pending = bytearray()
started = time.time()
with serial.Serial("COM11", 19200, timeout=0.2) as port:
    while time.time() - started < 120:
        pending.extend(port.read(512))
        while b"\n" in pending:
            line, _, tail = pending.partition(b"\n")
            pending = bytearray(tail)
            stripped = line.strip()
            for marker, needle in (
                ("RDY", b"RDY"), ("QMTOPEN", b"+QMTOPEN:"),
                ("QMTCONN", b"+QMTCONN:"), ("QMTSUB", b"+QMTSUB:"),
                ("MQTT_READY", b"mqtt ready"),
            ):
                if needle in stripped:
                    counts[marker] += 1
                    events.append({"t": round(time.time(), 3), "marker": marker,
                                   "standalone": stripped == needle})
        if len(pending) > 2048:
            pending.clear()
print(json.dumps({"started_at": round(started, 3), "counts": counts,
                  "events": events}, sort_keys=True))

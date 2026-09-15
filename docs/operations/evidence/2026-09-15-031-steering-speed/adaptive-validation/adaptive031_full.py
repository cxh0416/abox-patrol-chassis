import json
import os
import sqlite3
import sys
import time
import uuid

os.chdir("/opt/device-ota-service/current")
sys.path.insert(0, ".")
os.environ["OTA_DB_PATH"] = "/var/lib/device-ota-service/ota.sqlite3"
os.environ["PATROL_CHASSIS_CONTROL_DB_PATH"] = "/var/lib/device-ota-service/chassis-control.sqlite3"

from device_ota import db

PRODUCT = "patrol_vehicle_chassis"
VID = "PATROL-CHASSIS-000"
terminal = {"succeeded", "failed", "expired"}
reader = sqlite3.connect("file:/var/lib/device-ota-service/ota.sqlite3?mode=ro", uri=True)
reader.row_factory = sqlite3.Row


def operation(capability, params=None, timeout=35.0):
    operation_id = db.create_operation(
        PRODUCT, VID, capability, params or {}, "codex-maintenance", str(uuid.uuid4())
    )
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        row = reader.execute(
            "select state,result_json,error from operations where id=?", (operation_id,)
        ).fetchone()
        if row and row["state"] in terminal:
            result = json.loads(row["result_json"] or "{}")
            print("OP", capability, operation_id, row["state"], json.dumps(result), flush=True)
            if row["state"] != "succeeded":
                raise RuntimeError(f"{capability}: {row['error']}")
            return result
        time.sleep(0.2)
    raise RuntimeError(f"operation timeout: {capability} {operation_id}")


def status_data():
    status = db.get_chassis_control_status(VID)
    if not status:
        raise RuntimeError("missing chassis status")
    age = time.time() - float(status["received_at"])
    data = (status.get("payload") or {}).get("data") or {}
    if age > 2.5:
        raise RuntimeError(f"stale chassis status age={age:.3f}s")
    if data.get("faultLatched") or data.get("driveFault") or data.get("canBusOff"):
        raise RuntimeError("fault or CAN bus-off: " + json.dumps(data))
    if not all(data.get(key) is True for key in (
        "calibrated", "steeringProfileReady", "steeringRangeValid",
        "steeringOnline", "driveOnline", "encoderOnline", "feedbackFresh", "powerReady",
    )):
        raise RuntimeError("safety feedback lost: " + json.dumps(data))
    if data.get("otaBusy") is not False:
        raise RuntimeError("OTA became busy")
    if abs(int(data.get("leftActualRpm") or 0)) > 20 or abs(int(data.get("rightActualRpm") or 0)) > 20:
        raise RuntimeError("wheel movement detected: " + json.dumps(data))
    if int(data.get("targetDriveRpm") or 0) != 0:
        raise RuntimeError("non-zero drive target detected: " + json.dumps(data))
    if abs(float(data.get("steeringActualDeg") or 0) - float(data.get("absoluteActualDeg") or 0)) > 2.0:
        raise RuntimeError("steering feedback mismatch: " + json.dumps(data))
    return data


session_id = None
token = None
seq = 0
centered = False


def send_target(target_mdeg, duration, label):
    global seq
    deadline = time.monotonic() + duration
    last_report = None
    samples = []
    settled_reports = 0
    while time.monotonic() < deadline:
        seq += 1
        session = db.update_chassis_control_target(session_id, token, seq, 0, target_mdeg)
        if not session or session.get("state") != "active":
            raise RuntimeError("control session ended: " + json.dumps(session))
        data = status_data()
        report_id = data.get("reportId")
        if report_id != last_report:
            last_report = report_id
            samples.append({key: data.get(key) for key in (
                "reportId", "targetSteeringDeg", "steeringActualDeg", "absoluteActualDeg",
                "leftActualRpm", "rightActualRpm", "steeringBoundary", "safetyReady", "stopReason",
            )})
            target_deg = target_mdeg / 1000.0
            if (abs(float(data.get("steeringActualDeg") or 0) - target_deg) <= 0.4 and
                    abs(float(data.get("absoluteActualDeg") or 0) - target_deg) <= 0.5):
                settled_reports += 1
            else:
                settled_reports = 0
            if settled_reports >= 2:
                break
        time.sleep(0.2)
    data = status_data()
    print("STAGE", label, json.dumps({
        "requested_mdeg": target_mdeg,
        "final": samples[-1] if samples else data,
        "sample_count": len(samples),
        "min_actual": min((float(s["steeringActualDeg"]) for s in samples), default=None),
        "max_actual": max((float(s["steeringActualDeg"]) for s in samples), default=None),
    }), flush=True)
    return data


try:
    info = operation("get_info")
    if info.get("fwVersion") != "patrol-chassis-0.2.31" or info.get("otaBusy"):
        raise RuntimeError("unexpected firmware/OTA state: " + json.dumps(info))

    # Force a state-changed report so the platform's 2.5 s acquisition gate
    # evaluates current feedback rather than the 120 s idle report.
    operation("power_off")
    time.sleep(1.0)
    operation("power_on")
    time.sleep(5.0)
    bench = None
    for _ in range(5):
        bench = operation("get_bench_steering")
        if (bench.get("coordinateValid") and bench.get("rangeValid") and
                bench.get("profileReady") and bench.get("driveFault") == 0):
            break
        time.sleep(1.0)
    if not (bench.get("coordinateValid") and bench.get("rangeValid") and
            bench.get("profileReady") and bench.get("driveFault") == 0):
        raise RuntimeError("bench safety precondition failed: " + json.dumps(bench))
    permit = operation("arm_bench_drive", {"limitMdeg": 30000})
    if not permit.get("drivePermitted"):
        raise RuntimeError("bench permit rejected: " + json.dumps(permit))

    session_id = permit["sessionId"]
    token = str(uuid.uuid4())
    initial = status_data()
    hold_mdeg = round(float(initial.get("steeringActualDeg") or 0) * 1000)
    db.create_chassis_control_session(
        "codex-maintenance", token, VID,
        device_session_id=session_id, steering_mdeg=hold_mdeg,
    )

    # One uninterrupted lease avoids an unsafe disable between the edge and
    # the explicit return-to-center command.
    plan = (
        (0, 10.0, "acquire_center"),
        (29500, 25.0, "positive_edge_1"),
        (0, 50.0, "center_after_positive_1"),
        (-29500, 25.0, "negative_edge"),
        (0, 50.0, "center_after_negative"),
        (29500, 25.0, "positive_edge_2"),
        (0, 50.0, "final_center"),
    )
    for target, duration, label in plan:
        final = send_target(target, duration, label)
        if abs(target) == 29500:
            if abs(float(final["steeringActualDeg"])) < 27.0:
                raise RuntimeError(f"{label} did not enter the 50 rpm edge zone")
            if abs(float(final["absoluteActualDeg"])) > 30.5:
                raise RuntimeError(f"{label} exceeded the hard envelope")
        else:
            if abs(float(final["steeringActualDeg"])) > 0.4 or abs(float(final["absoluteActualDeg"])) > 0.4:
                raise RuntimeError(f"{label} did not reach center")
    centered = True
finally:
    if session_id and token:
        if not centered:
            try:
                send_target(0, 20.0, "emergency_center")
                centered = True
            except Exception as exc:
                print("EMERGENCY_CENTER_FAILED", repr(exc), flush=True)
        db.end_chassis_control_session(session_id, "explicit_stop", token)
        time.sleep(2.0)
    try:
        operation("power_off")
        operation("get_bench_steering")
        operation("get_can_diag")
        operation("get_info")
    except Exception as exc:
        print("FINAL_STATE_QUERY_FAILED", repr(exc), flush=True)

if not centered:
    raise RuntimeError("test did not finish centered")
print("RESULT PASS adaptive steering full-cycle validation", flush=True)

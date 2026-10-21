import json
import os
import threading
import time
import uuid

import paho.mqtt.client as mqtt

from device_ota.adapters import adapter_for
from device_ota.products import load_products

VID = "BENCH-SW-054363"
config = load_products()["patrol_vehicle_chassis"]
adapter = adapter_for("patrol_vehicle_chassis", config)
request_id, payload, request_topic, qos = adapter.operation(VID, "sync_state", {})
response_topic = config["topics"]["public_response"].replace("+", VID)
report_topic = config["topics"]["public_report"].replace("+", VID)
subscribed = threading.Event()
response = threading.Event()
report = threading.Event()
seen = []

try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1,
                         client_id=f"codex-bench-sync-{uuid.uuid4().hex[:8]}",
                         clean_session=True)
except AttributeError:
    client = mqtt.Client(client_id=f"codex-bench-sync-{uuid.uuid4().hex[:8]}",
                         clean_session=True)
username = os.environ.get(config.get("username_env", ""), "")
password = os.environ.get(config.get("password_env", ""), "")
assert username and password
client.username_pw_set(username, password)

def on_connect(client, user, flags, rc):
    assert rc == 0
    client.subscribe([(response_topic, 1), (report_topic, 1)])

def on_subscribe(client, user, mid, granted):
    subscribed.set()

def on_message(client, user, message):
    try:
        envelope = json.loads(message.payload)
    except (ValueError, UnicodeDecodeError):
        return
    data = envelope.get("data") or {}
    if data.get("requestId") != request_id:
        return
    if message.topic == response_topic:
        seen.append({"type": "response", "code": str(data.get("code")),
                     "expectedStates": (data.get("result") or {}).get("expectedStates")})
        response.set()
    elif message.topic == report_topic:
        seen.append({"type": "report", "reportName": data.get("reportName"),
                     "reportType": data.get("reportType")})
        report.set()

client.on_connect = on_connect
client.on_subscribe = on_subscribe
client.on_message = on_message
client.connect(config["broker_host"], int(config["broker_port"]), 30)
client.loop_start()
try:
    assert subscribed.wait(15), "subscriptions timed out"
    receipt = client.publish(request_topic, payload, qos=qos)
    receipt.wait_for_publish(timeout=10)
    assert receipt.is_published(), "command publish timed out"
    assert response.wait(30), "response timed out"
    assert report.wait(30), "correlated report timed out"
    assert seen[0]["type"] == "response", seen
    assert seen[0]["code"] == "200", seen
    assert seen[1]["type"] == "report", seen
    print(json.dumps({"request_id": request_id, "events": seen}))
finally:
    client.loop_stop()
    client.disconnect()

import json, os, threading, time
import paho.mqtt.client as mqtt
from device_ota.products import load_products
from device_ota.broker_admin import BrokerAdminClient

vid = 'BENCH-SW-054363'
config = load_products()['patrol_vehicle_chassis']
client_id = 'codex-patrol-bootstrap-observer'
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id=client_id, clean_session=True)
client.username_pw_set(os.environ[config['username_env']], os.environ[config['password_env']])
connected = threading.Event()
events = []
start = 0.0

def on_connect(client, user, flags, rc):
    if rc == 0:
        for kind in ('manifest', 'heartbeat', 'report'):
            client.subscribe(f'/zxwl/abox/{vid}/{kind}', qos=1)
        connected.set()

def on_message(client, user, message):
    if not start or time.time() < start:
        return
    kind = message.topic.rsplit('/', 1)[-1]
    try:
        data = json.loads(message.payload).get('data') or {}
    except (ValueError, UnicodeDecodeError):
        data = {}
    events.append({'t': round(time.time(), 3), 'kind': kind,
                   'firmware': data.get('firmware'),
                   'reportName': data.get('reportName'),
                   'reportType': data.get('reportType'),
                   'reportReason': data.get('reason'),
                   'retain': bool(message.retain)})

client.on_connect = on_connect
client.on_message = on_message
client.connect(config['broker_host'], int(config['broker_port']), 30)
client.loop_start()
assert connected.wait(8), 'observer connect failed'
time.sleep(1)
start = time.time()
BrokerAdminClient('abox')._request('DELETE', '/api/v5/clients/BENCH-SW-054363')
time.sleep(95)
client.loop_stop()
client.disconnect()
print(json.dumps({'kick_at': round(start, 3), 'events': events}, sort_keys=True))

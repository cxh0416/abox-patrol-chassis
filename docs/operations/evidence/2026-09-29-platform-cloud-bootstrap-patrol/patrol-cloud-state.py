import json
from device_ota import db
product = 'patrol_vehicle_chassis'
vid = 'BENCH-SW-054363'
manifest = db.get_device_manifest(product, vid) or {}
state = db.get_product_state(product, vid) or {}
device = db.get_device(product, vid) or {}
manifest_data = (manifest.get('payload') or {}).get('data') or {}
state_data = (state.get('payload') or {}).get('data') or {}
print(json.dumps({'manifest_firmware': manifest_data.get('firmware'),
                  'manifest_received_at': manifest.get('received_at'),
                  'state_received_at': state.get('received_at'),
                  'state_report_name': state_data.get('reportName'),
                  'state_report_type': state_data.get('reportType'),
                  'state_reason': state_data.get('reason'),
                  'device_online': device.get('online'),
                  'last_seen': device.get('last_seen')}, sort_keys=True))

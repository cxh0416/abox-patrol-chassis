import json
import sys
from device_ota import db
if sys.argv[1] == 'create':
    print(db.create_operation('patrol_vehicle_chassis','BENCH-SW-054363','get_log',{},'codex-standard-test-board'))
else:
    op=db.get_operation(sys.argv[1])
    assert op is not None
    print(json.dumps({'id':op['id'],'state':op['state'],'error':op.get('error'),'response_code':op.get('response_code'),'result':op.get('result'),'events':[e.get('message') for e in op.get('events',[])]},sort_keys=True))
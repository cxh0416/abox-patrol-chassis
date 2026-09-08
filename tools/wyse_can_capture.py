"""Bounded, listen-only SocketCAN capture through the onsite SSH gateway.

Requires paramiko and WYSE_SSH_PASSWORD. An existing 500 kbps interface can be
observed with --existing without changing its state. No CAN send operation exists.
"""
import argparse
import base64
import json
import os
from pathlib import Path

import paramiko


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--port', type=int, default=6008)
    parser.add_argument('--user', default='cxh')
    parser.add_argument('--seconds', type=int, default=180)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--existing', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 600:
        parser.error('seconds must be 1..600')
    password = os.environ['WYSE_SSH_PASSWORD']
    script = '''
import json, socket, struct, subprocess, time
state=json.loads(subprocess.check_output(['ip','-j','-details','link','show','can0']))[0]
existing=EXISTING
if existing:
    assert 'UP' in state['flags'], 'can0 is not up'
    assert state['linkinfo']['info_data']['bittiming']['bitrate']==500000, 'wrong bitrate'
else:
    assert 'UP' not in state['flags'], 'can0 is already in use; select --existing'
sock=None
try:
    if not existing:
        subprocess.run(['ip','link','set','can0','type','can','bitrate','500000',
                    'listen-only','on','restart-ms','0'],check=True)
        subprocess.run(['ip','link','set','can0','up'],check=True)
    sock=socket.socket(socket.PF_CAN,socket.SOCK_RAW,socket.CAN_RAW)
    sock.bind(('can0',));sock.settimeout(.2)
    print(json.dumps({'ready':True,'passiveSocket':True,'existingInterface':existing,'time':time.time()}),flush=True)
    deadline=time.monotonic()+DURATION
    while time.monotonic()<deadline:
        try: raw=sock.recv(16)
        except TimeoutError: continue
        ident,dlc,data=struct.unpack('=IB3x8s',raw)
        print(json.dumps({'time':time.time(),'id':ident,'dlc':dlc,'hex':data[:dlc].hex()}),flush=True)
finally:
    if sock: sock.close()
    if not existing:
        subprocess.run(['ip','link','set','can0','down'],check=True)
        subprocess.run(['ip','link','set','can0','type','can','listen-only','off'],check=True)
    print(json.dumps({'finished':True,'restoredDown':not existing,'time':time.time()}),flush=True)
'''.replace('DURATION', str(args.seconds)).replace('EXISTING', str(args.existing))
    encoded = base64.b64encode(script.encode()).decode()
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(args.host, port=args.port, username=args.user, password=password, timeout=15)
    try:
        command = "sudo -S -p '' python3 -u -c \"import base64;exec(base64.b64decode('" + encoded + "'))\""
        stdin, stdout, stderr = client.exec_command(command, timeout=args.seconds + 30)
        stdin.write(password + '\n'); stdin.flush()
        with args.output.open('w', encoding='utf-8') as output:
            for line in stdout:
                output.write(line)
                row = json.loads(line)
                if row.get('ready') or row.get('finished'):
                    output.flush()
                    print(line.strip(), flush=True)
        error = stderr.read().decode()
        status = stdout.channel.recv_exit_status()
        if status:
            raise RuntimeError(error.strip().splitlines()[-1])
        print(json.dumps({'output':str(args.output),'exit':status}), flush=True)
    finally:
        client.close()


if __name__ == '__main__':
    main()

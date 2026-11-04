"""Run against our fresh simulated --pc-only --stream app; never use real hardware."""
import http.client
import json
from pathlib import Path
import runpy
import socket
import threading
import time


def rpc(action):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(5)
        sock.connect('/tmp/camera-dataset.sock')
        sock.sendall((json.dumps(dict(action=action)) + '\n').encode())
        data = b''
        while b'\n' not in data:
            data += sock.recv(8192)
    return json.loads(data)


for _ in range(100):
    try:
        state = rpc('status')
        if state.get('camera_ready'):
            break
    except (OSError, ValueError):
        pass
    time.sleep(.1)
else:
    raise AssertionError('simulation never ready')
check = http.client.HTTPConnection('127.0.0.1', 47994, timeout=5)
check.request('GET', '/v1/status.json')
assert json.loads(check.getresponse().read())['source'] == 'simulation', 'Simulation only'
check.close()
stop = threading.Event()
frames = []
errors = []


def fetcher():
    while not stop.is_set():
        conn = http.client.HTTPConnection('127.0.0.1', 47994, timeout=5)
        try:
            conn.request('GET', '/v1/frame.jpg')
            response = conn.getresponse()
            data = response.read()
            if response.status == 200:
                assert response.getheader('X-Camera-Source') == 'simulation'
                assert data[:2] == b'\xff\xd8' and data[-2:] == b'\xff\xd9'
                assert int(response.getheader('Content-Length')) == len(data)
                frames.append(response.getheader('X-Frame-Sequence'))
            elif response.status != 503:
                errors.append(str(response.status))
        except OSError as exc:
            if not stop.is_set():
                errors.append(str(exc))
        finally:
            conn.close()
        stop.wait(.2)


worker = threading.Thread(target=fetcher)
worker.start()
try:
    # Covers countdown, invalid guide actions, no automatic photos, two frozen
    # JPEGs, SHA integrity, repeated chunks, wrong ACKs and idempotent correct ACKs.
    runpy.run_path(str(Path(__file__).with_name('guided_integration.py')), run_name='__main__')
finally:
    stop.set()
    worker.join(10)
assert not worker.is_alive() and not errors, errors
assert len(set(frames)) >= 4, frames
print('LIVE_PHOTO_CONCURRENT_PASS frames={} distinct={} capture_ACK_guidance=yes'.format(
    len(frames), len(set(frames))))

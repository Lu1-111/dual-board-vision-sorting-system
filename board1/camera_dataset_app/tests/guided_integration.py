"""Run against a fresh --simulate --pc-only GUI (no capture-first)."""
import json
import runpy
from pathlib import Path
import socket
import time


def rpc(action, **fields):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(5)
        sock.connect('/tmp/camera-dataset.sock')
        sock.sendall((json.dumps(dict(fields, action=action, rpc_id='guided')) + '\n').encode())
        data = b''
        while b'\n' not in data:
            chunk = sock.recv(8192)
            assert chunk, 'socket ended before reply'
            data += chunk
    return json.loads(data)


for _ in range(200):
    try:
        state = rpc('status')
        if state.get('camera_ready'): break
    except (OSError, ValueError):
        pass
    time.sleep(.1)
else:
    raise AssertionError('Simulation did not become ready')
assert state['guidance_protocol'] == 1 and not state['pending']
valid = dict(phase='prepare', text='可以摆放：红、黄、蓝、绿各一块，完整入镜',
             seconds=3, step=1, total=6)
assert rpc('guide', **valid)['ok']
assert not rpc('capture')['ok']
for override in ({'seconds': -1}, {'seconds': .5}, {'seconds': 61}, {'seconds': '2'},
                 {'step': 7}, {'text': '字' * 181}, {'phase': 'unknown'}):
    invalid = dict(valid, **override)
    assert not rpc('guide', **invalid)['ok'], override
time.sleep(3.5)
state = rpc('status')
assert state['guidance_seconds'] == 0 and state['guidance_phase'] == 'hold'
assert not state['pending'] and not state['busy'], 'Countdown must not take photos automatically'
assert rpc('guide', **dict(valid, phase='capture', seconds=0, text='请保持静止，正在拍照'))['ok']
assert rpc('capture')['ok']
# Existing integrity, resume and idempotent ACK tests capture a second photo,
# check no photos were written to the board, then close the application.
runpy.run_path(str(Path(__file__).with_name('pc_memory_integration.py')), run_name='__main__')
print('GUIDED_PASS countdown=yes invalid_commands=yes no_auto_capture=yes RAM_transfer=yes')

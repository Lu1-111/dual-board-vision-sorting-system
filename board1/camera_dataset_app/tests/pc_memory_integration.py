"""Exercise the running simulated GUI's RAM-only photo transfer endpoint."""
import base64
import hashlib
import json
import os
import socket
import time


def rpc(action, **fields):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(5)
        sock.connect('/tmp/camera-dataset.sock')
        sock.sendall((json.dumps(dict(fields, action=action, rpc_id='integration'))+'\n').encode())
        data = b''
        while b'\n' not in data:
            data += sock.recv(8192)
    reply = json.loads(data)
    assert reply['rpc_id'] == 'integration'
    return reply


def wait_photo():
    for _ in range(400):
        state = rpc('status')
        if state['pending']: return state['photo']
        time.sleep(.05)
    raise AssertionError('No photo arrived')


for capture in range(2):
    if capture: assert rpc('capture')['ok']
    photo = wait_photo()
    ident, digest = photo['photo_id'], photo['sha256']
    assert photo['source'] == 'simulation'
    assert (photo['width'], photo['height']) == (1920, 1080)
    assert not rpc('capture')['ok']
    assert not rpc('image_chunk', photo_id='wrong', offset=0)['ok']
    for offset in (-1, 0.5, photo['size'], '0'):
        assert not rpc('image_chunk', photo_id=ident, offset=offset)['ok']
    assert not rpc('ack', photo_id=ident, sha256='wrong')['ok']
    assert rpc('status')['pending']
    # Arbitrary/repeated chunks support resuming and retransmission without another capture.
    first = rpc('image_chunk', photo_id=ident, offset=0)
    assert first == rpc('image_chunk', photo_id=ident, offset=0)
    data = bytearray()
    while len(data) < photo['size']:
        r = rpc('image_chunk', photo_id=ident, offset=len(data))
        assert r['ok'] and r['offset'] == len(data) and r['sha256'] == digest
        data.extend(base64.b64decode(r['data'], validate=True))
    assert len(data) == photo['size'] and hashlib.sha256(data).hexdigest() == digest
    assert data[:2] == b'\xff\xd8' and data[-2:] == b'\xff\xd9'
    assert rpc('ack', photo_id=ident, sha256=digest)['ok']
    assert rpc('ack', photo_id=ident, sha256=digest)['already_confirmed']
    state = rpc('status')
    assert not state['pending'] and not state['busy']
    assert int(state['delivered']) == capture + 1
assert not os.path.exists('/tmp/camera-no-photo-files-20261030')
assert rpc('close')['ok']
print('PC_MEMORY_PASS photos=2 integrity=yes retry=yes ack=yes no_board_photo_files=yes')

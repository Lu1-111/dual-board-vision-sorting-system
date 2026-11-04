"""Native Linux executable integration: synthetic files and local fake arm socket only."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time

exe = sys.argv[1]
env = os.environ.copy()
with tempfile.TemporaryDirectory(prefix='sorting-cli-') as temporary:
    root = Path(temporary)
    sock = str(root / 'fake-arm.sock')
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(sock)
    server.listen(1)
    server.settimeout(2)
    requests = []
    def serve():
        peer, _ = server.accept()
        with peer:
            peer.settimeout(1)
            data = b''
            while b'\n' not in data:
                data += peer.recv(1024)
                assert len(data) <= 4096
            requests.append(json.loads(data.decode()))
            peer.sendall(b'{"ok":true,"connected":true,"mode":"hardware","enabled":false,"moving":false,"actionPlaying":false}\n')
    worker = threading.Thread(target=serve)
    worker.start()
    now = time.monotonic() * 1000
    config = dict(measured=False, calibration_confirmed=False, region_confirmed=False, region=None)
    track = dict(schema=1, status='ok', source='imx415', source_session='test-one', frame_sequence='1',
                 published_monotonic_ms=now, source_frame_age_ms=1, width=1920, height=1080,
                 purpose='localization_only', coordinate_frame='camera_pixel', localization_width=320,
                 sampling_step_px=[6,6], objects=[dict(track_id='7', partial=False, ambiguous=False,
                 center_xy=[1000,500], bbox_xyxy=[950,450,1050,550])])
    for name, value in [('config.json', config), ('track.json', track)]:
        (root/name).write_text(json.dumps(value))
    proc = subprocess.run([exe,'--observe-only','--config',str(root/'config.json'),
                           '--tracking',str(root/'track.json'),'--v28',str(root/'absent.json'),
                           '--arm-socket',sock], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           universal_newlines=True, timeout=4)
    worker.join(3)
    server.close()
    assert proc.returncode == 0, (proc.returncode, proc.stderr)
    assert requests == [{'action':'status'}], requests
    report = json.loads(proc.stdout)
    assert report['state'] == 'UNCONFIGURED_REFERENCE_ONLY', report
    assert report['motion_allowed'] is False
    assert report['arm_status']['enabled'] is False
    assert report['belt_status']['ok'] is False  # No device requested/opened.
    locked = subprocess.run([exe,'--single-cycle','--config',str(root/'does-not-exist')], env=env,
                            stdout=subprocess.PIPE,stderr=subprocess.PIPE,universal_newlines=True,timeout=2)
    assert locked.returncode == 3 and 'EXECUTION_LOCKED' in locked.stderr, locked.stderr
print('SORTING_READONLY_CLI_PASS: only status request; no belt device; PWMoff preserved; single-cycle rejected before IO')

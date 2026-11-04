#!/usr/bin/env python3
"""Loopback Qt integration: tracker overlay, exact frame binding, full JPEG identity.
Python 3.6+, standard library. No camera, motor, arm, NPU or board access.
"""
import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile
import time


def port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    value = s.getsockname()[1]
    s.close()
    return value


def line(proc, prefix, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if select.select([proc.stdout], [], [], max(0, deadline - time.monotonic()))[0]:
            value = proc.stdout.readline().strip()
            if value.startswith(prefix):
                return value
            if proc.poll() is not None:
                break
    raise AssertionError('fixture did not emit ' + prefix)


def command(proc, value):
    proc.stdin.write(value + '\n')
    proc.stdin.flush()
    line(proc, 'ACK ' + value)


def get(number, path):
    conn = http.client.HTTPConnection('127.0.0.1', number, timeout=3)
    try:
        conn.request('GET', path)
        response = conn.getresponse()
        return response.status, dict(response.getheaders()), response.read()
    finally:
        conn.close()


def ready_frame(number, path):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        status, headers, body = get(number, path)
        if status == 200:
            return headers, body
        time.sleep(.08)
    raise AssertionError('frame never ready: ' + path)


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--output', type=Path, default=Path('tracking_stream_results.json'))
    parser.add_argument('--reference-config', type=Path)
    args = parser.parse_args()
    processes, evidence = [], {}
    try:
        with tempfile.TemporaryDirectory(prefix='camera-centers-test-') as temporary:
            root = Path(temporary)
            reference_bytes = None
            if args.reference_config:
                reference_bytes = args.reference_config.read_bytes()
                (root / 'tracking1').mkdir()
                (root / 'tracking1' / 'reference.json').write_bytes(reference_bytes)
            ports = [port(), port()]
            for index in range(2):
                log = open(str(root / ('fixture{}.log'.format(index))), 'w')
                proc = subprocess.Popen([os.path.abspath(args.fixture), str(ports[index]),
                                         str(root / ('tracking{}'.format(index))), str(index)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log,
                                        universal_newlines=True, bufsize=1)
                processes.append((proc, log))
                line(proc, 'READY')
            baseline, tracked = processes[0][0], processes[1][0]
            _, full_baseline = ready_frame(ports[0], '/v1/frame.jpg')
            _, full_tracked = ready_frame(ports[1], '/v1/frame.jpg')
            assert full_baseline == full_tracked, 'tracking changed full inference JPEG'
            _, preview_baseline = ready_frame(ports[0], '/v1/preview.jpg')
            _, preview_tracked = ready_frame(ports[1], '/v1/preview.jpg')
            assert preview_baseline != preview_tracked, 'overlay was not encoded'
            command(tracked, 'pause')
            tracking_path = root / 'tracking1' / 'latest.json'
            bound = None
            until = time.monotonic() + 1
            while time.monotonic() < until:
                row = read_json(tracking_path)
                headers, _ = ready_frame(ports[1], '/v1/preview.jpg')
                if row['source_session'] == headers['X-Camera-Session'] and row['frame_sequence'] == headers['X-Frame-Sequence']:
                    bound = row
                    break
                time.sleep(.03)
            assert bound is not None, 'preview and JSON frame identifiers disagree'
            assert bound['width'] == 1920 and bound['height'] == 1080
            assert bound['status'] == 'ok' and bound['source'] == 'simulation'
            assert bound['objects'][0]['center_xy'] == [1020, 540]
            assert not bound['objects'][0]['partial'] and not bound['objects'][0]['ambiguous']
            assert all('score' not in item and 'label' not in item for item in bound['objects'])
            assert bound['tracking_ms'] >= 0 and 0 <= bound['source_frame_age_ms'] <= 500
            first_session = bound['source_session']
            first_id = bound['objects'][0]['track_id']
            if reference_bytes is not None:
                assert bound['reference_overlay']['center_xy'] == [1235, 543]
                assert bound['reference_overlay']['region_xyxy'] == [1135, 531, 1335, 555]
                assert bound['reference_overlay']['calibrated'] is False
                assert bound['reference_overlay']['motion_enabled'] is False
                command(tracked, 'move')
                command(tracked, 'resume')
                deadline = time.monotonic() + 3
                moved = None
                while time.monotonic() < deadline:
                    candidate = read_json(tracking_path)
                    if candidate.get('objects') and candidate['objects'][0]['center_xy'] == [1260, 540]:
                        moved = candidate
                        break
                    time.sleep(.03)
                assert moved is not None, 'synthetic object did not move'
                assert moved['reference_overlay']['center_xy'] == [1235, 543], 'reference followed moving object'
                assert (root / 'tracking1' / 'reference.json').read_bytes() == reference_bytes, 'camera rewrote reference config'
            command(tracked, 'stop')
            stopped = read_json(tracking_path)
            assert stopped['status'] == 'unavailable' and stopped['objects'] == []
            assert stopped['source_session'] == ''
            time.sleep(.2)
            assert read_json(tracking_path)['status'] == 'unavailable', 'in-flight encode revived stopped session'
            command(tracked, 'begin')
            ready_frame(ports[1], '/v1/preview.jpg')
            fresh = read_json(tracking_path)
            assert fresh['source_session'] != first_session
            assert fresh['objects'][0]['track_id'] != first_id
            command(tracked, 'quit')
            tracked.wait(timeout=5)
            assert read_json(tracking_path)['status'] == 'unavailable'
            evidence = dict(passed=True, center=[1020, 540],
                            full_jpeg_sha256=hashlib.sha256(full_tracked).hexdigest(),
                            full_jpeg_unchanged=True, preview_overlay_changed=True,
                            same_frame_binding=True, stopped_session_invalidated=True,
                            track_id_new_on_restart=True, tracking_ms=bound['tracking_ms'])
            if reference_bytes is not None:
                evidence['reference_fixed_1235_543'] = True
                evidence['reference_read_only'] = True
    finally:
        for proc, log in processes:
            if proc.poll() is None:
                try:
                    command(proc, 'quit')
                    proc.wait(timeout=3)
                except Exception:
                    proc.terminate()
                    proc.wait(timeout=3)
            log.close()
    args.output.write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print('TRACKING_STREAM_PASS ' + json.dumps(evidence), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

#!/usr/bin/env python3
"""Standard-library integration QA for the native Qt CameraPreviewClient smoke binary.

Run on Linux: python3 run_camera_preview_qa.py --binary ./camera_preview_smoke
The server binds loopback only, never camera hardware. Uses the small JPEG fixture
from ../../../fake_camera_server.py (override --fixture when copying standalone).
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import socketserver
import struct
import subprocess
import threading
import time


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(2)
        try:
            request = b''
            while b'\r\n\r\n' not in request and len(request) < 8192:
                chunk = self.request.recv(1024)
                if not chunk:
                    return
                request += chunk
            with self.server.counter_lock:
                self.server.count += 1
                n = self.server.count
            case = self.server.case
            body = self.server.jpeg
            session, seq, age = 'qa-a', 100 + n, 0
            if case == 'duplicate':
                seq = 100
            elif case == 'rollback':
                seq = [100, 99, 101][min(n - 1, 2)]
            elif case == 'retired-session':
                session, seq = [('qa-a', 100), ('qa-b', 1), ('qa-a', 101), ('qa-b', 2)][min(n - 1, 3)]
            elif case == 'disconnect-recover' and n == 1:
                return
            elif case == 'timeout-recover' and n == 1:
                time.sleep(2.5)
                return
            elif case == 'stale':
                age = 2001
            if case == 'truncated-jpeg':
                body = body[:-20]
            elif case == 'jpeg-dimension-bomb':
                data = bytearray(body)
                at = data.find(b'\xff\xc0')
                assert at >= 0
                data[at + 5:at + 9] = struct.pack('>HH', 65535, 65535)
                body = bytes(data)
            width, height = 32, 24
            if case == 'header-dimension-mismatch':
                width = 31
            if case == 'header-dimension-bomb':
                width, height = 4096, 4096
            protocol = '2' if case == 'wrong-protocol' else '1'
            content_length = 8 * 1024 * 1024 + 1 if case == 'oversize-length' else len(body)
            headers = [
                'HTTP/1.1 200 OK', 'Connection: close', 'Content-Type: image/jpeg',
                'Content-Length: {}'.format(content_length),
                'X-Camera-Protocol: {}'.format(protocol),
                'X-Camera-Session: {}'.format(session),
                'X-Frame-Sequence: {}'.format(seq),
                'X-Frame-Age-Ms: {}'.format(age),
                'X-Frame-Width: {}'.format(width), 'X-Frame-Height: {}'.format(height),
                'X-Captured-Utc: 2026-11-03T00:00:00Z', 'X-Camera-Source: imx415',
            ]
            if case == 'missing-protocol':
                headers = [h for h in headers if not h.startswith('X-Camera-Protocol:')]
            self.request.sendall(('\r\n'.join(headers) + '\r\n\r\n').encode('ascii'))
            self.request.sendall(body)
        except (BrokenPipeError, ConnectionResetError, TimeoutError, OSError):
            pass


def execute(binary, jpeg, case, count, timeout_ms, wanted, wanted_status=None):
    server = Server(('127.0.0.1', 0), Handler)
    server.jpeg, server.case, server.count = jpeg, case, 0
    server.counter_lock = threading.Lock()
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    started = time.monotonic()
    try:
        result = subprocess.run([
            binary, '--url', 'http://127.0.0.1:{}/v1/frame.jpg'.format(server.server_address[1]),
            '--fps', '10', '--frames', str(count), '--timeout-ms', str(timeout_ms),
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True,
            encoding='utf-8', errors='replace', timeout=timeout_ms / 1000 + 5)
    finally:
        server.shutdown()
        server.server_close()
    elapsed = time.monotonic() - started
    frames, states = [], []
    for line in result.stdout.splitlines():
        if line.startswith('FRAME '):
            frames.append(json.loads(line[6:]))
        if line.startswith('STATE '):
            states.append(json.loads(line[6:])['status'])
    actual = [(f['source_session'], int(f['frame_sequence'])) for f in frames]
    errors = []
    if actual != wanted:
        errors.append('frames {} != {}'.format(actual, wanted))
    expected_exit = 0 if len(wanted) >= count else 1
    if result.returncode != expected_exit:
        errors.append('exit {} != {}'.format(result.returncode, expected_exit))
    if wanted_status and wanted_status not in states:
        errors.append('missing state {}'.format(wanted_status))
    if case == 'timeout-recover' and elapsed < 1.9:
        errors.append('timeout fixture was not exercised')
    for frame in frames:
        if frame['width'] != 32 or frame['height'] != 24:
            errors.append('wrong original dimensions')
        if frame['display_width'] != 32 or frame['display_height'] != 24:
            errors.append('small JPEG was rescaled instead of retaining its original size')
        if not (0 <= frame['source_frame_age_ms'] <= 2000):
            errors.append('stale frame published')
    return dict(case=case, passed=not errors, errors=errors, frames=actual,
                states=states, requests=server.count, elapsed_s=round(elapsed, 3),
                returncode=result.returncode, stdout=result.stdout, stderr=result.stderr)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True)
    parser.add_argument('--fixture', type=Path,
                        default=Path(__file__).resolve().parents[3] / 'fake_camera_server.py')
    parser.add_argument('--output', type=Path, default=Path('camera_preview_qa_results.json'))
    parser.add_argument('--case', action='append', help='Run only the named case(s).')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('camera_fixture', str(args.fixture))
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    cases = [
        ('normal', 5, 5000, [('qa-a', n) for n in range(101, 106)], 'ok'),
        ('duplicate', 2, 3400, [('qa-a', 100)], 'source_stale'),
        ('rollback', 2, 5000, [('qa-a', 100), ('qa-a', 101)], 'source_stale'),
        ('retired-session', 3, 5000, [('qa-a', 100), ('qa-b', 1), ('qa-b', 2)], 'source_stale'),
        ('disconnect-recover', 1, 5000, [('qa-a', 102)], 'ok'),
        ('timeout-recover', 1, 5000, [('qa-a', 102)], 'ok'),
    ]
    for name in ['stale', 'oversize-length', 'wrong-protocol', 'missing-protocol',
                 'header-dimension-mismatch', 'header-dimension-bomb',
                 'jpeg-dimension-bomb', 'truncated-jpeg']:
        cases.append((name, 1, 2600, [], 'source_stale'))
    selected = set(args.case or [])
    if selected - {c[0] for c in cases}:
        parser.error('unknown case(s): ' + ', '.join(sorted(selected - {c[0] for c in cases})))
    results = []
    binary = os.path.abspath(args.binary)
    for case in cases:
        if selected and case[0] not in selected:
            continue
        try:
            row = execute(binary, fixture.JPEG, *case)
        except Exception as error:
            row = dict(case=case[0], passed=False, errors=[repr(error)])
        results.append(row)
        print('{} {}'.format('PASS' if row['passed'] else 'FAIL', case[0]), flush=True)
    args.output.write_text(json.dumps({'passed': all(r['passed'] for r in results),
                                      'results': results}, ensure_ascii=False, indent=2), encoding='utf-8')
    return 0 if all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())

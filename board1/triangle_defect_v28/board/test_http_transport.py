#!/usr/bin/env python3
"""Standard-library loopback QA; no real camera/model/NPU.

Build: g++ -std=c++17 -O2 http_transport_probe.cpp -o http_transport_probe
Run: python3 test_http_transport.py --binary ./http_transport_probe
Fixtures test SOF/transport framing only, intentionally not JPEG entropy decoding.
Actual image decode and model equivalence require the separate board regression.
"""
import argparse
import json
import os
from pathlib import Path
import socketserver
import struct
import subprocess
import tempfile
import threading
import time


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            self.request.settimeout(2)
            req = b''
            while b'\r\n\r\n' not in req and len(req) < 8192:
                part = self.request.recv(1024)
                if not part:
                    return
                req += part
            case = req.split(b' ')[1].decode().strip('/')
            width, height = (1920, 1080) if case != 'sof-bomb' else (65535, 65535)
            if case == 'sof-mismatch':
                width = 1919
            body = b'\xff\xd8\xff\xc0\x00\x11\x08' + struct.pack('>HH', height, width) + b'\x03\x01\x11\x00\x02\x11\x00\x03\x11\x00\xff\xd9'
            if case == 'missing-eoi':
                body = body[:-2]
            length = 8 * 1024 * 1024 + 1 if case == 'oversize' else len(body)
            if case == 'truncated':
                length += 100
            headers = {'Connection': 'close', 'Content-Type': 'image/jpeg',
                       'Content-Length': str(length), 'X-Camera-Protocol': '1',
                       'X-Camera-Session': 'qa-a', 'X-Frame-Sequence': '100',
                       'X-Frame-Age-Ms': '0', 'X-Camera-Source': 'imx415',
                       'X-Captured-Utc': '2026-11-03T00:00:00Z',
                       'X-Frame-Width': '1920', 'X-Frame-Height': '1080'}
            if case == 'preview-size':
                headers['X-Frame-Width'] = '640'
                headers['X-Frame-Height'] = '360'
            if case == 'protocol':
                headers['X-Camera-Protocol'] = '2'
            if case == 'source':
                headers['X-Camera-Source'] = 'simulation'
            if case == 'bad-session':
                headers['X-Camera-Session'] = '../../escape'
            if case == 'bad-sequence':
                headers['X-Frame-Sequence'] = '-1'
            if case == 'stale':
                headers['X-Frame-Age-Ms'] = '5001'
            if case == 'no-length':
                del headers['Content-Length']
            if case == 'chunked':
                headers['Transfer-Encoding'] = 'chunked'
            if case == 'header-bomb':
                headers['X-Padding'] = 'a' * 70000
            if case == 'slow-header':
                time.sleep(2.5)
            status = '500 Failure' if case == 'http500' else '200 OK'
            wire = ('HTTP/1.1 ' + status + '\r\n' + ''.join(k + ': ' + v + '\r\n' for k, v in headers.items())).encode()
            if case == 'duplicate-length':
                wire += b'Content-Length: 24\r\n'
            self.request.sendall(wire + b'\r\n')
            if case == 'slow-body':
                time.sleep(2.5)
            self.request.sendall(body)
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True)
    parser.add_argument('--output', type=Path, default=Path('http_transport_results.json'))
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    server = Server(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    results = []
    try:
        for case in ['normal', 'preview-size', 'protocol', 'source', 'bad-session', 'bad-sequence',
                     'stale', 'oversize', 'truncated', 'missing-eoi', 'sof-bomb', 'sof-mismatch', 'no-length',
                     'chunked', 'header-bomb', 'http500', 'duplicate-length', 'slow-header', 'slow-body']:
            started = time.monotonic()
            run = subprocess.run([binary, 'http://127.0.0.1:{}/{}'.format(server.server_address[1], case)],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True, timeout=4)
            elapsed = time.monotonic() - started
            ok = (run.returncode == 0) == (case == 'normal')
            if case.startswith('slow-'):
                ok = ok and 1.8 <= elapsed < 2.8
            results.append(dict(case=case, passed=ok, elapsed=elapsed,
                                stdout=run.stdout, stderr=run.stderr, returncode=run.returncode))
            print(('PASS ' if ok else 'FAIL ') + case, flush=True)
        process = subprocess.Popen([binary, 'http://127.0.0.1:{}/slow-body'.format(server.server_address[1])],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        time.sleep(0.2)
        started = time.monotonic()
        process.terminate()
        stdout, stderr = process.communicate(timeout=1)
        elapsed = time.monotonic() - started
        ok = process.returncode == 1 and elapsed < 0.5 and 'Stopping' in stderr
        results.append(dict(case='sigterm_cancels_http', passed=ok, elapsed=elapsed,
                            stdout=stdout, stderr=stderr, returncode=process.returncode))
        print(('PASS ' if ok else 'FAIL ') + 'sigterm_cancels_http', flush=True)
        with tempfile.TemporaryDirectory(prefix='triangle-publisher-') as directory:
            sentinel = Path(directory) / 'user-owned.txt'
            sentinel.write_text('must remain untouched')
            run = subprocess.run([binary, '--self-test', directory], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, universal_newlines=True, timeout=5)
            rows = {line.split(' ', 1)[0]: json.loads(line.split(' ', 1)[1])
                    for line in run.stdout.splitlines() if line.startswith(('RESULT ', 'INVALID '))}
            good = rows.get('RESULT', {})
            bad = rows.get('INVALID', {})
            ok = (run.returncode == 0 and good.get('frame_sequence') == '3'
                  and good.get('objects', [{}])[0].get('label') == 'NORMAL'
                  and bad.get('objects') == [] and bad.get('frame_path') == ''
                  and sentinel.read_text() == 'must remain untouched')
            results.append(dict(case='gate_atomic_binding_lock_cleanup', passed=ok,
                                stdout=run.stdout, stderr=run.stderr, returncode=run.returncode))
            print(('PASS ' if ok else 'FAIL ') + 'gate_atomic_binding_lock_cleanup', flush=True)
    finally:
        server.shutdown()
        server.server_close()
    passed = all(row['passed'] for row in results)
    args.output.write_text(json.dumps(dict(passed=passed, results=results), indent=2), encoding='utf-8')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())

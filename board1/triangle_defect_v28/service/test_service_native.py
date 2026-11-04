"""POSIX native fixture tests. Never starts a model, opens board devices or calls GPIO.
Usage: python3 test_service_native.py (requires sh, cc, flock, sha256sum and /proc).
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent


def run(args, **kwargs):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          universal_newlines=True, timeout=45, **kwargs)


def main():
    if os.name != 'posix':
        raise SystemExit('Native POSIX fixture only; no target-board tests are run here')
    checks = []
    with tempfile.TemporaryDirectory(prefix='triangle-service-test-') as folder:
        app = Path(folder)
        out = app / 'out'; out.mkdir()
        models = app / 'models'; models.mkdir()
        replacements = {}
        for name, expected in [
            ('triangle_v28_features_fp32.onnx','108d66f9faeea396a22adba1d039c0b4bbeb4c547d48018589532b951f0ccbf7'),
            ('triangle_v28_head_uint8.onnx','dcbe3b45e1eb3c0bb797aa93eb147ecd5c6f119f833abb588bb1f6c8cf0d2168')]:
            data = ('fixture-only-' + name).encode()
            (models/name).write_bytes(data)
            replacements[expected] = hashlib.sha256(data).hexdigest()
        for name in ['run_triangle.sh','triangle_service.sh','bounded_log.sh']:
            source = (ROOT/name).read_text()
            source = source.replace('/tmp/triangle-live.lock', str(app/'service.lock'))
            source = source.replace('/tmp/triangle-live', str(out))
            source = source.replace('/home/uisrc/triangle_defect_v28/models', str(models))
            source = source.replace('/home/uisrc/triangle_defect_v28', str(app))
            # Exercise the same check against a harmless host character device.
            source = source.replace('for device in cma_mem hard_npu soft_npu;', 'for device in null;')
            for a,b in replacements.items(): source = source.replace(a,b)
            path = app/name; path.write_text(source); path.chmod(0o755)
            syntax = run(['sh','-n',str(path)])
            assert syntax.returncode == 0, syntax.stdout
        checks.append('shell_syntax')
        stub = app/'worker.c'
        stub.write_text('#include <signal.h>\n#include <unistd.h>\n'
                        'static volatile sig_atomic_t stop=0;\n'
                        'static void term(int s){(void)s;stop=1;}\n'
                        'int main(void){signal(SIGTERM,term);while(!stop)usleep(20000);usleep(150000);return 0;}\n')
        compile_result = run(['cc',str(stub),'-o',str(app/'triangle_defect_http')])
        assert compile_result.returncode == 0, compile_result.stdout
        (app/'triangle_defect_http.sha256').write_text(hashlib.sha256((app/'triangle_defect_http').read_bytes()).hexdigest()+'  triangle_defect_http\n')
        service = [str(app/'triangle_service.sh')]
        try:
            first = run(service+['start']); assert first.returncode == 0, first.stdout
            worker_before = (out/'worker.pid').read_text()
            second = run(service+['start']); assert second.returncode == 0, second.stdout
            assert worker_before == (out/'worker.pid').read_text()
            checks.append('duplicate_start_same_worker')
            direct = run([str(app/'run_triangle.sh')]); assert direct.returncode == 3, direct.stdout
            checks.append('direct_duplicate_flock_rejected')
            stopped = run(service+['stop']); assert stopped.returncode == 0, stopped.stdout
            checks.append('graceful_stop_releases_lock')
            # An unrelated process must not receive a signal via forged PID metadata.
            unrelated = subprocess.Popen(['sleep','20'])
            try:
                fields = Path('/proc/{}/stat'.format(unrelated.pid)).read_text().rsplit(')',1)[1].split()
                (out/'worker.pid').write_text('{} {}\n'.format(unrelated.pid,fields[19]))
                stopped = run(service+['stop']); assert stopped.returncode == 0
                assert unrelated.poll() is None
            finally:
                unrelated.terminate(); unrelated.wait()
            checks.append('pid_executable_identity_rejects_unrelated_process')
            restarted = run(service+['start']); assert restarted.returncode == 0, restarted.stdout
            supervisor = int((out/'supervisor.pid').read_text().split()[0])
            os.kill(supervisor,9)  # synthetic sleep-loop fixture only, never a board process
            time.sleep(.2)
            locked = run(['flock','-n',str(app/'service.lock'),'true']); assert locked.returncode != 0
            again = run(service+['start']); assert 'Already running' in again.stdout, again.stdout
            stopped = run(service+['stop']); assert stopped.returncode == 0, stopped.stdout
            checks.append('orphan_worker_retains_lock_and_stops_by_verified_identity')
            (models/'triangle_v28_head_uint8.onnx').write_bytes(b'wrong hash')
            invalid = run(service+['start']); assert invalid.returncode != 0, invalid.stdout
            assert run(['flock','-n',str(app/'service.lock'),'true']).returncode == 0
            checks.append('model_hash_mismatch_refuses_start')
            # More than two files of logs, with no newline; memory is chunk-bounded.
            payload = bytes(range(256))*4096
            logged = subprocess.run([str(app/'bounded_log.sh'),str(out/'worker.log')], input=payload,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
            assert logged.returncode == 0, logged.stderr
            logs = [(out/'worker.log.1').read_bytes(),(out/'worker.log').read_bytes()]
            assert all(len(data) <= 262144 for data in logs)
            joined = b''.join(logs)
            assert joined == payload[-len(joined):]
            assert not list(out.glob('worker.log.chunk.*'))
            checks.append('byte_bounded_logs_long_line_rotation_and_order')
        finally:
            run(service+['stop'])
    print(json.dumps({'status':'PASS','checks':checks,'scope':'Native synthetic worker only; no NPU/camera/board access'},indent=2))


if __name__ == '__main__': main()

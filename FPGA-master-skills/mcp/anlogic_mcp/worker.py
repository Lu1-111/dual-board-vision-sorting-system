"""Detached build worker. No MCP output; survives a client reconnect."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

import psutil


def save(path: Path, data: dict) -> None:
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    # Windows readers briefly hold a handle without FILE_SHARE_DELETE.
    for attempt in range(100):
        try:
            os.replace(temporary, path)
            return
        except PermissionError:
            if attempt == 99:
                raise
            time.sleep(0.01)


def stop_tree(process: subprocess.Popen) -> None:
    try:
        parent = psutil.Process(process.pid)
        children = parent.children(recursive=True)
        parent.suspend()
        for child in reversed(children):
            try:
                child.kill()
            except psutil.NoSuchProcess:
                pass
        parent.kill()
        psutil.wait_procs(children + [parent], timeout=5)
    except psutil.NoSuchProcess:
        pass


def main(directory: Path) -> None:
    spec = json.loads((directory / 'request.json').read_text(encoding='utf-8'))
    state = {k: spec[k] for k in ('job_id', 'kind', 'command', 'cwd', 'timeout_seconds')}
    state.update(status='starting', worker_pid=os.getpid(),
                 worker_created=psutil.Process().create_time(), started_at=time.time())
    status = directory / 'status.json'
    save(status, state)
    process = None
    lock = None
    # Put the worker and all descendants in a Windows job. If the worker crashes,
    # closing its last job handle kills lingering compiler/TD children as well.
    job_handle = None
    try:
        if os.name == 'nt':
            import win32api
            import win32job
            job_handle = win32job.CreateJobObject(None, '')
            limits = win32job.QueryInformationJobObject(job_handle, win32job.JobObjectExtendedLimitInformation)
            limits['BasicLimitInformation']['LimitFlags'] |= win32job.JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            win32job.SetInformationJobObject(job_handle, win32job.JobObjectExtendedLimitInformation, limits)
            win32job.AssignProcessToJobObject(job_handle, win32api.GetCurrentProcess())
        lock_key = hashlib.sha256(spec['lock_key'].encode()).hexdigest()
        lock = (directory.parent / (lock_key + '.lock')).open('a+b')
        lock.seek(0)
        if os.name == 'nt':
            import msvcrt
            if lock.read(1) == b'':
                lock.write(b'0')
                lock.flush()
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with (directory / 'output.log').open('wb') as output:
            process = subprocess.Popen(spec['command'], cwd=spec['cwd'], env={**os.environ, **spec['env']},
                stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            state.update(status='running', pid=process.pid)
            save(status, state)
            deadline = time.monotonic() + spec['timeout_seconds']
            while process.poll() is None:
                reason = ('cancelled' if (directory / 'cancel').exists() else
                          'timed_out' if time.monotonic() >= deadline else None)
                if reason:
                    stop_tree(process)
                    state['status'] = reason
                    break
                time.sleep(0.2)
            state['returncode'] = process.wait(timeout=10)
        if state['status'] == 'running':
            marker = spec.get('success_marker')
            tail = b''
            with (directory / 'output.log').open('rb') as stream:
                stream.seek(max(0, stream.seek(0, 2) - 65536))
                tail = stream.read()
            passed = state['returncode'] == 0 and (not marker or marker.encode() in tail)
            state['status'] = 'succeeded' if passed else 'failed'
            if marker and marker.encode() not in tail:
                state['error'] = 'TD did not reach the success marker; exit code alone is insufficient.'
    except Exception as error:
        state.update(status='failed', error=f'{type(error).__name__}: {error}')
        if process is not None and process.poll() is None:
            stop_tree(process)
    finally:
        state['finished_at'] = time.time()
        save(status, state)
        if lock:
            lock.close()
        # Leave the job handle alive until process exit; closing it explicitly
        # also terminates this worker before its status has been flushed.
        if job_handle is not None:
            os._exit(0)


if __name__ == '__main__':
    main(Path(sys.argv[1]))

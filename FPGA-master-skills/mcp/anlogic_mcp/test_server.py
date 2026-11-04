"""Unit and real subprocess lifecycle tests; no installed EDA tools required."""
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

import psutil
import server


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='anlogic-test-')
        self.root = Path(self.temp.name).resolve()
        self.previous = server.ROOT, server.STATE
        server.ROOT, server.STATE = self.root, self.root / 'jobs'

    def tearDown(self):
        # Workers are detached. Wait for their final file handles before cleanup.
        for request in server.STATE.glob('*/*/request.json'):
            status = server.status_of(request.parent)
            if status['status'] in ('starting', 'running'):
                (request.parent / 'cancel').touch()
            for _ in range(100):
                if server.status_of(request.parent)['status'] not in ('starting', 'running'):
                    break
                time.sleep(0.05)
        time.sleep(0.15)
        server.ROOT, server.STATE = self.previous
        self.temp.cleanup()

    def wait(self, job):
        directory = server.job_dir(job['job_id'], 'td')
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            state = server.status_of(directory)
            if state['status'] not in ('starting', 'running'):
                return state
            time.sleep(0.05)
        self.fail('worker did not finish')

    def launch(self, code, **kwargs):
        return server.start_job('td', [sys.executable, '-c', code], self.root, kwargs.pop('timeout', 10), **kwargs)

    def test_path_traversal(self):
        with self.assertRaises(ValueError):
            server.path_in_root(str(self.root.parent), True)
        with self.assertRaises(ValueError):
            server.job_dir('../x', 'td')

    def test_td_nonstandard_metadata(self):
        p = self.root / 'design.al'
        p.write_text('<Project><Name>demo</Name><Device>DR1M90GEG400</Device>'
                     '<Attr Name="UsedInP&R"/><BitgenProperty::GeneralOption/>'
                     '<File Path="missing.v"/><Run Name="syn_1" Type="Synthesis"></Run></Project>')
        result = server.td_info(p)
        self.assertEqual(result['device'], 'DR1M90GEG400')
        self.assertEqual(result['missing_sources'], ['missing.v'])
        self.assertEqual(result['runs'][0]['Name'], 'syn_1')

    def test_success_and_no_secret_environment_on_disk(self):
        os.environ['ANLOGIC_TEST_SECRET'] = 'not-for-request-json'
        try:
            job = self.launch('import os; print("pass", bool(os.environ.get("ANLOGIC_TEST_SECRET")))')
            self.assertEqual(self.wait(job)['status'], 'succeeded')
            d = server.job_dir(job['job_id'], 'td')
            self.assertNotIn('not-for-request-json', (d / 'request.json').read_text())
            self.assertIn('True', (d / 'output.log').read_text())
        finally:
            os.environ.pop('ANLOGIC_TEST_SECRET')

    def test_missing_success_marker_and_exit_code(self):
        self.assertEqual(self.wait(self.launch('print("fake success")', marker='expected'))['status'], 'failed')
        self.assertEqual(self.wait(self.launch('raise SystemExit(7)'))['returncode'], 7)

    def test_timeout(self):
        result = self.wait(self.launch('import time; time.sleep(30)', timeout=1))
        self.assertEqual(result['status'], 'timed_out', result)
        self.assertFalse(psutil.pid_exists(result['pid']))

    def test_cancel_tree_and_lock(self):
        child_pid = self.root / 'child.pid'
        code = ('import subprocess,sys,time,pathlib; '
                'p=subprocess.Popen([sys.executable,"-c","import time; time.sleep(60)"]); '
                f'pathlib.Path({str(child_pid)!r}).write_text(str(p.pid)); time.sleep(60)')
        job = self.launch(code, timeout=60)
        for _ in range(100):
            if child_pid.exists():
                break
            time.sleep(0.05)
        if not child_pid.exists():
            self.fail(str(self.wait(job)))
        blocked = self.launch('print("should not run")')
        self.assertEqual(self.wait(blocked)['status'], 'failed')
        (server.job_dir(job['job_id'], 'td') / 'cancel').touch()
        self.assertEqual(self.wait(job)['status'], 'cancelled')
        self.assertFalse(psutil.pid_exists(int(child_pid.read_text())))

    def test_pagination_and_binary(self):
        p = self.root / 'test.log'
        p.write_bytes(b'0123456789')
        self.assertEqual(server.read_chunk(p, -1, 4)['text'], '6789')
        self.assertEqual(server.read_chunk(p, 4, 3)['next_offset'], 7)
        p.write_bytes(b'\x00ELF')
        with self.assertRaises(ValueError):
            server.read_chunk(p, 0, 10)


if __name__ == '__main__':
    unittest.main(verbosity=2)

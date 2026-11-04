"""Synchronous SMD budgets are independent of ordinary setup/hold summaries."""
from pathlib import Path
import tempfile
import tkinter
import unittest


class ExceptionReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tcl = tkinter.Tcl()
        self.tcl.call('source', str(Path(__file__).resolve().parents[1] / 'timing_release_check.tcl'))
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.report = Path(self.folder.name) / 'exceptions.timing'
        self.valid = '''Confidence : Routed
MCP/SMD Summary
Path Group : sync_interstage
Type : SMD
Max : SWNS 0.123ns, STNS 0.000ns, 0 Viol Endpoints, 2 Total Endpoints, 2 Paths Analyzed
Min : HWNS 0.000ns, HTNS 0.000ns, 0 Viol Endpoints, 0 Total Endpoints, 0 Paths Analyzed
Slack : 0.123ns
'''

    def check(self, text):
        self.report.write_text(text, encoding='utf-8')
        return self.tcl.call('check_timing_exception_report', str(self.report))

    def test_native_group_format_including_empty_minimum(self):
        self.assertEqual(str(self.check(self.valid)), '1')

    def test_negative_group_without_path_details_is_rejected(self):
        with self.assertRaisesRegex(tkinter.TclError, 'budget violated'):
            self.check(self.valid.replace('0.123ns, STNS', '-0.001ns, STNS').replace('0 Viol', '1 Viol', 1))

    def test_nonzero_violations_with_rounded_zero_slack_are_rejected(self):
        with self.assertRaisesRegex(tkinter.TclError, 'budget violated'):
            self.check(self.valid.replace('0.123ns, STNS', '0.000ns, STNS').replace('0 Viol', '1 Viol', 1))

    def test_negative_per_path_is_rejected(self):
        with self.assertRaisesRegex(tkinter.TclError, 'per-path budget violated'):
            self.check(self.valid.replace('Slack : 0.123ns', 'Slack : -0.001ns'))

    def test_incomplete_or_truncated_analysis_is_rejected(self):
        for text in (self.valid.split('Path Group')[0],
                     self.valid.replace('2 Paths Analyzed', '1 Paths Analyzed'),
                     self.valid.replace('Min : HWNS', 'Min : missing')):
            with self.subTest(text=text), self.assertRaisesRegex(tkinter.TclError, 'Incomplete'):
                self.check(text)

    def test_missing_or_synthesized_report_is_rejected(self):
        with self.assertRaisesRegex(tkinter.TclError, 'Missing timing exception'):
            self.tcl.call('check_timing_exception_report', str(self.report))
        with self.assertRaisesRegex(tkinter.TclError, 'not routed'):
            self.check(self.valid.replace('Routed', 'Synthesized'))


if __name__ == '__main__':
    unittest.main()

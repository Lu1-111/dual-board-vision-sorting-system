"""Regression cases for the same Tcl checker used by TD build/release scripts."""
from pathlib import Path
import tempfile
import tkinter
import unittest


class TimingReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tcl = tkinter.Tcl()
        self.tcl.call("source", str(Path(__file__).resolve().parents[1] / "timing_release_check.tcl"))
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.report = Path(self.folder.name) / "report.timing"
        self.valid = """STA coverage : 100.00%
Confidence : Routed
SWNS: 0.123ns, STNS: 0.000ns
HWNS: 0.045ns, HTNS: 0.000ns
clk0: 75.002 213 ps.GEN_PL_GP0S
clk1: 150.015 210 ps.GEN_PL_GP1S
clk2: 150.015 229 ps.GEN_PL_HP0
clk3: 150.015 234 ps.GEN_PL_HP1
"""

    def check(self, text, closed=1, coverage=1):
        self.report.write_text(text, encoding="utf-8")
        return self.tcl.call("check_release_timing", str(self.report), closed, coverage)

    def test_valid_routed_report(self):
        self.assertTrue(self.check(self.valid))

    def test_wrong_frequency(self):
        with self.assertRaisesRegex(tkinter.TclError, "wrong frequency"):
            self.check(self.valid.replace("75.002", "50.000"))

    def test_zero_fanout_virtual_clock(self):
        with self.assertRaisesRegex(tkinter.TclError, "disconnected"):
            self.check(self.valid.replace("75.002 213", "75.002 0"))

    def test_incomplete_coverage(self):
        with self.assertRaisesRegex(tkinter.TclError, "coverage"):
            self.check(self.valid.replace("100.00%", "83.21%"))
        self.assertTrue(self.check(self.valid.replace("100.00%", "83.21%"), closed=0, coverage=0))

    def test_negative_slack(self):
        for text in (self.valid.replace("0.123ns", "-1.316ns"),
                     self.valid.replace("0.045ns", "-2.921ns")):
            with self.assertRaisesRegex(tkinter.TclError, "not closed"):
                self.check(text)
            self.assertTrue(self.check(text, closed=0))

    def test_coverage_does_not_hide_simultaneous_timing_failure(self):
        report = self.valid.replace("100.00%", "99.94%").replace("0.123ns", "-5.200ns").replace("0.045ns", "-0.148ns")
        with self.assertRaises(tkinter.TclError) as failure:
            self.check(report)
        self.assertIn("coverage is 99.94%", str(failure.exception))
        self.assertIn("setup=-5.200 ns, hold=-0.148 ns", str(failure.exception))

    def test_unrouted_report(self):
        with self.assertRaisesRegex(tkinter.TclError, "routed"):
            self.check(self.valid.replace("Routed", "Estimated"))

    def test_missing_status(self):
        with self.assertRaisesRegex(tkinter.TclError, "Missing hold"):
            self.check(self.valid.replace("HWNS:", "missing:"))

    def test_native_model_and_constraint_errors_block_release(self):
        fields = ["no clock", "clock definition", "invalid constraint", "combinational loops",
                  "shadowed/ignored timing exception constraints"]
        good = "\n".join("checking " + name + "(0)" for name in fields)
        self.report.write_text(good)
        self.assertEqual(str(self.tcl.call("check_release_diagnostics", str(self.report))), "1")
        for name in fields:
            with self.subTest(name=name):
                self.report.write_text(good.replace("checking " + name + "(0)", "checking " + name + "(1)"))
                with self.assertRaisesRegex(tkinter.TclError, "Unresolved native diagnostics"):
                    self.tcl.call("check_release_diagnostics", str(self.report))

    def test_incomplete_diagnostics_cannot_pass(self):
        self.report.write_text("checking no clock(0)")
        with self.assertRaisesRegex(tkinter.TclError, "Incomplete native diagnostics"):
            self.tcl.call("check_release_diagnostics", str(self.report))

    def test_failed_native_diagnostics_cannot_reuse_stale_file(self):
        self.report.write_text("old report")
        self.tcl.eval("proc check_timing {args} {}")
        with self.assertRaisesRegex(tkinter.TclError, "Missing native timing diagnostics"):
            self.tcl.call("report_release_diagnostics", str(self.report))
        self.assertFalse(self.report.exists())


if __name__ == "__main__":
    unittest.main()

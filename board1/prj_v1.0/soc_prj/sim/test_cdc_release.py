"""A missing, incomplete or failing Gray-bus analysis must block release."""
from pathlib import Path
import tempfile
import tkinter
import unittest


class CdcReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tcl = tkinter.Tcl()
        self.tcl.call("source", str(Path(__file__).resolve().parents[1] / "timing_release_check.tcl"))
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        for owner, direction, width in (("control", "rd_to_wr", 12), ("control", "wr_to_rd", 12),
                                         ("video", "rd_to_wr", 5), ("video", "wr_to_rd", 5)):
            first = 1 if owner == "control" and direction == "rd_to_wr" else 0
            text = """Confidence : Routed
Type : SMD
set_max_delay -datapath_only -from x -to y 6.000
Max : SWNS 1.000ns, 0 Viol Endpoints, {width} Total Endpoints, {width} Paths Analyzed
""".format(width=width)
            for bit in range(first, first + width):
                text += (f"End Point : fifo/{direction}_cross_inst/sync_r1_reg[{bit}].d (clock)\n"
                         "Slack : 1.000ns\nData Path Delay : 5.000ns\n")
            (self.root / f"{owner}_cdc_{direction}.timing").write_text(text)

    def check(self):
        return self.tcl.call("check_control_cdc_timing", str(self.root))

    def replace(self, before, after):
        path = self.root / "control_cdc_rd_to_wr.timing"
        path.write_text(path.read_text().replace(before, after, 1))

    def test_all_used_bits_meet_datapath_budget(self):
        self.assertEqual(str(self.check()), "1")

    def retained_zero(self, slack='1.000'):
        path = self.root / 'control_cdc_rd_to_wr.timing'
        detail = path.read_text().replace('12 Total Endpoints, 12 Paths Analyzed',
                                          '13 Total Endpoints, 13 Paths Analyzed')
        detail += ('End Point : fifo/rd_to_wr_cross_inst/sync_r1_reg[0].d (clock)\n'
                   f'Slack : {slack}ns\nData Path Delay : 5.000ns\n')
        path.write_text(detail)

    def test_routed_unused_zero_may_be_retained_with_all_live_bits(self):
        self.retained_zero()
        self.assertEqual(str(self.tcl.call('check_control_cdc_timing', str(self.root), 1)), '1')

    def test_retained_zero_requires_its_own_budget_check(self):
        self.retained_zero('-0.001')
        with self.assertRaisesRegex(tkinter.TclError, 'per-bit budget'):
            self.check()

    def test_retained_zero_cannot_replace_missing_live_bit(self):
        self.retained_zero()
        self.replace('sync_r1_reg[12]', 'sync_r1_reg[0]')
        with self.assertRaisesRegex(tkinter.TclError, 'Missing or duplicate'):
            self.check()

    def test_packed_retained_zero_uses_exact_logical_endpoint_proof(self):
        self.retained_zero()
        self.packed_register_proof()
        self.assertEqual(str(self.tcl.call('check_control_cdc_timing', str(self.root), 1)), '1')

    def test_missing_report_rejected(self):
        (self.root / "control_cdc_rd_to_wr.timing").unlink()
        with self.assertRaisesRegex(tkinter.TclError, "Missing CDC analysis"):
            self.check()

    def test_old_doubled_fifo_geometry_rejected(self):
        path = self.root / "control_cdc_rd_to_wr.timing"
        detail = path.read_text().replace("12 Total Endpoints, 12 Paths Analyzed",
                                          "13 Total Endpoints, 13 Paths Analyzed")
        detail += ("End Point : fifo/rd_to_wr_cross_inst/sync_r1_reg[13].d (clock)\n"
                   "Slack : 1.000ns\nData Path Delay : 5.000ns\n")
        path.write_text(detail)
        with self.assertRaises(tkinter.TclError):
            self.check()

    def test_empty_table_rejected(self):
        (self.root / "control_cdc_rd_to_wr.timing").write_text("Path Skew Report Summary\n")
        with self.assertRaisesRegex(tkinter.TclError, "Incomplete CDC"):
            self.check()

    def test_wrong_endpoint_or_missing_bit_rejected(self):
        self.replace("sync_r1_reg[1]", "sync_r1_reg[2]")
        with self.assertRaisesRegex(tkinter.TclError, "Missing or duplicate"):
            self.check()

    def test_negative_per_bit_slack_rejected(self):
        self.replace("Slack : 1.000ns", "Slack : -0.001ns")
        with self.assertRaisesRegex(tkinter.TclError, "per-bit budget"):
            self.check()

    def test_out_of_budget_delay_rejected(self):
        self.replace("Delay : 5.000ns", "Delay : 6.001ns")
        with self.assertRaisesRegex(tkinter.TclError, "Invalid CDC"):
            self.check()

    def test_synthesis_report_cannot_qualify_routed_release(self):
        self.replace("Confidence : Routed", "Confidence : Synthesized")
        with self.assertRaisesRegex(tkinter.TclError, "not routed"):
            self.tcl.call("check_control_cdc_timing", str(self.root), 1)

    def test_routed_physical_endpoint_names(self):
        for path in self.root.glob("*.timing"):
            path.write_text(path.read_text().replace("].d (clock)", "]_syn_2.Q_oqb_D (clock)"))
        self.assertEqual(str(self.tcl.call("check_control_cdc_timing", str(self.root), 1)), "1")

    def test_native_query_failure_cannot_reuse_gate_reports(self):
        self.tcl.eval("proc report_timing_exception {args} {}")
        with self.assertRaisesRegex(tkinter.TclError, "Fresh CDC report not generated"):
            self.tcl.call("report_control_cdc_timing", str(self.root))
        self.assertFalse((self.root / "control_cdc_rd_to_wr.timing").exists())

    def packed_register_proof(self):
        for path in self.root.glob("*.timing"):
            text = path.read_text()
            lines = []
            for line in text.splitlines():
                if line.startswith("End Point"):
                    logical = line.split(" : ", 1)[1].split(".d ", 1)[0].replace("sync_r1_reg", "sync_r1")
                    lines += ["End Point : packed_cell_named_for_another_stage.imb (clock)",
                              "Logic Level : 0", "reg x01y02 0.100 net: " + logical + ", source.v(10)"]
                else:
                    lines.append(line)
            path.write_text("\n".join(lines) + "\n")

    def test_packed_cell_name_uses_native_logical_register_proof(self):
        self.packed_register_proof()
        self.assertEqual(str(self.tcl.call("check_control_cdc_timing", str(self.root), 1)), "1")

    def test_packed_second_stage_cannot_impersonate_first_stage(self):
        self.packed_register_proof()
        self.replace("cross_inst/sync_r1[1]", "cross_inst/primary_addr_gray_sync[1]")
        with self.assertRaisesRegex(tkinter.TclError, "Unexpected CDC logical endpoint"):
            self.check()

    def test_gray_crossing_must_have_no_combinational_logic(self):
        self.packed_register_proof()
        self.replace("Logic Level : 0", "Logic Level : 1")
        with self.assertRaisesRegex(tkinter.TclError, "Combinational logic"):
            self.check()


if __name__ == "__main__":
    unittest.main()

"""Check that reviewed CDC constraints fail closed and stay at first stages."""
from pathlib import Path
import re
import tkinter
import unittest


class ControlCdcTests(unittest.TestCase):
    def run_constraints(self, missing=""):
        tcl = tkinter.Tcl()
        tcl.tk.wantobjects(False)
        tcl.setvar("missing", missing)
        tcl.eval('''
            set constraints {}
            proc get_regs {args} {
                set pattern [lindex $args end]
                if {[string match $::missing $pattern]} {return {}}
                if {[string match *Video_Out_0_Inst* $pattern]} {
                    if {[string match *asy_* $pattern]} {return [lrepeat 4 $pattern]}
                    return [lrepeat 5 $pattern]
                }
                if {[string match *rd_to_wr_cross_inst/* $pattern]} {return [lrepeat 13 $pattern]}
                if {[string match *wr_to_rd_cross_inst/* $pattern]} {return [lrepeat 12 $pattern]}
                if {[string match *hs_reset_release* $pattern]} {return [lrepeat 2 $pattern]}
                if {[string match *reset_release* $pattern]} {return [lrepeat 6 $pattern]}
                if {[string match *servo_reset_pipe* $pattern]} {return [lrepeat 2 $pattern]}
                if {[string match *gpio_* $pattern]} {return [lrepeat 57 $pattern]}
                if {[string match *commit_sync* $pattern]} {return [lrepeat 3 $pattern]}
                if {[string match *asy_* $pattern]} {return [lrepeat 4 $pattern]}
                if {[string match *vs_i_r1$ $pattern]} {return [list $pattern]}
                if {[string match *vs_i_r* $pattern]} {return [lrepeat 4 $pattern]}
                return [list $pattern]
            }
            proc get_pins {args} {
                set pattern [lindex $args end]
                if {[string match $::missing $pattern]} {return {}}
                return [list $pattern]
            }
            foreach name {set_false_path set_max_delay set_path_skew} {
                proc $name {args} [format {lappend ::constraints [linsert $args 0 %s]} $name]
            }
        ''')
        folder = Path(__file__).resolve().parents[1] / "src/constraint"
        tcl.call("source", str(folder / "control_cdc_check.tcl"))
        tcl.call("source", str(folder / "control_cdc.sdc"))
        return [tuple(str(x) for x in tcl.splitlist(item))
                for item in tcl.splitlist(tcl.getvar("constraints"))]

    def test_missing_reset_chain_rejects_build(self):
        with self.assertRaisesRegex(tkinter.TclError, "CDC register count mismatch"):
            self.run_constraints("*reset_release*")

    def test_missing_gray_first_stage_rejects_build(self):
        with self.assertRaisesRegex(tkinter.TclError, "CDC register count mismatch"):
            self.run_constraints("*rd_to_wr_cross_inst/sync_r1*")

    def test_missing_overflow_chain_rejects_build(self):
        for stage in ("latched", "meta", "sync"):
            with self.subTest(stage=stage):
                with self.assertRaisesRegex(tkinter.TclError, "CDC register count mismatch"):
                    self.run_constraints("*overflow_" + stage + "$*")

    def test_no_whole_domain_or_later_stage_cuts(self):
        commands = self.run_constraints()
        cuts = [x for x in commands if x[0] == "set_false_path"]
        self.assertEqual(len(cuts), 7)
        for cut in cuts:
            if "overflow_latched" in str(cut):
                self.assertEqual(cut[1], "-from")
                self.assertIn("overflow_latched$", cut[2])
                self.assertEqual(cut[3], "-to")
                self.assertIn("overflow_meta$", cut[4])
                self.assertNotIn("overflow_sync", str(cut))
                continue
            self.assertEqual(cut[1], "-to")
            self.assertTrue("[.](a)?sr$" in cut[2] or "vs_i_r1$" in cut[2])
            self.assertNotIn("primary_addr_gray_sync", str(cut))

    def test_both_gray_directions_have_delay_and_skew_bounds(self):
        commands = self.run_constraints()
        for kind in ("set_max_delay", "set_path_skew"):
            bounds = [x for x in commands if x[0] == kind and "primary_addr_gray_reg" in str(x)]
            self.assertEqual(len(bounds), 4)
            for bound in bounds:
                self.assertIn("primary_addr_gray_reg", str(bound))
                self.assertIn("sync_r1", str(bound))
                self.assertEqual(float(bound[-1]), 6.0)
                if kind == "set_max_delay":
                    self.assertIn("-datapath_only", bound)

    def test_interstage_constraints_keep_hold_checks(self):
        bounds = [x for x in self.run_constraints()
                  if x[0] == "set_max_delay" and "primary_addr_gray_reg" not in str(x)]
        self.assertEqual(len(bounds), 13)
        for bound in bounds:
            self.assertNotIn("-datapath_only", bound)
            self.assertEqual(float(bound[-1]), 1.0)

    def test_first_stage_regex_excludes_input_aliases_and_second_stage(self):
        sdc = (Path(__file__).resolve().parents[1] / "src/constraint/control_cdc.sdc").read_text()
        patterns = re.findall(r"-to \[get_regs -regexp \{([^}]+sync_r1[^}]+)\}\]", sdc)
        self.assertEqual(len(patterns), 8)
        for pattern in patterns:
            base = ("design_0_AXI4_Stream_to_Video_Out_0_Inst/FIFO_INST" if "Video_Out" in pattern
                    else "uidbuf_write_u0/u_wfifo")
            direction = "rd_to_wr" if "rd_to_wr" in pattern else "wr_to_rd"
            name = base + "/" + direction + "_cross_inst/sync_r1[3]"
            self.assertIsNotNone(re.fullmatch(pattern, name))
            self.assertIsNone(re.fullmatch(pattern, name + "_input"))
            self.assertIsNone(re.fullmatch(pattern, name.replace("sync_r1", "primary_addr_gray_sync")))


if __name__ == "__main__":
    unittest.main()

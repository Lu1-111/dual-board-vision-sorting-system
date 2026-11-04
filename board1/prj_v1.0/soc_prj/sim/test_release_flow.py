"""Exercise the real routed-release script without invoking TD or bitgen."""
from pathlib import Path
import shutil
import tempfile
import tkinter
import unittest


class ReleaseFlowTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        project = Path(__file__).resolve().parents[1]
        for name in ("finalize_conveyor_timing.tcl", "timing_release_check.tcl", "verify_arm_servo_timing.tcl"):
            shutil.copyfile(project / name, self.root / name)
        self.output = self.root / "soc_prj_Runs/control_pipeline_20261025"
        self.output.mkdir(parents=True)
        self.tcl = tkinter.Tcl()
        self.tcl.eval('''
            unset -nocomplain ::env(CONVEYOR_ROUTED_DB)
            set calls {}
            proc cd {args} {}
            proc exit {code} {error "TEST_EXIT_$code"}
            proc unknown {name args} {lappend ::calls $name}
            set mock_reset_regs {}
            foreach instance {0 0_0 0_1} {
                foreach bit {0 1} {
                    lappend mock_reset_regs [format {design_0_inst/design_0_pwm_%s_Inst/pwm/reset_release[%d]} $instance $bit]
                }
            }
            foreach bit {0 1} {lappend mock_reset_regs [format {servo_reset_pipe[%d]} $bit]}
            foreach bit {0 1} {lappend mock_reset_regs [format {design_top_wrapper_u/u_mipi_dphy_rx_over_lvds_wrapper/hs_reset_release[%d]} $bit]}
            foreach owner {uidbuf_write_u0/u_wfifo design_0_AXI4_Stream_to_Video_Out_0_Inst/FIFO_INST} {
                foreach domain {r w} {
                    foreach bit {0 1} {lappend mock_reset_regs [format {%s/asy_%s_rst%d} $owner $domain $bit]}
                }
            }
            proc mock_reset_cell {net} {
                if {[regexp {^(.*)[[]([0-9]+)[]]$} $net _ base bit]} {
                    return [format {%s_reg[%d]} $base $bit]
                }
                return ${net}_reg
            }
            proc get_regs {args} {
                set found {}
                foreach net $::mock_reset_regs {if {[regexp [lindex $args end] $net]} {lappend found $net}}
                return $found
            }
            proc get_nets {args} {
                set found {}
                foreach net $::mock_reset_regs {if {[regexp [lindex $args end] $net]} {lappend found $net}}
                return $found
            }
            proc get_cells {args} {
                set found {}
                foreach net $::mock_reset_regs {
                    set owner [mock_reset_cell $net]
                    if {[regexp [lindex $args end] $owner]} {lappend found $owner}
                }
                return [lsort -unique $found]
            }
            proc get_pins {args} {
                set regexp_at [lsearch -exact $args -regexp]
                if {$regexp_at >= 0} {
                    set found {}
                    foreach net $::mock_reset_regs {
                        set pin [mock_reset_cell $net].sr
                        if {[regexp [lindex $args end] $pin]} {lappend found $pin}
                    }
                    return $found
                }
                set net [lindex [lindex $args [expr {[lsearch -exact $args -of]+1}]] 0]
                return [list [mock_reset_cell $net].q unrelated_consumer.sr]
            }
            proc report_timing_status {args} {
                set f [open [lindex $args end] w]
                puts $f $::mock_report
                close $f
            }
            proc report_timing_summary {args} {report_timing_status {*}$args}
            proc check_timing {args} {
                set f [open [lindex $args end] w]
                foreach name {{no clock} {clock definition} {invalid constraint} {combinational loops} {shadowed/ignored timing exception constraints}} {
                    set count [expr {$name eq "invalid constraint" && [info exists ::bad_diagnostics] ? 1 : 0}]
                    puts $f "checking ${name}($count)"
                }
                close $f
            }
            proc report_timing_exception {args} {
                set path [lindex $args end]
                set f [open $path w]
                puts $f "Confidence : Routed\nMCP/SMD Summary"
                foreach owner {control video} {
                    foreach direction {rd_to_wr wr_to_rd} {
                        set width [expr {$owner eq "video" ? 5 : 12}]
                        set first [expr {$owner eq "control" && $direction eq "rd_to_wr" ? 1 : 0}]
                        set base [expr {$owner eq "video" ? "design_0_AXI4_Stream_to_Video_Out_0_Inst/FIFO_INST" : "uidbuf_write_u0/u_wfifo"}]
                        puts $f {Path Group :}
                        puts $f [format {set_max_delay -datapath_only -from [get_regs {%s/%s_cross_inst/primary_addr_gray_reg*}] -to [get_pins {%s/%s_cross_inst/sync_r1_reg*.d}] 6.000} $base $direction $base $direction]
                        puts $f {Type : SMD}
                        puts $f "Max : SWNS 1.000ns, 0 Viol Endpoints, $width Total Endpoints, $width Paths Analyzed"
                        for {set bit $first} {$bit < $first+$width} {incr bit} {
                            puts $f [format {End Point : %s/%s_cross_inst/sync_r1_reg[%d].d (clock)} $base $direction $bit]
                            puts $f {Slack : 1.000ns}
                            puts $f {Data Path Delay : 5.000ns}
                        }
                    }
                }
                if {[info exists ::bad_exception_slack]} {
                    puts $f {Path Group : unrelated_sync_interstage}
                    puts $f {Type : SMD}
                    puts $f {Max : SWNS -0.001ns, 1 Viol Endpoints, 1 Total Endpoints, 1 Paths Analyzed}
                    puts $f {Slack : -0.001ns}
                }
                close $f
            }
            proc fix_hold {args} {
                lappend ::calls fix_hold
                set ::mock_report $::repaired_report
            }
        ''')
        self.tcl.setvar('env(CONVEYOR_ROUTED_DB)', str(self.output / 'video_dma_routed.db'))
        self.valid = '''STA coverage : 100.00%
Confidence : Routed
SWNS: 0.123ns, STNS: 0.000ns
HWNS: 0.045ns, HTNS: 0.000ns
clk0: 75.002 213 ps.GEN_PL_GP0S
clk1: 150.015 210 ps.GEN_PL_GP1S
clk2: 150.015 229 ps.GEN_PL_HP0
clk3: 150.015 234 ps.GEN_PL_HP1
'''

    def run_flow(self, report, expected_exit, repaired=None, candidate=True):
        if candidate:
            (self.output / "video_dma_routed.db").write_text("test stub")
        self.tcl.setvar("mock_report", report)
        self.tcl.setvar("repaired_report", repaired or report)
        with self.assertRaisesRegex(tkinter.TclError, "TEST_EXIT_" + str(expected_exit)):
            self.tcl.call("source", str(self.root / "finalize_conveyor_timing.tcl"))
        return tuple(str(name) for name in self.tcl.splitlist(self.tcl.getvar("calls")))

    def test_missing_candidate_never_imports_or_generates(self):
        calls = self.run_flow(self.valid, 1, candidate=False)
        self.assertNotIn("import_db", calls)
        self.assertNotIn("bitgen", calls)

    def test_existing_historical_database_is_never_selected_implicitly(self):
        self.tcl.eval('unset env(CONVEYOR_ROUTED_DB)')
        calls = self.run_flow(self.valid, 1)
        self.assertNotIn('import_db', calls)
        self.assertNotIn('bitgen', calls)

    def test_blank_candidate_cannot_select_working_directory(self):
        self.tcl.setvar('env(CONVEYOR_ROUTED_DB)', '  ')
        calls = self.run_flow(self.valid, 1)
        self.assertNotIn('import_db', calls)

    def test_legacy_audit_requires_explicit_candidate_too(self):
        (self.output / 'video_dma_routed.db').write_text('historical stub')
        self.tcl.eval('unset env(CONVEYOR_ROUTED_DB)')
        with self.assertRaisesRegex(tkinter.TclError, 'TEST_EXIT_1'):
            self.tcl.call('source', str(self.root / 'verify_arm_servo_timing.tcl'))
        calls = tuple(str(name) for name in self.tcl.splitlist(self.tcl.getvar('calls')))
        self.assertNotIn('import_db', calls)

    def test_wrong_clock_model_stops_before_eco_and_bitgen(self):
        calls = self.run_flow(self.valid.replace("75.002", "50.000"), 1)
        self.assertNotIn("fix_hold", calls)
        self.assertNotIn("bitgen", calls)

    def test_invalid_native_constraint_blocks_otherwise_passing_report(self):
        self.tcl.setvar("bad_diagnostics", 1)
        calls = self.run_flow(self.valid, 1)
        self.assertNotIn("fix_hold", calls)
        self.assertNotIn("bitgen", calls)

    def test_physical_constraints_rebound_before_audit_and_after_eco(self):
        calls = self.run_flow(self.valid.replace("0.045ns", "-0.045ns"), 0,
                              repaired=self.valid)
        clears = [i for i, name in enumerate(calls) if name == "clear_sdc"]
        self.assertEqual(len(clears), 2)
        self.assertLess(clears[0], calls.index("fix_hold"))
        self.assertLess(calls.index("fix_hold"), clears[1])
        self.assertLess(clears[1], calls.index("bitgen"))
        self.assertEqual(calls.count("read_sdc"), 4)

    def test_failed_eco_cannot_generate_bitstream(self):
        negative = self.valid.replace("0.045ns", "-0.045ns")
        calls = self.run_flow(negative, 1)
        self.assertIn("fix_hold", calls)
        self.assertIn("export_db", calls)
        self.assertNotIn("bitgen", calls)

    def test_extra_budget_failure_blocks_passing_summary_and_gray_paths(self):
        self.tcl.setvar("bad_exception_slack", 1)
        calls = self.run_flow(self.valid, 1)
        self.assertIn("export_db", calls)
        self.assertNotIn("bitgen", calls)

    def test_failed_native_report_cannot_reuse_old_passing_summary(self):
        old_report = self.output / "conveyor_before.timing"
        old_report.write_text(self.valid)
        self.tcl.eval('proc report_timing_summary {args} {}')
        calls = self.run_flow(self.valid, 1)
        self.assertFalse(old_report.exists())
        self.assertNotIn("fix_hold", calls)
        self.assertNotIn("bitgen", calls)

    def test_legacy_audit_cannot_reuse_old_passing_summary(self):
        old_report = self.output / "arm_servo_audit.timing"
        old_report.write_text(self.valid)
        (self.output / "video_dma_routed.db").write_text("test stub")
        self.tcl.eval('proc report_timing_summary {args} {}')
        with self.assertRaisesRegex(tkinter.TclError, "TEST_EXIT_1"):
            self.tcl.call("source", str(self.root / "verify_arm_servo_timing.tcl"))
        self.assertFalse(old_report.exists())

    def test_repaired_routed_candidate_may_generate(self):
        calls = self.run_flow(self.valid.replace("0.045ns", "-0.045ns"), 0,
                              repaired=self.valid)
        self.assertLess(calls.index("fix_hold"), calls.index("bitgen"))

    def test_legacy_verifier_cannot_pass_negative_timing(self):
        (self.output / "video_dma_routed.db").write_text("test stub")
        self.tcl.setvar("mock_report", self.valid.replace("0.123ns", "-0.123ns"))
        with self.assertRaisesRegex(tkinter.TclError, "TEST_EXIT_1"):
            self.tcl.call("source", str(self.root / "verify_arm_servo_timing.tcl"))
        calls = tuple(str(name) for name in self.tcl.splitlist(self.tcl.getvar("calls")))
        self.assertNotIn("bitgen", calls)

    def test_legacy_build_entries_use_guarded_build(self):
        project = Path(__file__).resolve().parents[1]
        (self.root / "build_conveyor_system.tcl").write_text("set ::guarded_build_used 1\n")
        for name in ("build_arm_servo.tcl", "continue_conveyor_physical.tcl"):
            shutil.copyfile(project / name, self.root / name)
            self.tcl.setvar("guarded_build_used", 0)
            self.tcl.call("source", str(self.root / name))
            self.assertEqual(str(self.tcl.getvar("guarded_build_used")), "1")


if __name__ == "__main__":
    unittest.main()

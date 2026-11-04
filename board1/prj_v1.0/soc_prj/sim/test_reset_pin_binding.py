"""Packed cell names must not determine async-reset exception coverage."""
from pathlib import Path
import tkinter
import unittest


class ResetPinBindingTests(unittest.TestCase):
    def setUp(self):
        self.tcl = tkinter.Tcl()
        self.tcl.call('source', str(Path(__file__).resolve().parents[1] / 'timing_release_check.tcl'))
        self.tcl.eval('''
            set reset_regs {r0 r1}
            set driver_pins {r0 {r0_reg.q consumer_a.asr consumer_b.ssr}
                             r1 {{other.ip/packed[3]$lane.oqb} consumer_c.asr}}
            set all_asr {r0_reg.sr {other.ip/packed[3]$lane.asr} {otherXip/packedX3YZlane.asr}
                         r0_reg.ssr {other.ip/packed[3]$lane.ssr}}
            set bound {}
            set native_queries 0
            set empty_of 0
            proc get_regs {args} {return $::reset_regs}
            proc get_nets {args} {
                set found {}
                foreach net $::reset_regs {if {[regexp [lindex $args end] $net]} {lappend found $net}}
                return $found
            }
            proc get_cells {args} {
                set found {}
                dict for {net pins} $::driver_pins {
                    foreach pin $pins {
                        if {![regexp {[.](q|oq[ab])$} $pin]} {continue}
                        set owner [string range $pin 0 [expr {[string last . $pin]-1}]]
                        if {[regexp [lindex $args end] $owner]} {lappend found $owner}
                    }
                }
                return [lsort -unique $found]
            }
            proc get_pins {args} {
                if {[lsearch -exact $args -regexp] >= 0} {
                    set found {}
                    foreach pin $::all_asr {if {[regexp [lindex $args end] $pin]} {lappend found $pin}}
                    if {[lsearch -exact $args -verbose] < 0} {incr ::native_queries}
                    return $found
                }
                set net [lindex [lindex $args [expr {[lsearch -exact $args -of]+1}]] 0]
                if {$net eq ""} {set ::empty_of 1;error "empty object would query everything"}
                return [dict get $::driver_pins $net]
            }
            proc set_false_path {args} {set ::bound [lindex $args end]}
            proc read_sdc {path} {uplevel #0 [list source $path]}
        ''')

    def resolve(self):
        return self.tcl.call('resolve_reset_sync_asr_pins', ('r.*',))

    def test_logical_driver_finds_unrelated_packed_cell_and_excludes_sinks(self):
        result = self.resolve()
        self.assertEqual(self.tcl.call('dict', 'get', result, 'r1'), 'other.ip/packed[3]$lane.asr')
        self.assertEqual(self.tcl.call('dict', 'get', result, 'r0'), 'r0_reg.sr')

    def test_only_missing_owner_reset_is_added(self):
        self.tcl.eval('set all_asr {r0_reg.sr {other.ip/packed[3]$lane.asr} r0_reg.ssr}')
        self.tcl.call('bind_reset_sync_asr_pins', ('r.*',), ('^r0_reg[.]sr$',), 2)
        self.assertEqual(tuple(self.tcl.splitlist(self.tcl.getvar('bound'))), ('other.ip/packed[3]$lane.asr',))
        self.assertEqual(str(self.tcl.getvar('native_queries')), '1')

    def test_ambiguous_native_selector_never_binds_names_as_collection(self):
        with self.assertRaisesRegex(tkinter.TclError, 'Ambiguous physical reset exception'):
            self.tcl.call('bind_reset_sync_asr_pins', ('r.*',), ('^r0_reg[.]sr$',), 2)
        self.assertEqual(str(self.tcl.getvar('native_queries')), '0')
        self.assertEqual(str(self.tcl.getvar('bound')), '')

    def test_missing_register_cannot_fall_back_to_all_pins(self):
        self.tcl.setvar('reset_regs', ())
        with self.assertRaisesRegex(tkinter.TclError, 'Missing logical reset'):
            self.resolve()
        self.assertEqual(str(self.tcl.getvar('empty_of')), '0')

    def test_missing_native_net_is_rejected_before_fanout_query(self):
        self.tcl.eval('proc get_nets {args} {return {}}')
        with self.assertRaisesRegex(tkinter.TclError, 'Ambiguous reset binding selector: get_nets'):
            self.resolve()
        self.assertEqual(str(self.tcl.getvar('empty_of')), '0')

    def test_missing_native_owner_is_rejected_before_reset_pin_query(self):
        self.tcl.eval('proc get_cells {args} {return {}}')
        with self.assertRaisesRegex(tkinter.TclError, 'Ambiguous reset binding selector: get_cells'):
            self.resolve()

    def test_missing_driver_is_rejected_before_owner_query(self):
        self.tcl.eval('dict set driver_pins r1 {consumer.asr}')
        with self.assertRaisesRegex(tkinter.TclError, 'one Q driver'):
            self.resolve()
        self.assertEqual(str(self.tcl.getvar('empty_of')), '0')

    def test_two_driver_candidates_are_rejected(self):
        self.tcl.eval('dict set driver_pins r1 {a.oqb b.q}')
        with self.assertRaisesRegex(tkinter.TclError, 'one Q driver'):
            self.resolve()

    def test_synchronous_reset_cannot_replace_async_reset(self):
        self.tcl.eval('set all_asr {r0_reg.sr {other.ip/packed[3]$lane.ssr}}')
        with self.assertRaisesRegex(tkinter.TclError, 'one async-reset pin'):
            self.resolve()

    def test_similar_cell_name_cannot_impersonate_exact_owner(self):
        self.tcl.eval('set all_asr {r0_reg.sr {otherXip/packedX3YZlane.asr}}')
        with self.assertRaisesRegex(tkinter.TclError, 'one async-reset pin'):
            self.resolve()

    def test_incomplete_whitelist_never_binds(self):
        with self.assertRaisesRegex(tkinter.TclError, 'Incomplete logical reset'):
            self.tcl.call('bind_reset_sync_asr_pins', ('r.*',), (), 3)
        self.assertEqual(str(self.tcl.getvar('bound')), '')


if __name__ == '__main__':
    unittest.main()

"""Post-gate repair must bind real FFs and prove every attribute took effect."""
from pathlib import Path
import tempfile
import tkinter
import unittest


class AsyncRegisterBindingTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.output = str(Path(self.folder.name) / 'bindings.tcl')
        self.tcl = tkinter.Tcl()
        self.tcl.call('source', str(Path(__file__).resolve().parents[1] /
                                    'src/constraint/control_async_regs.tcl'))
        self.tcl.eval('''
            set regs {n0 n1}
            set pinmap {n0 {ff0.q sink0.d} n1 {ff1.q sink1.d}}
            set cells {ff0 ff1}
            set nets {n0 n1}
            set allpins {}
            set upstream {}
            set models {}
            set placed_cells {}
            set properties {}
            set enabled 0
            set ignored 0
            set writes 0
            set netqueries 0
            proc get_regs {args} {return $::regs}
            proc matched {items pattern} {
                set result {}
                foreach item $items {if {[regexp $pattern $item]} {lappend result $item}}
                return $result
            }
            proc get_cells {args} {return [matched $::cells [lindex $args end]]}
            proc get_nets {args} {
                incr ::netqueries
                set i [lsearch -exact $args -of]
                if {$i >= 0} {return [dict get $::upstream [lindex $args [expr {$i+1}]]]}
                return [matched $::nets [lindex $args end]]
            }
            proc get_pins {args} {
                set i [lsearch -exact $args -of]
                if {$i >= 0} {return [dict get $::pinmap [lindex [lindex $args [expr {$i+1}]] 0]]}
                return [matched $::allpins [lindex $args end]]
            }
            proc get_property {key object} {
                if {$key eq "REF_NAME"} {return [dict get $::models $object]}
                if {$key eq "IS_PLACED"} {return [dict get $::placed_cells $object]}
                if {[dict exists $::properties $object]} {return [dict get $::properties $object]}
                return {}
            }
            proc set_param {args} {set ::enabled 1}
            proc set_property {key value objects} {
                if {!$::enabled} {error "directive switch is off"}
                if {$::ignored} {return}
                foreach object $objects {dict set ::properties $object 1;incr ::writes}
            }
        ''')

    def apply(self, count=2):
        return self.tcl.call('apply_control_async_registers', self.output, ('^n[01]$', count))

    def test_marks_and_verifies_both_net_and_actual_q_driver(self):
        result = self.apply()
        self.assertEqual(int(self.tcl.call('dict', 'size', result)), 2)
        self.assertEqual(int(self.tcl.getvar('writes')), 4)

    def test_mapped_keep_buffer_is_followed_to_real_flip_flop(self):
        self.tcl.eval('''
            dict set pinmap n0 {buf.o sink.d}
            dict set pinmap raw0 {ff0.q buf.i}
            lappend nets raw0
            lappend cells buf
            lappend allpins buf.i
            dict set models buf AL_BUFKEEP
            dict set upstream buf.i raw0
        ''')
        self.assertEqual(self.tcl.call('control_async_q_driver', 'n0'), 'ff0')
        self.apply()
        self.assertFalse(bool(self.tcl.call('dict', 'exists', self.tcl.getvar('properties'), 'buf')))

    def test_arbitrary_combinational_driver_cannot_be_treated_as_buffer(self):
        self.tcl.eval('dict set pinmap n0 {buf.o sink.d};lappend cells buf;dict set models buf AL_LUT1')
        with self.assertRaisesRegex(tkinter.TclError, 'Unreviewed synchronizer driver'):
            self.apply()
        self.assertEqual(int(self.tcl.getvar('writes')), 0)

    def test_wrong_register_count_stops_before_mutating_attributes(self):
        with self.assertRaisesRegex(tkinter.TclError, 'attribute count mismatch'):
            self.apply(3)
        self.assertEqual(int(self.tcl.getvar('writes')), 0)

    def test_missing_q_driver_is_rejected(self):
        self.tcl.eval('dict set pinmap n0 {sink.d}')
        with self.assertRaisesRegex(tkinter.TclError, 'one Q driver'):
            self.apply()

    def test_silently_ignored_setter_does_not_pass(self):
        self.tcl.setvar('ignored', 1)
        with self.assertRaisesRegex(tkinter.TclError, 'did not take effect'):
            self.apply()

    def test_similar_owner_name_cannot_impersonate_exact_cell(self):
        self.tcl.eval('dict set pinmap n0 {{ff[0].q} sink.d};set cells {ffX0Y ff1}')
        with self.assertRaisesRegex(tkinter.TclError, 'Ambiguous synchronizer attribute selector'):
            self.apply()

    def test_missing_packed_cell_property_is_rejected_even_when_net_is_marked(self):
        self.tcl.eval('dict set properties n0 1')
        with self.assertRaisesRegex(tkinter.TclError, 'did not take effect: get_cells'):
            self.tcl.call('verify_control_async_registers', ('n0', 'ff0'))

    def gpio_mapping(self, bits):
        self.tcl.setvar('regs', tuple(f'arm_servo_pwm_inst/gpio_meta[{i}]' for i in bits))
        self.tcl.eval('''
            set pinmap {}
            set nets $regs
            foreach net $regs {dict set pinmap $net [list ${net}_reg.q]}
        ''')
        return self.tcl.call('resolve_control_async_registers',
                             ('arm_servo_pwm_inst/gpio_meta.[0-9]+.$', 57))

    def test_q_driver_uses_verified_native_net_collection(self):
        self.assertEqual(self.tcl.call('control_async_q_driver', 'n0'), 'ff0')
        self.assertGreaterEqual(int(self.tcl.getvar('netqueries')), 2)

    def test_missing_net_is_rejected_before_any_global_fanout_query(self):
        self.tcl.eval('set nets {n1}')
        with self.assertRaisesRegex(tkinter.TclError, 'Ambiguous synchronizer attribute selector'):
            self.apply()
        self.assertEqual(int(self.tcl.getvar('writes')), 0)

    def prepare_packed(self):
        self.tcl.eval('''
            set properties {n0 1 n1 1 ff0 1}
            set models {ff0 DR1_PHY_LSLICE ff1 DR1_PHY_LSLICE}
            set placed_cells {ff0 1 ff1 1}
        ''')

    def bind_packed(self):
        return self.tcl.call('bind_packed_control_async_registers', self.output, ('^n[01]$', 2))

    def test_only_missing_packed_cell_metadata_is_restored(self):
        self.prepare_packed()
        self.bind_packed()
        self.assertEqual(int(self.tcl.getvar('writes')), 1)
        self.assertNotIn('get_nets', Path(self.output).read_text())

    def test_lost_net_attribute_is_never_repaired_or_hidden(self):
        self.prepare_packed()
        self.tcl.eval('dict unset properties n1')
        with self.assertRaisesRegex(tkinter.TclError, 'Original synchronizer net attribute lost'):
            self.bind_packed()
        self.assertEqual(int(self.tcl.getvar('writes')), 0)

    def test_unplaced_owner_is_rejected_before_rebinding(self):
        self.prepare_packed()
        self.tcl.eval('dict set placed_cells ff1 0')
        with self.assertRaisesRegex(tkinter.TclError, 'Expected a placed DR1 slice'):
            self.bind_packed()
        self.assertEqual(int(self.tcl.getvar('writes')), 0)

    def test_unexpected_cell_model_is_rejected_before_rebinding(self):
        self.prepare_packed()
        self.tcl.eval('dict set models ff1 AL_FF')
        with self.assertRaisesRegex(tkinter.TclError, 'Expected a placed DR1 slice'):
            self.bind_packed()
        self.assertEqual(int(self.tcl.getvar('writes')), 0)

    def test_ignored_packed_cell_setter_is_rejected(self):
        self.prepare_packed()
        self.tcl.setvar('ignored', 1)
        with self.assertRaisesRegex(tkinter.TclError, 'Packed synchronizer attribute did not take effect'):
            self.bind_packed()

    def test_shared_packed_owner_is_restored_once(self):
        self.prepare_packed()
        self.tcl.eval('dict unset properties ff0;dict set pinmap n1 {ff0.q sink1.d}')
        self.bind_packed()
        self.assertEqual(int(self.tcl.getvar('writes')), 1)

    def test_gate_gpio_excludes_only_unused_protocol_bits(self):
        result = self.gpio_mapping(range(64))
        self.assertEqual(int(self.tcl.call('dict', 'size', result)), 57)
        for bit in range(64):
            present = self.tcl.call('dict', 'exists', result, f'arm_servo_pwm_inst/gpio_meta[{bit}]')
            self.assertEqual(bool(present), bit <= 48 or bit >= 56)

    def test_packed_gpio_live_set_is_accepted(self):
        result = self.gpio_mapping([*range(49), *range(56, 64)])
        self.assertEqual(int(self.tcl.call('dict', 'size', result)), 57)

    def test_missing_live_gpio_bit_cannot_be_replaced_by_unused_bit(self):
        with self.assertRaisesRegex(tkinter.TclError, 'attribute count mismatch'):
            self.gpio_mapping([*range(48), *range(49, 58)])

    def test_duplicate_gpio_bit_cannot_mask_missing_live_bit(self):
        with self.assertRaisesRegex(tkinter.TclError, 'Invalid GPIO'):
            self.gpio_mapping([*range(48), 47, *range(56, 64)])


if __name__ == '__main__':
    unittest.main()

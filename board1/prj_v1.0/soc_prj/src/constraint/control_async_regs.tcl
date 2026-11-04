# Apply/verify physical synchronizer attributes on an existing mapped design.
# TD can ignore both HDL and set_property async_reg while the RTL directive
# switch is off. A post-gate repair therefore enables it explicitly, marks
# the logical nets AND their verified Q-driver cells, and checks every value.
# No timing exception or delay budget is added by this helper.
proc control_async_register_groups {} {
    return {
        {design_0_inst/conveyor_gp0_inst/motor_inst/enc_[ab]_sync.[01].$} 4
        {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/sync_r1.[0-9]+.$} 12:13
        {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/primary_addr_gray_sync.[0-9]+.$} 12:13
        {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/sync_r1.[0-9]+.$} 12
        {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/primary_addr_gray_sync.[0-9]+.$} 12
        {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/sync_r1.[0-9]+.$} 5
        {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/primary_addr_gray_sync.[0-9]+.$} 5
        {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/sync_r1.[0-9]+.$} 5
        {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/primary_addr_gray_sync.[0-9]+.$} 5
        {design_0_inst/design_0_pwm_0.*_Inst/.*/reset_release.[0-9]+.$} 6
        {servo_reset_pipe.[0-9]+.$} 2
        {design_top_wrapper_u/u_mipi_dphy_rx_over_lvds_wrapper/hs_reset_release.[0-9]+.$} 2
        {uidbuf_write_u0/.*u_wfifo/asy_[rw]_rst[01]$} 4
        {.*Video_Out_0_Inst/.*FIFO_INST/asy_[rw]_rst[01]$} 4
        {arm_servo_pwm_inst/gpio_meta.[0-9]+.$} 57
        {arm_servo_pwm_inst/gpio_sync.[0-9]+.$} 57
        {arm_servo_pwm_inst/commit_sync.[0-9]+.$} 3
        {uidbuf_write_u0/.*fs_cap_W0/vs_i_r[1-4]$} 4
        {uidbuf_write_u0/.*overflow_(meta|sync)$} 2
    }
}

proc control_async_singleton_pattern {kind name} {
    regsub -all {[][\\.^$*+?(){}|]} $name {.} masked
    set pattern [format {^%s$} $masked]
    set selected [uplevel #0 [list $kind -verbose -regexp $pattern]]
    if {[llength $selected] != 1 || [lindex $selected 0] ne $name} {
        error "Ambiguous synchronizer attribute selector: $kind $name"
    }
    return $pattern
}

proc verify_control_async_registers {mapping} {
    set checked_cells {}
    dict for {net owner} $mapping {
        foreach {kind object} [list get_nets $net get_cells $owner] {
            if {$kind eq "get_cells" && [dict exists $checked_cells $owner]} {continue}
            set pattern [control_async_singleton_pattern $kind $object]
            set value [uplevel #0 [format {get_property ASYNC_REG [%s -regexp %s]} $kind [list $pattern]]]
            if {![string is boolean -strict $value] || !$value} {
                error "ASYNC_REG did not take effect: $kind $object"
            }
            if {$kind eq "get_cells"} {dict set checked_cells $owner 1}
        }
    }
    puts "CONTROL_ASYNC_ATTRIBUTES_VERIFIED: [dict size $mapping] register nets, [dict size $checked_cells] Q-driver cells"
    return [dict size $mapping]
}

proc control_async_q_driver {net} {
    set visited {}
    for {set hop 0} {$hop < 8} {incr hop} {
        if {$net eq "" || [dict exists $visited $net]} {error "Invalid synchronizer driver chain"}
        dict set visited $net 1
        # A plain name containing brackets is not a native collection; TD
        # can silently treat it as empty and return every pin in the device.
        set pattern [control_async_singleton_pattern get_nets $net]
        set pins [uplevel #0 [format {get_pins -verbose -of [get_nets -regexp %s] *} [list $pattern]]]
        set drivers {}
        foreach pin $pins {if {[regexp {[.](q|oq[ab]|o)$} $pin]} {lappend drivers $pin}}
        if {[llength $drivers] != 1} {error "Synchronizer must have one Q driver: $net"}
        set driver [lindex $drivers 0]
        set owner [string range $driver 0 [expr {[string last . $driver]-1}]]
        if {[regexp {[.](q|oq[ab])$} $driver]} {return $owner}
        # Before packing, TD inserts AL_BUFKEEP pairs around kept nets. Walk
        # only a verified keep buffer, never arbitrary combinational logic.
        set pattern [control_async_singleton_pattern get_cells $owner]
        set model [uplevel #0 [format {get_property REF_NAME [get_cells -regexp %s]} [list $pattern]]]
        if {$model ne "AL_BUFKEEP"} {error "Unreviewed synchronizer driver buffer: $owner ($model)"}
        set pattern [control_async_singleton_pattern get_pins ${owner}.i]
        set upstream [uplevel #0 [format {get_nets -verbose -of [get_pins -regexp %s] *} [list $pattern]]]
        if {[llength $upstream] != 1} {error "Keep buffer must have one upstream net: $owner"}
        set net [lindex $upstream 0]
    }
    error "Synchronizer driver exceeds keep-buffer hop limit"
}

proc resolve_control_async_registers {{groups {}}} {
    if {$groups eq ""} {set groups [control_async_register_groups]}
    set mapping {}
    foreach {pattern expected} $groups {
        set regs [uplevel #0 [list get_regs -verbose -regexp $pattern]]
        # Kept, unused GPIO bits can still exist in a gate checkpoint but
        # disappear during packing. Only 0..48 and 56..63 drive the protocol.
        # Do not mark 49..55: ASYNC_REG would preserve otherwise unused FFs.
        if {$pattern in {{arm_servo_pwm_inst/gpio_meta.[0-9]+.$} {arm_servo_pwm_inst/gpio_sync.[0-9]+.$}}} {
            set live {}
            set seen {}
            foreach net $regs {
                if {![regexp {^arm_servo_pwm_inst/gpio_(meta|sync)\[([0-9]+)\]$} $net -> stage bit]
                    || $bit > 63 || [dict exists $seen $bit]} {
                    error "Invalid GPIO synchronizer register: $net"
                }
                dict set seen $bit 1
                if {$bit <= 48 || $bit >= 56} {lappend live $net}
            }
            set regs $live
        }
        set range [split $expected :]
        if {[llength $regs] < [lindex $range 0] || [llength $regs] > [lindex $range end]} {
            error "Synchronizer attribute count mismatch: $pattern; expected $expected, got [llength $regs]"
        }
        foreach net $regs {
            # Never query -of with an empty collection: that can select the
            # whole device. Only the net's one Q driver identifies its FF.
            if {$net eq ""} {error "Empty synchronizer register net"}
            set owner [control_async_q_driver $net]
            dict set mapping $net $owner
        }
    }
    if {[dict size $mapping] == 0} {error "No reviewed synchronizer attributes to apply"}
    return $mapping
}

proc apply_control_async_registers {constraint_path {groups {}}} {
    set mapping [resolve_control_async_registers $groups]
    set text "# Generated from reviewed register nets and exact Q-driver cells.\n"
    set cells {}
    dict for {net owner} $mapping {
        set pattern [control_async_singleton_pattern get_nets $net]
        append text [format {set_property async_reg true [get_nets -regexp %s]} [list $pattern]] "\n"
        if {![dict exists $cells $owner]} {
            set pattern [control_async_singleton_pattern get_cells $owner]
            append text [format {set_property async_reg true [get_cells -regexp %s]} [list $pattern]] "\n"
            dict set cells $owner 1
        }
    }
    file mkdir [file dirname $constraint_path]
    set stream [open $constraint_path w]
    puts -nonewline $stream $text
    close $stream
    uplevel #0 {set_param rtl directive:async_reg on}
    # read_sdc can silently ignore set_property on this TD release. Execute
    # the generated literal native calls as Tcl, then verify their effects.
    uplevel #0 [list source $constraint_path]
    verify_control_async_registers $mapping
    return $mapping
}

# Packing can retain every register-net attribute while dropping the wrapper
# cell attribute when an async FF shares a slice with an ordinary FF/LUT.
# Restore ONLY that physical wrapper metadata before routing. Missing logical
# net attributes, unplaced cells or unexpected models are never repaired here.
proc bind_packed_control_async_registers {constraint_path {groups {}}} {
    set mapping [resolve_control_async_registers $groups]
    set cells {}
    set missing {}
    dict for {net owner} $mapping {
        set pattern [control_async_singleton_pattern get_nets $net]
        set value [uplevel #0 [format {get_property ASYNC_REG [get_nets -regexp %s]} [list $pattern]]]
        if {![string is boolean -strict $value] || !$value} {
            error "Original synchronizer net attribute lost during placement: $net"
        }
        if {[dict exists $cells $owner]} {continue}
        dict set cells $owner 1
        set pattern [control_async_singleton_pattern get_cells $owner]
        set model [uplevel #0 [format {get_property REF_NAME [get_cells -regexp %s]} [list $pattern]]]
        set placed [uplevel #0 [format {get_property IS_PLACED [get_cells -regexp %s]} [list $pattern]]]
        if {$model ne "DR1_PHY_LSLICE" || ![string is boolean -strict $placed] || !$placed} {
            error "Expected a placed DR1 slice for synchronizer: $owner"
        }
        set value [uplevel #0 [format {get_property ASYNC_REG [get_cells -regexp %s]} [list $pattern]]]
        if {$value eq "" || ([string is boolean -strict $value] && !$value)} {
            dict set missing $owner $pattern
        } elseif {![string is boolean -strict $value]} {
            error "Invalid packed synchronizer attribute: $owner"
        }
    }
    # All original nets and all physical owners have been verified before
    # the first mutation. Do not conceal a lost logical synchronization chain.
    set text "# Restore packed-cell metadata; all reviewed logical nets retained ASYNC_REG.\n"
    dict for {owner pattern} $missing {
        append text [format {set_property async_reg true [get_cells -regexp %s]} [list $pattern]] "\n"
    }
    file mkdir [file dirname $constraint_path]
    set stream [open $constraint_path w]
    puts -nonewline $stream $text
    close $stream
    uplevel #0 {set_param rtl directive:async_reg on}
    uplevel #0 [list source $constraint_path]
    dict for {owner pattern} $missing {
        set value [uplevel #0 [format {get_property ASYNC_REG [get_cells -regexp %s]} [list $pattern]]]
        if {![string is boolean -strict $value] || !$value} {
            error "Packed synchronizer attribute did not take effect: $owner"
        }
        puts "CONTROL_ASYNC_PACKED_ATTRIBUTE_REBOUND: $owner"
    }
    puts "CONTROL_ASYNC_PACKED_ATTRIBUTES_VERIFIED: [dict size $mapping] register nets, [dict size $cells] Q-driver cells, [dict size $missing] restored cell attributes"
    return $mapping
}

# Staged query-only hardening; not sourced by the frozen async3 worker.
proc reset_binding_singleton_pattern {kind name} {
    if {$name eq ""} {error "Empty reset binding object"}
    regsub -all {[][\\.^$*+?(){}|]} $name {.} masked
    set pattern [format {^%s$} $masked]
    set selected [uplevel #0 [list $kind -verbose -regexp $pattern]]
    if {[llength $selected] != 1 || [lindex $selected 0] ne $name} {
        error "Ambiguous reset binding selector: $kind $name"
    }
    return $pattern
}
proc resolve_reset_sync_asr_pins {register_patterns} {
    set mapping {}
    foreach pattern $register_patterns {
        set regs [uplevel #0 [list get_regs -verbose -regexp $pattern]]
        if {[llength $regs] == 0} {error "Missing logical reset synchronizer: $pattern"}
        foreach reg $regs {
            set net_pattern [reset_binding_singleton_pattern get_nets $reg]
            set pins [uplevel #0 [format {get_pins -verbose -of [get_nets -regexp %s] *} [list $net_pattern]]]
            set drivers {}
            foreach pin $pins {if {[regexp {[.](q|oq[ab])$} $pin]} {lappend drivers $pin}}
            if {[llength $drivers] != 1} {error "Reset synchronizer must have one Q driver: $reg"}
            set owner [string range [lindex $drivers 0] 0 [expr {[string last . [lindex $drivers 0]]-1}]]
            set owner_pattern [reset_binding_singleton_pattern get_cells $owner]
            regsub -all {[][\\.^$*+?(){}|]} $owner {.} masked
            set asr_pattern [format {^%s[.](a)?sr$} $masked]
            set candidates [uplevel #0 [format {get_pins -verbose -of [get_cells -regexp %s] -regexp %s} [list $owner_pattern] [list $asr_pattern]]]
            set asr {}
            foreach pin $candidates {
                set pin_owner [string range $pin 0 [expr {[string last . $pin]-1}]]
                if {$pin_owner eq $owner && [regexp {[.](a)?sr$} $pin]} {lappend asr $pin}
            }
            if {[llength $asr] != 1} {error "Reset synchronizer owner must have one async-reset pin: $reg"}
            dict set mapping $reg [lindex $asr 0]
        }
    }
    return $mapping
}

proc bind_reset_sync_asr_pins {register_patterns legacy_pin_patterns expected_count {constraint_path {}}} {
    set mapping [resolve_reset_sync_asr_pins $register_patterns]
    if {[dict size $mapping] != $expected_count} {error "Incomplete logical reset synchronizer set"}
    set covered {}
    foreach pattern $legacy_pin_patterns {
        set covered [concat $covered [uplevel #0 [list get_pins -verbose -regexp $pattern]]]
    }
    set missing {}
    dict for {reg pin} $mapping {
        if {[lsearch -exact $covered $pin] < 0 && [lsearch -exact $missing $pin] < 0} {
            lappend missing $pin
            puts "LOGICAL_RESET_ASR_ADDED: $reg -> $pin"
        }
    }
    if {[llength $missing] > 0} {
        set constraints "# Generated from verified logical reset Q drivers.\n"
        foreach pin $missing {
            # TD's verbose query returns names, not a native constraint
            # collection. Re-query a verified singleton in the native call;
            # passing the Tcl list itself silently produces an invalid -to.
            regsub -all {[][\\.^$*+?(){}|]} $pin {.} masked
            set pattern [format {^%s$} $masked]
            set selected [uplevel #0 [list get_pins -verbose -regexp $pattern]]
            if {[llength $selected] != 1 || [lindex $selected 0] ne $pin} {
                error "Ambiguous physical reset exception selector: $pin"
            }
            append constraints [format {set_false_path -to [get_pins -regexp %s]} [list $pattern]] "\n"
        }
        # TD must parse the nested getter through read_sdc. Invoking the
        # same expression through Tcl uplevel can accept it without timing
        # the intended native pin. Retain production files for audit/import.
        set temporary [expr {$constraint_path eq ""}]
        if {$temporary} {
            set channel [file tempfile constraint_path]
        } else {
            file mkdir [file dirname $constraint_path]
            set channel [open $constraint_path w]
        }
        puts -nonewline $channel $constraints
        close $channel
        set status [catch {uplevel #0 [list read_sdc $constraint_path]} failure options]
        if {$temporary} {file delete -force $constraint_path}
        if {$status} {return -options $options $failure}
    }
    puts "LOGICAL_RESET_ASR_VERIFIED: [dict size $mapping] registers, [llength $missing] additional physical reset pins"
    return $mapping
}

proc bind_reviewed_reset_sync_pins {project_dir} {
    return [bind_reset_sync_asr_pins {
        {design_0_inst/design_0_pwm_0.*_Inst/.*/reset_release.[0-9]+.$}
        {servo_reset_pipe.[0-9]+.$}
        {design_top_wrapper_u/u_mipi_dphy_rx_over_lvds_wrapper/hs_reset_release.[0-9]+.$}
        {uidbuf_write_u0/.*u_wfifo/asy_[rw]_rst[01]$}
        {.*Video_Out_0_Inst/.*FIFO_INST/asy_[rw]_rst[01]$}
    } {
        {design_0_inst/design_0_pwm_0.*_Inst/.*reset_release_reg.*[.](a)?sr$}
        {servo_reset_pipe_reg.*[.](a)?sr$}
        {design_top_wrapper_u/u_mipi_dphy_rx_over_lvds_wrapper/hs_reset_release_reg.*[.](a)?sr$}
        {uidbuf_write_u0/.*u_wfifo/asy_[rw]_rst[01]_reg.*[.](a)?sr$}
        {.*Video_Out_0_Inst/.*FIFO_INST/asy_[rw]_rst[01]_reg.*[.](a)?sr$}
    } 18 [file join $project_dir soc_prj_Runs control_reset_bindings control_reset_asr_[pid].sdc]]
}

proc refresh_control_timing_constraints {project_dir} {
    # Packing changes .sr to .asr. TD can retain failed intermediate pin
    # matches across placement/phys_opt updates even when the final selector
    # matches correctly. Clear USER constraints and rebind the reviewed SDC
    # to the current physical model; retain the imported IP-owned constraints.
    uplevel #0 {clear_sdc -user}
    foreach name {timing.sdc control_cdc.sdc} {
        set path [file join $project_dir src constraint $name]
        uplevel #0 [list read_sdc $path]
    }
    # Packed cells may take an unrelated LUT's name. Complete the four
    # legacy name-based ASR selectors using each logical FF's actual owner.
    # D inputs/interstage timing and all unrelated reset pins remain timed.
    bind_reviewed_reset_sync_pins $project_dir
    return 1
}

proc check_release_diagnostics {report_path} {
    if {![file isfile $report_path]} {error "Missing native timing diagnostics: $report_path"}
    set f [open $report_path r];set detail [read $f];close $f
    foreach {name pattern} {
        no_clock {checking no clock\(([0-9]+)\)}
        clock_definition {checking clock definition\(([0-9]+)\)}
        invalid_constraint {checking invalid constraint\(([0-9]+)\)}
        combinational_loop {checking combinational loops\(([0-9]+)\)}
        ignored_exception {checking shadowed/ignored timing exception constraints\(([0-9]+)\)}
    } {
        if {![regexp $pattern $detail _ count]} {error "Incomplete native diagnostics: $name"}
        if {$count != 0} {error "Unresolved native diagnostics: $name=$count"}
    }
    return 1
}

proc report_release_diagnostics {report_path} {
    file delete -force $report_path
    uplevel #0 [format {check_timing -verbose -file %s} [list $report_path]]
    check_release_diagnostics $report_path
}

proc check_release_timing {report_path {require_closed 1} {require_full_coverage 1}} {
    set stream [open $report_path r]
    set detail [read $stream]
    close $stream
    foreach {key pattern} {
        coverage {STA coverage\s*:\s*([0-9.]+)%}
        setup {SWNS:\s*(-?[0-9.]+)ns}
        hold {HWNS:\s*(-?[0-9.]+)ns}
    } {
        if {![regexp $pattern $detail _ value]} {
            error "Missing $key in timing report $report_path"
        }
        set result($key) $value
    }
    # The hard PS interface clocks must time real endpoints. A virtual clock
    # with the right name/frequency and zero fanout must fail this check.
    foreach {suffix expected} {
        GEN_PL_GP0S 75.0 GEN_PL_GP1S 150.0 GEN_PL_HP0 150.0 GEN_PL_HP1 150.0
    } {
        set found 0
        foreach line [split $detail "\n"] {
            if {[regexp {clk[0-9]+:\s+([0-9.]+)\s+([0-9]+)\s+(\S+)} $line _ mhz fanout name] &&
                [string match "*.$suffix" $name] && $fanout > 0 &&
                abs($mhz-$expected) < 0.03} {
                set found 1
            }
        }
        if {!$found} {
            error "PS interface $suffix missing, disconnected or at wrong frequency (expected $expected MHz)"
        }
    }
    # Report all qualification failures together. Incomplete coverage must
    # not hide simultaneous setup/hold failures from the result summary.
    set failures {}
    if {$require_full_coverage && $result(coverage) < 100.0} {
        lappend failures "Timing coverage is $result(coverage)%; account for all unanalysed paths before release"
    }
    if {$require_closed && ![regexp {Confidence\s*:\s*Routed} $detail]} {
        lappend failures "Release timing must come from a routed design"
    }
    if {$require_closed && ($result(setup) < 0 || $result(hold) < 0)} {
        lappend failures "Timing not closed: setup=$result(setup) ns, hold=$result(hold) ns"
    }
    if {[llength $failures]} {error [join $failures "; "]}
    return [array get result]
}

# TD 2026.1_SP2 may emit an empty report_path_skew table. Instead of treating
# that as success, require a timed 6 ns datapath for EVERY used Gray bit.
# Since each datapath delay is nonnegative and <= 6 ns, this also bounds
# the inter-bit delay spread by 6 ns. Interstage paths retain normal timing.
proc check_timing_exception_report {path {require_routed 1}} {
    if {![file isfile $path]} {error "Missing timing exception analysis: $path"}
    set f [open $path r];set detail [read $f];close $f
    if {$require_routed && ![regexp {Confidence\s*:\s*Routed} $detail]} {
        error "Timing exception analysis is not routed: $path"
    }
    if {![regexp {MCP/SMD Summary} $detail]} {error "Incomplete timing exception report: $path"}
    # The ordinary SWNS/HWNS summary does not include SMD/MCP budgets.
    # Check ALL native group summaries, including synchronous interstage
    # budgets and reset/commit/data synchronizers, not just the Gray input.
    set summaries [regexp -all -inline -line {^(?:Max|Min)\s*:\s*(?:SWNS|HWNS)\s*(-?[0-9.]+)ns[^\n]*,\s*([0-9]+) Viol Endpoints,\s*([0-9]+) Total Endpoints,\s*([0-9]+) Paths Analyzed} $detail]
    set declarations [regexp -all -inline -line {^(?:Max|Min)\s*:} $detail]
    if {[llength $summaries] == 0 || [llength $summaries] != 5*[llength $declarations]} {
        error "Incomplete timing exception summaries: $path"
    }
    foreach {line slack violations endpoints paths} $summaries {
        if {$slack < 0 || $violations != 0} {error "Timing exception budget violated: $line"}
        if {$endpoints != $paths} {error "Incomplete timing exception endpoint analysis: $line"}
    }
    foreach {line slack} [regexp -all -inline {Slack\s*:\s*(-?[0-9.]+)ns} $detail] {
        if {$slack < 0} {error "Timing exception per-path budget violated: $slack ns"}
    }
    return 1
}

proc report_control_cdc_timing {output_dir} {
    # Physical implementation renames the original .d pins. Query stored
    # exceptions and remove old reports before writing fresh analysis.
    foreach name {control_cdc_all control_cdc_rd_to_wr control_cdc_wr_to_rd video_cdc_rd_to_wr video_cdc_wr_to_rd} {
        file delete -force [file join $output_dir ${name}.timing]
    }
    set path [file join $output_dir control_cdc_all.timing]
    uplevel #0 [format {report_timing_exception -ep_num 100 -path_num 1 -file %s} [list $path]]
    if {![file isfile $path]} {error "Fresh CDC report not generated: $path"}
    set f [open $path r];set full [read $f];close $f
    set header_end [string first "MCP/SMD Summary" $full]
    if {$header_end < 0} {error "Missing timing exception report header"}
    set header [string range $full 0 [expr {$header_end-1}]]
    set groups [regexp -all -inline -indices {(?m)^Path Group\s*:} $full]
    set seen {}
    for {set i 0} {$i < [llength $groups]} {incr i} {
        set first [lindex [lindex $groups $i] 0]
        set last [expr {$i+1 < [llength $groups] ? [lindex [lindex $groups [expr {$i+1}]] 0]-1 : [string length $full]-1}]
        set block [string range $full $first $last]
        set type_at [string first "Type " $block]
        if {$type_at < 0} {continue}
        set declaration [string range $block 0 $type_at]
        if {![string match "*set_max_delay -datapath_only*" $declaration] ||
            ![string match "*primary_addr_gray_reg*sync_r1*" $declaration]} {continue}
        if {![regexp {/(rd_to_wr|wr_to_rd)_cross_inst/} $declaration _ direction]} {continue}
        if {[string match "*uidbuf_write_u0/*u_wfifo/*" $declaration]} {
            set owner control
        } elseif {[string match "*Video_Out_0_Inst/*FIFO_INST/*" $declaration]} {
            set owner video
        } else {continue}
        set key ${owner}_cdc_${direction}
        if {[lsearch -exact $seen $key] >= 0} {error "Duplicate CDC timing group: $key"}
        lappend seen $key
        set f [open [file join $output_dir ${key}.timing] w]
        puts -nonewline $f "$header$block";close $f
    }
}

proc check_gray_cdc_report {path direction first last {require_routed 0}} {
    set width [expr {$last-$first+1}]
    if {![file isfile $path]} {error "Missing CDC analysis: $path"}
    set stream [open $path r]
    set detail [read $stream]
    close $stream
    if {$require_routed && ![regexp {Confidence\s*:\s*Routed} $detail]} {error "CDC analysis is not routed: $path"}
    if {![regexp {Type\s*:\s*SMD} $detail] ||
        ![regexp {set_max_delay -datapath_only[^\n]* 6\.000} $detail] ||
        ![regexp {Max\s*:\s*SWNS\s+(-?[0-9.]+)ns[^\n]*\s([0-9]+) Total Endpoints,\s+([0-9]+) Paths Analyzed} $detail _ worst endpoints paths] ||
        $endpoints != $width || $paths != $width} {
        error "Incomplete CDC datapath analysis for $direction"
    }
    if {$worst < 0} {error "CDC datapath budget violated for $direction: $worst ns"}
    set seen {}
    # Packing can put two logical FFs in one slice named after another bit or
    # stage. Native Routed paths identify the actual captured register NET in
    # their final 'reg ... net:' step. Check that logical endpoint, never infer
    # a register bit from its packed slice's display name.
    set register_steps [regexp -all -inline -line {^reg\s+[^\n]*net:\s+(\S+)} $detail]
    if {[llength $register_steps] > 0} {
        if {[llength $register_steps] != 2*$width} {error "Incomplete CDC register-net proof for $direction"}
        set levels [regexp -all -inline {Logic Level\s*:\s*([0-9]+)} $detail]
        if {[llength $levels] != 2*$width} {error "Missing CDC logic-level proof for $direction"}
        foreach {label level} $levels {
            if {$level != 0} {error "Combinational logic in registered Gray crossing: $direction"}
        }
        foreach {line net} $register_steps {
            set net [string trimright $net ,]
            if {![regexp {/(rd_to_wr|wr_to_rd)_cross_inst/sync_r1\[([0-9]+)\]$} $net _ found bit] || $found ne $direction} {
                error "Unexpected CDC logical endpoint: $net"
            }
            lappend seen $bit
        }
    } else {
        foreach line [split $detail "\n"] {
            if {[string match "End Point*" $line]} {
                if {![regexp {/(rd_to_wr|wr_to_rd)_cross_inst/sync_r1_reg\[([0-9]+)\](?:_syn_[0-9]+)?\.[A-Za-z0-9_]+\s} $line _ found bit] || $found ne $direction} {
                    error "Unexpected CDC endpoint: $line"
                }
                lappend seen $bit
            }
        }
    }
    set expected {}
    for {set bit $first} {$bit <= $last} {incr bit} {lappend expected $bit}
    if {[lsort -integer $seen] ne $expected} {
        error "Missing or duplicate CDC bits for $direction: $seen"
    }
    set slack_values [regexp -all -inline {Slack\s*:\s*(-?[0-9.]+)ns} $detail]
    if {[llength $slack_values] != 2*$width} {error "Missing CDC per-bit slack for $direction"}
    foreach {label slack} $slack_values {
        if {$slack < 0} {error "CDC per-bit budget violated for $direction: $slack ns"}
    }
    set delays [regexp -all -inline {Data Path Delay\s*:\s*(-?[0-9.]+)ns} $detail]
    if {[llength $delays] != 2*$width} {error "Missing CDC per-bit delay for $direction"}
    foreach {label delay} $delays {
        if {$delay < 0 || $delay > 6.0} {error "Invalid CDC datapath delay for $direction: $delay ns"}
    }
    return 1
}

proc check_control_cdc_timing {output_dir {require_routed 0}} {
    # The 128->64 gearbox discards synchronized binary read bit 0. Gray bit
    # 0 affects only that discarded bit. Packing can remove this unused
    # chain, while ASYNC_REG/DONT_TOUCH can retain it. Accept only the two
    # exact geometries, with every live bit independently checked below.
    set first_read 0
    set read_path [file join $output_dir control_cdc_rd_to_wr.timing]
    if {[file isfile $read_path]} {
        set f [open $read_path r];set detail [read $f];close $f
        if {[regexp {Confidence\s*:\s*Routed} $detail]} {
            set first_read 1
            if {[regexp {Max\s*:\s*SWNS\s+-?[0-9.]+ns[^\n]*\s13 Total Endpoints,\s+13 Paths Analyzed} $detail]} {
                set first_read 0
            }
        }
    }
    foreach {direction first last} [list rd_to_wr $first_read 12 wr_to_rd 0 11] {
        check_gray_cdc_report [file join $output_dir control_cdc_${direction}.timing] $direction $first $last $require_routed
    }
    foreach direction {rd_to_wr wr_to_rd} {
        check_gray_cdc_report [file join $output_dir video_cdc_${direction}.timing] $direction 0 4 $require_routed
    }
    return 1
}

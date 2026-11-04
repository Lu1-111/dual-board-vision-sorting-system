# Final audit/ECO for the current isolated routed design, never the old run.
# CONVEYOR_ROUTED_DB must identify the exact candidate being reviewed.
set project_dir [file dirname [file normalize [info script]]]
set checker [file join $project_dir timing_release_check.tcl]
source $checker
set routed_db {}
if {[info exists ::env(CONVEYOR_ROUTED_DB)] && [string trim $::env(CONVEYOR_ROUTED_DB)] ne ""} {
    set routed_db [file normalize $::env(CONVEYOR_ROUTED_DB)]
}
set output_dir [file dirname $routed_db]
set before_status [file join $output_dir conveyor_before.ts]
set before_report [file join $output_dir conveyor_before.timing]
set final_status [file join $output_dir conveyor_final.ts]
set final_report [file join $output_dir conveyor_final.timing]
set failed_db [file join $output_dir conveyor_timing_failed.db]
set checked_db [file join $output_dir conveyor_checked.db]
set checked_bit [file join $output_dir conveyor_checked.bit]
cd $project_dir
if {[catch {
    if {$routed_db eq ""} {
        error "Set CONVEYOR_ROUTED_DB to the exact routed candidate before auditing"
    }
    if {![file isfile $routed_db]} {
        error "Routed candidate missing; run build_conveyor_system.tcl first: $routed_db"
    }
    # A native report failure must not reuse a previous passing report.
    foreach report [list $before_status $before_report $final_status $final_report] {
        file delete -force $report
    }
    # Defer dynamic native paths until Tcl has expanded and quoted them.
    import_device dr1_90.db -package DR1M90GEG400 -speed 2
    uplevel #0 [format {import_db %s} [list $routed_db]]
    refresh_control_timing_constraints $project_dir
    update_timing -mode final
    uplevel #0 [format {report_timing_status -file %s} [list $before_status]]
    uplevel #0 [format {report_timing_summary -file %s} [list $before_report]]
    report_release_diagnostics [file join $output_dir conveyor_before_check.rpt]
    # Validate clocks/coverage before trying ECO. No SDC overlays or model cuts.
    check_release_timing $before_report 0
    set stream [open $before_status r]
    set before [read $stream]
    close $stream
    if {![regexp {HWNS:\s*(-?[0-9.]+)ns} $before _ hold]} {
        error "Cannot parse hold timing status"
    }
    if {$hold < 0} {
        fix_hold -eco
        refresh_control_timing_constraints $project_dir
        update_timing -mode final
    }
    uplevel #0 [format {report_timing_status -file %s} [list $final_status]]
    uplevel #0 [format {report_timing_summary -file %s} [list $final_report]]
    report_release_diagnostics [file join $output_dir conveyor_final_check.rpt]
    report_control_cdc_timing $output_dir
    check_control_cdc_timing $output_dir 1
    if {[catch {
        check_timing_exception_report [file join $output_dir control_cdc_all.timing]
        check_release_timing $final_report
    } timing_failure]} {
        uplevel #0 [format {export_db %s} [list $failed_db]]
        error $timing_failure
    }
    uplevel #0 [format {export_db %s} [list $checked_db]]
    uplevel #0 [format {bitgen -bit %s} [list $checked_bit]]
} failure]} {
    puts stderr "CONVEYOR_TIMING_CHECK_FAILED: $failure"
    exit 1
}
puts "CONVEYOR_ROUTED_TIMING_PASSED"
exit 0

# Audit the current routed candidate. Verification never generates a bitstream.
set project_dir [file dirname [file normalize [info script]]]
source [file join $project_dir timing_release_check.tcl]
set run_database {}
if {[info exists ::env(CONVEYOR_ROUTED_DB)] && [string trim $::env(CONVEYOR_ROUTED_DB)] ne ""} {
    set run_database [file normalize $::env(CONVEYOR_ROUTED_DB)]
}
set output_dir [file dirname $run_database]
set timing_report [file join $output_dir arm_servo_audit.timing]
set check_report [file join $output_dir arm_servo_audit_check.rpt]
cd $project_dir
if {[catch {
    if {$run_database eq ""} {error "Set CONVEYOR_ROUTED_DB to the exact routed candidate before auditing"}
    if {![file isfile $run_database]} {error "Current routed candidate missing: $run_database"}
    file delete -force $timing_report
    # Defer dynamic native paths until Tcl has expanded and quoted them.
    import_device dr1_90.db -package DR1M90GEG400 -speed 2
    uplevel #0 [format {import_db %s} [list $run_database]]
    refresh_control_timing_constraints $project_dir
    update_timing -mode final
    uplevel #0 [format {report_timing_summary -file %s} [list $timing_report]]
    report_release_diagnostics $check_report
    report_control_cdc_timing $output_dir
    check_control_cdc_timing $output_dir 1
    check_timing_exception_report [file join $output_dir control_cdc_all.timing]
    check_release_timing $timing_report
} error_text]} {
    puts stderr "ARM_SERVO_TIMING_CHECK_FAILED: $error_text"
    exit 1
}
puts "ARM_SERVO_TIMING_CHECK_PASSED"
exit 0

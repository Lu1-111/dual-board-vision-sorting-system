# Run using the installed TD bin/td_commands_prompt.exe.
# Check source elaboration, pin constraints and RTL synthesis without changing
# the project's saved syn_1 or phy_1 run results.
set project_dir [file dirname [file normalize [info script]]]
set project_file [file join $project_dir soc_prj.al]
cd $project_dir
if {[catch {
    exec python src/conveyor/integrate_conveyor.py
    import_device dr1_90.db -package DR1M90GEG400 -speed 2
    open_project $project_file
    elaborate -top system_top
    commit_param -step rtl
    read_adc {src/constraint/io.adc}
    read_sdc {src/constraint/timing.sdc}
    optimize_rtl
    report_area -file {log/arm_servo_rtl.area}
} error_text]} {
    puts stderr "ARM_SERVO_SYNTHESIS_FAILED: $error_text"
    exit 1
}
puts "ARM_SERVO_SYNTHESIS_PASSED"
exit 0

# Run using the installed TD bin/td_commands_prompt.exe.
# Elaborate the current project sources without resetting synthesis runs.
set project_dir [file dirname [file normalize [info script]]]
set project_file [file join $project_dir soc_prj.al]
cd $project_dir
if {[catch {
    exec python src/conveyor/integrate_conveyor.py
    import_device dr1_90.db -package DR1M90GEG400 -speed 2
    open_project $project_file
    elaborate -top system_top
} error_text]} {
    puts stderr "ARM_SERVO_ELABORATE_FAILED: $error_text"
    exit 1
}
puts "ARM_SERVO_ELABORATE_PASSED"
exit 0

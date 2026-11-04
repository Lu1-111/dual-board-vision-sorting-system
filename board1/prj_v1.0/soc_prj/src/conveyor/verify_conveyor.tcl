# Isolated elaboration/RTL synthesis. Does not edit system_top, the block design,
# saved main-project runs, pin constraints or existing board bitstreams.
# TD's command wrapper expects literal file arguments. Run from this directory.
file mkdir build-td
if {[catch {
    import_device dr1_90.db -package DR1M90GEG400 -speed 2
    open_project {E:/26FPGA/prj_v1.0/soc_prj/src/conveyor/conveyor.al}
    elaborate -top conveyor_axi
    commit_param -step rtl
    optimize_rtl
    report_area -file {E:/26FPGA/prj_v1.0/soc_prj/src/conveyor/build-td/conveyor_rtl.area}
} error_text]} {
    puts stderr "CONVEYOR_SYNTHESIS_FAILED: $error_text"
    exit 1
}
puts "CONVEYOR_SYNTHESIS_PASSED"
exit 0

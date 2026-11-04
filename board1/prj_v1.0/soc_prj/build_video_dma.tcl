# Fresh, isolated build of current sources. Existing synthesis/route runs survive.
# Default is synthesis. Set VIDEO_DMA_ROUTE=1 to place/route after model checks.
# No bitstream is produced by this diagnostic build.
set project_dir [file dirname [file normalize [info script]]]
set project_file [file join $project_dir soc_prj.al]
# Every invocation has its own versioned run; historical evidence is immutable.
set run_name control_pipeline_[clock format [clock seconds] -format %Y%m%d_%H%M%S]
if {[info exists ::env(VIDEO_DMA_RUN)]} {set run_name $::env(VIDEO_DMA_RUN)}
if {![regexp {^[A-Za-z0-9_][A-Za-z0-9_-]*$} $run_name]} {
    puts stderr "VIDEO_DMA_BUILD_FAILED: Invalid VIDEO_DMA_RUN: use a simple directory name"
    exit 1
}
set output_dir [file join $project_dir soc_prj_Runs $run_name]
if {[file exists $output_dir]} {
    puts stderr "VIDEO_DMA_BUILD_FAILED: Run already exists; choose a new VIDEO_DMA_RUN: $output_dir"
    exit 1
}
file mkdir $output_dir
# TD's native parser changes behavior across design stages. Generate a worker
# with literal file arguments before starting any expensive design command.
set worker_body {
if {[catch {
    source [file join @PROJECT_DIR@ timing_release_check.tcl]
    cd @PROJECT_DIR@
    # Reapply and verify post-generation clock wiring and GP0 integration.
    exec python src/conveyor/integrate_conveyor.py
    import_device dr1_90.db -package DR1M90GEG400 -speed 2
    open_project @PROJECT_FILE@
    set_param place async_reg on
    set_param place opt_timing high
    set_param place effort high
    set_param place fix_hold on
    commit_param -step design
    elaborate -top system_top
    read_sdc -ip design_0_ARM_Processor_System_0 design_0/ip/design_0_ARM_Processor_System_0/design_0_ARM_Processor_System_0.sdc
    read_sdc -ip design_0_video_timing_generator_v01_0 design_0/ip/design_0_video_timing_generator_v01_0/design_0_video_timing_generator_v01_0.sdc
    commit_param -step rtl
    read_adc src/constraint/io.adc
    read_sdc src/constraint/timing.sdc
    optimize_rtl
    export_db @ARG_run_video_dma_rtl_db@
    # Native timing-priority mapping improves the NPU same-domain paths in
    # the isolated comparison; clocks and RTL behavior are unchanged.
    set_param gate map_mode logic_level_prior
    set_param gate map_strategy 2
    set_param gate add_map_lut_struct on
    set_param gate multi_thread_mapping partition
    commit_param -step gate
    optimize_gate -packarea @ARG_run_video_dma_gate_area@
    legalize_phy_inst
    # Prune unused legacy bus bits during synthesis, then explicitly enable
    # and verify ASYNC_REG on every reviewed live net and its real FF cell.
    # The helper enables the native directive before setting properties.
    source src/constraint/control_async_regs.tcl
    apply_control_async_registers [file join @OUTPUT_DIR@ control_async_bindings.tcl]
    export_db @ARG_run_control_unconstrained_gate_db@
    source src/constraint/control_cdc_check.tcl
    read_sdc src/constraint/control_cdc.sdc
    export_db @ARG_run_video_dma_gate_db@
    report_timing_summary -file @ARG_run_video_dma_gate_timing@
    report_control_cdc_timing @OUTPUT_DIR@
    check_control_cdc_timing @OUTPUT_DIR@
    report_path_skew -file @ARG_run_control_cdc_gate_skew@
    puts "VIDEO_DMA_SYNTHESIS_COMPLETED"
    # This is a diagnostic implementation, not a release. Validate the PS
    # clock model now; incomplete external I/O constraints still block the
    # final release check after routing, but do not prevent investigation.
    check_release_timing @ARG_run_video_dma_gate_timing@ 0 0
    check_timing -verbose -file @ARG_run_video_dma_gate_check_rpt@
    check_release_diagnostics @ARG_run_video_dma_gate_check_rpt@
    if {[info exists ::env(VIDEO_DMA_ROUTE)] && $::env(VIDEO_DMA_ROUTE) eq "1"} {
        # Diagnostic routing requires a valid PS clock model. Full coverage
        # remains mandatory in the final release check below.
        commit_param -step place
        place
        # Save expensive work before any subsequent model/diagnostic failure.
        export_db @ARG_run_video_dma_placed_db@
        refresh_control_timing_constraints @PROJECT_DIR@
        source src/constraint/control_cdc_check.tcl
        bind_packed_control_async_registers [file join @OUTPUT_DIR@ control_async_packed_bindings.tcl]
        set_param route post_route_opt high
        set_param route postrt_opt_extra_round 4
        commit_param -step route
        route
        export_db @ARG_run_video_dma_routed_raw_db@
        refresh_control_timing_constraints @PROJECT_DIR@
        source src/constraint/control_cdc_check.tcl
        verify_control_async_registers [resolve_control_async_registers]
        update_timing -mode final
        export_db @ARG_run_video_dma_routed_db@
        report_timing_summary -file @ARG_run_video_dma_routed_timing@
        report_area -file @ARG_run_video_dma_routed_area@
        report_timing_summary -ep_num 2000 -path_num 1 -viol_only -file @ARG_run_all_violations_timing@
        report_timing_exception -ep_num 2000 -path_num 1 -viol_only -file @ARG_run_exception_violations_timing@
        check_timing -verbose -file @ARG_run_video_dma_routed_check_rpt@
        check_release_diagnostics @ARG_run_video_dma_routed_check_rpt@
        report_path_skew -file @ARG_run_control_cdc_routed_skew@
        report_control_cdc_timing @OUTPUT_DIR@
        check_control_cdc_timing @OUTPUT_DIR@ 1
        check_timing_exception_report @ARG_run_control_cdc_all_timing@
        check_release_timing @ARG_run_video_dma_routed_timing@
        puts "VIDEO_DMA_ROUTED_TIMING_PASSED"
    }
} failure]} {
    puts stderr "VIDEO_DMA_BUILD_FAILED: $failure"
    exit 1
}
exit 0

}
set replacements [list @PROJECT_DIR@ [list $project_dir] @PROJECT_FILE@ [list $project_file] @OUTPUT_DIR@ [list $output_dir]]
lappend replacements @ARG_run_all_violations_timing@ [list [file join $output_dir all_violations.timing]]
lappend replacements @ARG_run_control_cdc_all_timing@ [list [file join $output_dir control_cdc_all.timing]]
lappend replacements @ARG_run_control_cdc_gate_skew@ [list [file join $output_dir control_cdc_gate.skew]]
lappend replacements @ARG_run_control_cdc_routed_skew@ [list [file join $output_dir control_cdc_routed.skew]]
lappend replacements @ARG_run_control_unconstrained_gate_db@ [list [file join $output_dir control_unconstrained_gate.db]]
lappend replacements @ARG_run_exception_violations_timing@ [list [file join $output_dir exception_violations.timing]]
lappend replacements @ARG_run_video_dma_gate_area@ [list [file join $output_dir video_dma_gate.area]]
lappend replacements @ARG_run_video_dma_gate_db@ [list [file join $output_dir video_dma_gate.db]]
lappend replacements @ARG_run_video_dma_gate_timing@ [list [file join $output_dir video_dma_gate.timing]]
lappend replacements @ARG_run_video_dma_gate_check_rpt@ [list [file join $output_dir video_dma_gate_check.rpt]]
lappend replacements @ARG_run_video_dma_placed_db@ [list [file join $output_dir video_dma_placed.db]]
lappend replacements @ARG_run_video_dma_routed_area@ [list [file join $output_dir video_dma_routed.area]]
lappend replacements @ARG_run_video_dma_routed_db@ [list [file join $output_dir video_dma_routed.db]]
lappend replacements @ARG_run_video_dma_routed_timing@ [list [file join $output_dir video_dma_routed.timing]]
lappend replacements @ARG_run_video_dma_routed_check_rpt@ [list [file join $output_dir video_dma_routed_check.rpt]]
lappend replacements @ARG_run_video_dma_routed_raw_db@ [list [file join $output_dir video_dma_routed_raw.db]]
lappend replacements @ARG_run_video_dma_rtl_db@ [list [file join $output_dir video_dma_rtl.db]]
set worker_text [string map $replacements $worker_body]
set worker_path [file join $output_dir build_resolved.tcl]
set worker_file [open $worker_path {WRONLY CREAT EXCL}]
puts $worker_file $worker_text
close $worker_file
puts "VIDEO_DMA_RESOLVED_SCRIPT: $worker_path"
source $worker_path

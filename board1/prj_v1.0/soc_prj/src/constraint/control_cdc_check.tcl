# Validate stable register net selectors before mapping/physical analysis.
# TD may rename packed cell pins but preserves these exact register nets.
# Capture read Gray bit 0 is unused after 128->64 gearbox conversion. KEEP
# retains it when mapped; packing removes it. Reports require every live bit
# by number (mapped 0..12, routed 1..12), not just this cardinality guard.
foreach {pattern expected} {
    {design_0_inst/conveyor_gp0_inst/motor_inst/enc_[ab]_sync.[01].$} 4
    {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/primary_addr_gray_reg.[0-9]+.$} 12:13
    {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/sync_r1.[0-9]+.$} 12:13
    {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/primary_addr_gray_sync.[0-9]+.$} 12:13
    {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/primary_addr_gray_reg.[0-9]+.$} 12
    {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/sync_r1.[0-9]+.$} 12
    {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/primary_addr_gray_sync.[0-9]+.$} 12
    {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/primary_addr_gray_reg.[0-9]+.$} 5
    {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/sync_r1.[0-9]+.$} 5
    {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/primary_addr_gray_sync.[0-9]+.$} 5
    {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/primary_addr_gray_reg.[0-9]+.$} 5
    {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/sync_r1.[0-9]+.$} 5
    {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/primary_addr_gray_sync.[0-9]+.$} 5
    {design_0_inst/design_0_pwm_0.*_Inst/.*/reset_release.[0-9]+.$} 6
    {servo_reset_pipe.[0-9]+.$} 2
    {design_top_wrapper_u/u_mipi_dphy_rx_over_lvds_wrapper/hs_reset_release.[0-9]+.$} 2
    {arm_servo_pwm_inst/gpio_meta.([0-9]|[1-3][0-9]|4[0-8]|5[6-9]|6[0-3]).$} 57
    {arm_servo_pwm_inst/gpio_sync.([0-9]|[1-3][0-9]|4[0-8]|5[6-9]|6[0-3]).$} 57
    {arm_servo_pwm_inst/commit_sync.[0-9]+.$} 3
    {uidbuf_write_u0/.*u_wfifo/asy_[rw]_rst[01]$} 4
    {uidbuf_write_u0/.*fs_cap_W0/vs_i_r[1-4]$} 4
    {uidbuf_write_u0/.*fs_cap_W0/vs_i_r1$} 1
    {uidbuf_write_u0/.*overflow_latched$} 1
    {uidbuf_write_u0/.*overflow_meta$} 1
    {uidbuf_write_u0/.*overflow_sync$} 1
    {.*Video_Out_0_Inst/.*FIFO_INST/asy_[rw]_rst[01]$} 4
} {
    set found [get_regs -verbose -regexp $pattern]
    set range [split $expected :]
    set low [lindex $range 0]
    set high [lindex $range end]
    if {[llength $found] < $low || [llength $found] > $high} {
        error "Reviewed CDC register count mismatch: $pattern; expected $expected, got [llength $found]"
    }
}
foreach pattern {
    {design_0_inst/design_0_pwm_0.*_Inst/.*reset_release_reg.*[.](a)?sr$}
    {servo_reset_pipe_reg.*[.](a)?sr$}
    {design_top_wrapper_u/u_mipi_dphy_rx_over_lvds_wrapper/hs_reset_release_reg.*[.](a)?sr$}
    {uidbuf_write_u0/.*u_wfifo/asy_[rw]_rst[01]_reg.*[.](a)?sr$}
    {.*Video_Out_0_Inst/.*FIFO_INST/asy_[rw]_rst[01]_reg.*[.](a)?sr$}
} {
    if {[llength [get_pins -verbose -regexp $pattern]] == 0} {
        error "Reviewed CDC async reset pin missing: $pattern"
    }
}
puts "CONTROL_CDC_ENDPOINTS_VERIFIED"
puts "VIDEO_FIFO_CDC_ENDPOINTS_VERIFIED"

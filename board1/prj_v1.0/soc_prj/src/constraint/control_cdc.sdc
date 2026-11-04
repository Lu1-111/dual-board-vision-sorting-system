# Reviewed capture/frame/servo/reset and VideoOut FIFO synchronizers.
# TD re-evaluates SDC after packing: use exact register NET names, since
# mapped .d/.q pins disappear. Anchored regex excludes *_input aliases.
# No backslash escapes: TD passes native selectors through its own parser.

# External AB Hall encoder is asynchronous to the 75 MHz control clock.
# Exempt only the pin-to-first-stage paths, retain second-stage setup/hold.
set_false_path -from [get_ports {encoder_a}] -to [get_regs -regexp {design_0_inst/conveyor_gp0_inst/motor_inst/enc_a_sync.0.$}]
set_false_path -from [get_ports {encoder_b}] -to [get_regs -regexp {design_0_inst/conveyor_gp0_inst/motor_inst/enc_b_sync.0.$}]
set_max_delay -from [get_regs -regexp {design_0_inst/conveyor_gp0_inst/motor_inst/enc_a_sync.0.$}] -to [get_regs -regexp {design_0_inst/conveyor_gp0_inst/motor_inst/enc_a_sync.1.$}] 1.000
set_max_delay -from [get_regs -regexp {design_0_inst/conveyor_gp0_inst/motor_inst/enc_b_sync.0.$}] -to [get_regs -regexp {design_0_inst/conveyor_gp0_inst/motor_inst/enc_b_sync.1.$}] 1.000

# Waive async assertion only: .sr (mapped) or .asr (physical), never .ssr.
set_false_path -to [get_pins -regexp {design_0_inst/design_0_pwm_0.*_Inst/.*reset_release_reg.*[.](a)?sr$}]
set_false_path -to [get_pins -regexp {servo_reset_pipe_reg.*[.](a)?sr$}]
set_false_path -to [get_pins -regexp {design_top_wrapper_u/u_mipi_dphy_rx_over_lvds_wrapper/hs_reset_release_reg.*[.](a)?sr$}]
set_false_path -to [get_pins -regexp {uidbuf_write_u0/.*u_wfifo/asy_[rw]_rst[01]_reg.*[.](a)?sr$}]

# Only the first frame-indicator receiving register is asynchronous.
set_false_path -to [get_regs -regexp {uidbuf_write_u0/.*fs_cap_W0/vs_i_r1$}]

# Sticky single-bit overflow indication. Only its asynchronous receiving
# stage is excepted; the second stage retains the existing 1 ns budget.
set_false_path -from [get_regs -regexp {uidbuf_write_u0/.*overflow_latched$}] -to [get_regs -regexp {uidbuf_write_u0/.*overflow_meta$}]
set_max_delay -from [get_regs -regexp {uidbuf_write_u0/.*overflow_meta$}] -to [get_regs -regexp {uidbuf_write_u0/.*overflow_sync$}] 1.000

# Registered Gray -> first stage: 6 ns versus fastest 6.4 ns clock period.
# -datapath_only waives hold ONLY on these asynchronous first-stage paths.
# Native reports must check every retained bit after mapping AND routing.
set_max_delay -datapath_only -from [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/primary_addr_gray_reg.[0-9]+.$}] -to [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/sync_r1.[0-9]+.$}] 6.000
set_path_skew -from [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/primary_addr_gray_reg.[0-9]+.$}] -to [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/sync_r1.[0-9]+.$}] 6.000
set_max_delay -datapath_only -from [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/primary_addr_gray_reg.[0-9]+.$}] -to [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/sync_r1.[0-9]+.$}] 6.000
set_path_skew -from [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/primary_addr_gray_reg.[0-9]+.$}] -to [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/sync_r1.[0-9]+.$}] 6.000

# VideoOut HAS_ASYNC_CLK=1 / ADDR_WIDTH=5; 20 mapped connections audited.
set_false_path -to [get_pins -regexp {.*Video_Out_0_Inst/.*FIFO_INST/asy_[rw]_rst[01]_reg.*[.](a)?sr$}]
set_max_delay -datapath_only -from [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/primary_addr_gray_reg.[0-9]+.$}] -to [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/sync_r1.[0-9]+.$}] 6.000
set_path_skew -from [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/primary_addr_gray_reg.[0-9]+.$}] -to [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/sync_r1.[0-9]+.$}] 6.000
set_max_delay -datapath_only -from [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/primary_addr_gray_reg.[0-9]+.$}] -to [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/sync_r1.[0-9]+.$}] 6.000
set_path_skew -from [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/primary_addr_gray_reg.[0-9]+.$}] -to [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/sync_r1.[0-9]+.$}] 6.000

# KEEP preserves stages. Tight interstage budget retains clock skew and
# normal hold checks; register endpoints also include clock-to-Q delay.
set_max_delay -from [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/sync_r1.[0-9]+.$}] -to [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/rd_to_wr_cross_inst/primary_addr_gray_sync.[0-9]+.$}] 1.000
set_max_delay -from [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/sync_r1.[0-9]+.$}] -to [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/wr_to_rd_cross_inst/primary_addr_gray_sync.[0-9]+.$}] 1.000
set_max_delay -from [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/sync_r1.[0-9]+.$}] -to [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/rd_to_wr_cross_inst/primary_addr_gray_sync.[0-9]+.$}] 1.000
set_max_delay -from [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/sync_r1.[0-9]+.$}] -to [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/wr_to_rd_cross_inst/primary_addr_gray_sync.[0-9]+.$}] 1.000
set_max_delay -from [get_regs -regexp {design_0_inst/design_0_pwm_0.*_Inst/.*/reset_release.[0-9]+.$}] -to [get_regs -regexp {design_0_inst/design_0_pwm_0.*_Inst/.*/reset_release.[0-9]+.$}] 1.000
set_max_delay -from [get_regs -regexp {servo_reset_pipe.[0-9]+.$}] -to [get_regs -regexp {servo_reset_pipe.[0-9]+.$}] 1.000
set_max_delay -from [get_regs -regexp {design_top_wrapper_u/u_mipi_dphy_rx_over_lvds_wrapper/hs_reset_release.[0-9]+.$}] -to [get_regs -regexp {design_top_wrapper_u/u_mipi_dphy_rx_over_lvds_wrapper/hs_reset_release.[0-9]+.$}] 1.000
set_max_delay -from [get_regs -regexp {arm_servo_pwm_inst/commit_sync.[0-9]+.$}] -to [get_regs -regexp {arm_servo_pwm_inst/commit_sync.[0-9]+.$}] 1.000
set_max_delay -from [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/asy_[rw]_rst[01]$}] -to [get_regs -regexp {uidbuf_write_u0/.*u_wfifo/asy_[rw]_rst[01]$}] 1.000
set_max_delay -from [get_regs -regexp {uidbuf_write_u0/.*fs_cap_W0/vs_i_r[1-4]$}] -to [get_regs -regexp {uidbuf_write_u0/.*fs_cap_W0/vs_i_r[1-4]$}] 1.000
set_max_delay -from [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/asy_[rw]_rst[01]$}] -to [get_regs -regexp {.*Video_Out_0_Inst/.*FIFO_INST/asy_[rw]_rst[01]$}] 1.000
set_max_delay -from [get_regs -regexp {arm_servo_pwm_inst/gpio_meta.[0-9]+.$}] -to [get_regs -regexp {arm_servo_pwm_inst/gpio_sync.[0-9]+.$}] 1.000

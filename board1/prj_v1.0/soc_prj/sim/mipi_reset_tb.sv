`timescale 1ns/1ps
// Exercise the real wrapper/reset wiring. Vendor encrypted receive/align IP
// is replaced only at its documented ports; this is not a CSI decoder model.
module mipi_reset_tb;
    reg hs_clk = 0;
    reg lp_clk = 0;
    reg rst = 0;
    reg running = 0;
    wire byte_clk, byte_rst;
    integer checks = 0;
    always #3.2 if (running) hs_clk = ~hs_clk;
    always #7.142 lp_clk = ~lp_clk;
    design_top_wrapper dut (
        // The PLL stub interprets this input as its test lock signal.
        .I_sys_clk_156m(!rst), .I_clk_hs_p(hs_clk),
        .I_data_hs_p(4'b0), .I_data_lp_p(4'b1111), .I_data_lp_n(4'b1111),
        .O_imx334_clk_24m(), .S_pll_lock(), .S_lane_error(),
        .S_hs_rx_clk(byte_clk), .O_ISP_tdata(), .O_ISP_tlast(),
        .O_ISP_tuser(), .O_ISP_tvalid()
    );
    assign byte_rst = dut.S_hs_rst;
    task expect_reset(input bit expected);
        #0.001;
        if (byte_rst !== expected) $fatal(1, "byte reset expected %b got %b", expected, byte_rst);
        if (dut.u_mipi_dphy_rx_over_lvds_wrapper.u_hs_rx_wrapper.u_channel_aligner_wrapper.I_rst_n !== !expected)
            $fatal(1, "channel aligner did not share byte-domain reset");
        if (dut.u_csi_unpacket.I_rst_n !== !expected ||
            dut.u_raw10_unpacket.I_rst_n !== !expected ||
            dut.image_correction.I_rst_n !== !expected ||
            dut.u_isp_top.I_rst_n !== !expected)
            $fatal(1, "CSI/RAW/image/ISP reset release diverged");
        if (dut.u_mipi_dphy_rx_over_lvds_wrapper.u_rx_clk_gen.I_rst !== rst)
            $fatal(1, "clock divider raw reset changed");
        checks = checks + 1;
    endtask
    initial begin
        for (integer phase=0; phase<32; phase=phase+1) begin
            running = 0; hs_clk = 0;
            #0.1 rst = 1;
            expect_reset(1);
            #10 rst = 0;
            #31 expect_reset(1); // No forwarded clock: no reset release.
            #(0.05 + phase*0.07);
            running = 1;
            @(posedge byte_clk); expect_reset(1);
            @(posedge byte_clk); expect_reset(0);
            repeat (5) @(posedge byte_clk);
            expect_reset(0);
            @(negedge byte_clk); running = 0;
            #1.1 rst = 1;
            expect_reset(1); // Assertion must work with a stopped clock.
            #1.1 rst = 0;
            #20 expect_reset(1);
        end
        $display("MIPI_RESET_PASS checks=%0d phases=32 stopped_clock=1", checks);
        $finish;
    end
    initial begin #100000; $fatal(1, "timeout"); end
endmodule

module PLL(input refclk, output lock, clk0_out, clk1_out, clk2_out, clk3_out);
    assign lock=refclk;
    assign clk0_out=0, clk1_out=0, clk2_out=0, clk3_out=0;
endmodule

module csi_unpacket(input I_clk, I_rst_n, I_hs_valid, input [31:0] I_hs_data,
    output O_csi_frame_start, O_csi_frame_end, O_csi_valid, output [31:0] O_csi_data);
    assign O_csi_frame_start=0, O_csi_frame_end=0, O_csi_valid=0, O_csi_data=0;
endmodule

module raw10_unpacket_4lane(input I_clk, I_rst_n, I_csi_frame_start, I_csi_frame_end,
    I_csi_valid, input [31:0] I_csi_data,
    output O_raw10_frame_start, O_raw10_frame_end, O_raw10_valid, output [39:0] O_raw10_data);
    assign O_raw10_frame_start=0, O_raw10_frame_end=0, O_raw10_valid=0, O_raw10_data=0;
endmodule

module image_correction #(parameter DATA_WIDTH=40, TDEST_WIDTH=10) (
    input I_clk, I_rst_n, input [DATA_WIDTH-1:0] I_raw_data,
    input I_raw_valid, I_raw_frame_start, I_raw_frame_end,
    output [DATA_WIDTH-1:0] O_raw_tdata, output O_raw_tlast,
    output [TDEST_WIDTH-1:0] O_raw_tdest, output O_raw_tvalid, O_raw_tuser);
    assign O_raw_tdata=0, O_raw_tlast=0, O_raw_tdest=0, O_raw_tvalid=0, O_raw_tuser=0;
endmodule

module isp_top(input axi4s_video_aclk, I_rst_n, I_tlast, I_tuser,
    input [39:0] I_tdata, input I_tvalid, input [9:0] I_tdest,
    input O_tready, output [127:0] O_tdata, output O_tlast, O_tuser, O_tvalid,
    output I_tready);
    assign I_tready=O_tready, O_tdata=0, O_tlast=0, O_tuser=0, O_tvalid=0;
endmodule

module rx_clk_gen #(parameter DEVICE="DR1") (
    input I_mipi_clk_p, I_rst, output O_clk, O_clk_x2, O_clk_x4
);
    assign O_clk=I_mipi_clk_p, O_clk_x2=I_mipi_clk_p, O_clk_x4=I_mipi_clk_p;
endmodule

module data_lane_rx_wrapper #(parameter DEVICE="DR1") (
    input I_hs_clk_x4, I_hs_clk_x2, I_hs_clk, I_lp_clk, I_rst,
    input I_lane_hs_invert, I_lane_lp_invert,
    input I_mipi_hs_p, I_mipi_lp_p, I_mipi_lp_n,
    output O_hs_valid, output [7:0] O_hs_data, output O_lane_error
);
    assign O_hs_valid=0, O_hs_data=0, O_lane_error=0;
endmodule

module channel_aligner_wrapper #(parameter CH_NUM=4, DATA_WIDTH=8) (
    input I_clk, I_rst_n, input [CH_NUM-1:0] I_ch_valid,
    input [DATA_WIDTH-1:0] I_ch_data[CH_NUM-1:0],
    output [CH_NUM-1:0] O_ch_aligner_valid,
    output [DATA_WIDTH-1:0] O_ch_aligner_data[CH_NUM-1:0]
);
    assign O_ch_aligner_valid=0;
    for (genvar n=0; n<CH_NUM; n=n+1) assign O_ch_aligner_data[n]=0;
endmodule

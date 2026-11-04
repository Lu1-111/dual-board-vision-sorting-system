/************************************************************\
**	Copyright (c) 2012-2025 Anlogic Inc.
**	All Right Reserved.
\************************************************************/
/************************************************************\
**	Build time: Mar 31 2025 15:59:45
**	TD version	:	5.9.151508
************************************************************/
///////////////////////////////////////////////////////////////////////////////
//	Input frequency:                50.0 MHZ
//	Clock multiplication factor: 3
//	Clock division factor:       1
//	Clock information:
//		Clock name	| Frequency 	| Phase shift 
//		C0        	| 150.000000   MHZ	    |  0.0000 DEG 
//		C1        	| 50.000000   MHZ	    |  0.0000 DEG 
//		C2        	| 70.000000   MHZ		|  0.0000 DEG 
//		C3        	| 23.863636   MHZ		|  0.0000 DEG 
///////////////////////////////////////////////////////////////////////////////
`timescale 1 ns / 100 fs 

module PLL
(
  input                         refclk,
  output                        clk0_out,
  output                        clk1_out,
  output                        clk2_out,
  output                        clk3_out,
  output                        lock
);
  wire							  clk0_buf;
DR1_LOGIC_BUFG bufg_feedback (
 .i(clk0_buf), 
 .o(clk0_out) 
 ); 

 
 
 




  dr1_phy_pll_59fc11d80b0d
  #(
      .FBKCLK("CLKC0_EXT"),
      .REFCLK_DIV(1),
      .FBCLK_DIV(3),
      .CLKC0_FPHASE(0),
      .CLKC1_FPHASE(0),
      .CLKC2_FPHASE(0),
      .CLKC3_FPHASE(0),
      .CLKC0_CPHASE(6),
      .CLKC1_CPHASE(20),
      .CLKC2_CPHASE(14),
      .CLKC3_CPHASE(43),
      .CLKC0_DIV(7),
      .CLKC1_DIV(21),
      .CLKC2_DIV(15),
      .CLKC3_DIV(44),
      .CLKC0_DUTY_INT(4),
      .CLKC1_DUTY_INT(11),
      .CLKC2_DUTY_INT(8),
      .CLKC3_DUTY_INT(22),
      .CLKC0_ENABLE("ENABLE"),
      .CLKC1_ENABLE("ENABLE"),
      .CLKC2_ENABLE("ENABLE"),
      .CLKC3_ENABLE("ENABLE"),
      .FIN("50.0"),
      .FEEDBK_MODE("NORMAL"),
      .PLL_USR_RST("DISABLE"),
      .PLL_FEED_TYPE("EXTERNAL"),
      .LPF_RES(2),
      .LPF_CAP(2),
      .ICP_CUR(12),
      .GMC_GAIN(2),
      .FRAC_ENABLE("DISABLE"),
      .DITHER_ENABLE("DISABLE"),
      .SDM_FRAC(0),
      .SSC_AMP(0.0000),
      .MPHASE_ENABLE("DISABLE"),
      .DYN_PHASE_PATH_SEL("DISABLE"),
      .DYN_FPHASE_EN("DISABLE"),
      .CLKC0_FPHASE_RSTSEL(0),
      .CLKC1_FPHASE_RSTSEL(0),
      .CLKC2_FPHASE_RSTSEL(0),
      .CLKC3_FPHASE_RSTSEL(0),
      .CLKC0_DUTY50("ENABLE"),
      .CLKC1_DUTY50("ENABLE"),
      .CLKC2_DUTY50("ENABLE"),
      .CLKC3_DUTY50("ENABLE"),
      .INTPI(1),
      .SSC_ENABLE("DISABLE"),
      .SSC_MODE("DOWN"),
      .SSC_FREQ_DIV(0),
      .SSC_RNGE(0),
      .HIGH_SPEED_EN("DISABLE")
  )dr1_phy_pll_59fc11d80b0d_Inst
  (
      .clk4_en(1'b0),
      .clk4_out(),
      .clkb4_out(),
      .clk5_en(1'b0),
      .clk5_out(),
      .clkb5_out(),
      .clk6_en(1'b0),
      .clk6_out(),
      .clkb6_out(),
      .refclk(refclk),
      .ssc_en(1'b0),
      .ext_freq_mod_val(17'b00000000000000000),
      .ext_freq_mod_en(1'b0),
      .ext_freq_mod_clk(1'b0),
	  .clkc_rst(2'b00),
      .fbclk(clk0_out),
      .drp_rdata(),
      .drp_rdy(),
      .drp_err(),
      .drp_wdata(8'b00000000),
      .drp_addr(8'b00000000),
      .drp_wr(1'b0),
      .drp_rd(1'b0),
      .drp_sel(1'b0),
      .drp_rstn(1'b1),
      .drp_clk(1'b0),
      .cps_step(2'b00),
      .psclksel(3'b000),
      .psdone(),
      .psstep(1'b0),
      .psdown(1'b0),
      .psclk(1'b0),
      .pllpd(1'b0),
      .reset(1'b0),
      .wakeup(1'b0),
      .refclk_rst(1'b0),
      .clk0_en(1'b1),
      .clkb0_out(),
      .clk0_out(clk0_buf),
      .clk1_en(1'b1),
      .clkb1_out(),
      .clk1_out(clk1_out),
      .clk2_en(1'b1),
      .clkb2_out(),
      .clk2_out(clk2_out),
      .clk3_en(1'b1),
      .clkb3_out(),
      .clk3_out(clk3_out),
      .lock(lock)
  );
endmodule

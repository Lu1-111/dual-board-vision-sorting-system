`timescale 1ns/1ps
module conveyor_gp0_tb;
parameter EXTERNAL=0;
reg encoder_a=0,encoder_b=0;
reg clk=0;always #500 clk=~clk;reg rst_n=0;integer checks=0;reg [31:0] value;
reg [32:0] s_awaddr=0;
reg [2:0] s_awprot=0;
reg s_awvalid=0;
wire s_awready;
reg [31:0] s_wdata=0;
reg [3:0] s_wstrb=0;
reg s_wvalid=0;
wire s_wready;
wire [1:0] s_bresp;
wire s_bvalid;
reg s_bready=0;
reg [32:0] s_araddr=0;
reg [2:0] s_arprot=0;
reg s_arvalid=0;
wire s_arready;
wire [31:0] s_rdata;
wire [1:0] s_rresp;
wire s_rvalid;
reg s_rready=0;
wire [32:0] m_awaddr;
wire [2:0] m_awprot;
wire m_awvalid;
wire m_awready;
wire [31:0] m_wdata;
wire [3:0] m_wstrb;
wire m_wvalid;
wire m_wready;
wire [1:0] m_bresp;
wire m_bvalid;
wire m_bready;
wire [32:0] m_araddr;
wire [2:0] m_arprot;
wire m_arvalid;
wire m_arready;
wire [31:0] m_rdata;
wire [1:0] m_rresp;
wire m_rvalid;
wire m_rready;
conveyor_gp0 #(.CLK_HZ(1000000),.BENCH_ONLY(!EXTERNAL),.ENCODER_CONNECTED(EXTERNAL)) dut (.encoder_a(encoder_a),.encoder_b(encoder_b),.clk(clk),.rst_n(rst_n),.s_awaddr(s_awaddr),
.s_awprot(s_awprot),
.s_awvalid(s_awvalid),
.s_awready(s_awready),
.s_wdata(s_wdata),
.s_wstrb(s_wstrb),
.s_wvalid(s_wvalid),
.s_wready(s_wready),
.s_bresp(s_bresp),
.s_bvalid(s_bvalid),
.s_bready(s_bready),
.s_araddr(s_araddr),
.s_arprot(s_arprot),
.s_arvalid(s_arvalid),
.s_arready(s_arready),
.s_rdata(s_rdata),
.s_rresp(s_rresp),
.s_rvalid(s_rvalid),
.s_rready(s_rready),
.m_awaddr(m_awaddr),
.m_awprot(m_awprot),
.m_awvalid(m_awvalid),
.m_awready(m_awready),
.m_wdata(m_wdata),
.m_wstrb(m_wstrb),
.m_wvalid(m_wvalid),
.m_wready(m_wready),
.m_bresp(m_bresp),
.m_bvalid(m_bvalid),
.m_bready(m_bready),
.m_araddr(m_araddr),
.m_arprot(m_arprot),
.m_arvalid(m_arvalid),
.m_arready(m_arready),
.m_rdata(m_rdata),
.m_rresp(m_rresp),
.m_rvalid(m_rvalid),
.m_rready(m_rready));
wire legacy_en;conveyor_axi #(.CLK_HZ(1000000),.PWM_HZ(10000)) legacy (.aclk(clk),.aresetn(rst_n),.s_axi_awaddr(m_awaddr[11:0]),
.s_axi_awvalid(m_awvalid),
.s_axi_awready(m_awready),
.s_axi_wdata(m_wdata),
.s_axi_wstrb(m_wstrb),
.s_axi_wvalid(m_wvalid),
.s_axi_wready(m_wready),
.s_axi_bresp(m_bresp),
.s_axi_bvalid(m_bvalid),
.s_axi_bready(m_bready),
.s_axi_araddr(m_araddr[11:0]),
.s_axi_arvalid(m_arvalid),
.s_axi_arready(m_arready),
.s_axi_rdata(m_rdata),
.s_axi_rresp(m_rresp),
.s_axi_rvalid(m_rvalid),
.s_axi_rready(m_rready),.estop(1'b0),.driver_fault(1'b0),.encoder_a(1'b0),.encoder_b(1'b0),.motor_enable(legacy_en));

task check;input condition;input [799:0] text;begin checks=checks+1;if(!condition)begin $display("FAIL %0s",text);$fatal(1);end end endtask
task wr;input [32:0] address;input [31:0] data;input integer ad,wd,stall;input [1:0] response;begin
  fork
    begin repeat(ad)@(negedge clk);@(negedge clk);s_awaddr=address;s_awvalid=1;@(posedge clk);while(!s_awready)@(posedge clk);@(negedge clk);s_awvalid=0;end
    begin repeat(wd)@(negedge clk);@(negedge clk);s_wdata=data;s_wstrb=15;s_wvalid=1;@(posedge clk);while(!s_wready)@(posedge clk);@(negedge clk);s_wvalid=0;end
  join
  wait(s_bvalid);#1;check(s_bresp==response,"write response routed");
  repeat(stall)begin @(negedge clk);check(s_bvalid&&s_bresp==response,"write backpressure");end
  @(negedge clk);s_bready=1;@(posedge clk);@(negedge clk);s_bready=0;
end endtask
task rd;input [32:0] address;input integer stall;output [31:0] data;begin
  @(negedge clk);s_araddr=address;s_arvalid=1;@(posedge clk);while(!s_arready)@(posedge clk);@(negedge clk);s_arvalid=0;
  wait(s_rvalid);#1;data=s_rdata;check(s_rresp==0,"read response routed");
  repeat(stall)begin @(negedge clk);check(s_rvalid&&s_rdata==data,"read backpressure");end
  @(negedge clk);s_rready=1;@(posedge clk);@(negedge clk);s_rready=0;
end endtask
task command;input [32:0] base;input [31:0] duty,control;begin
  wr(base+8,duty,0,4,2,0);wr(base+12,control,4,0,2,0);wr(base+16,32'hc0de0001,0,0,0,0);
end endtask
always @(negedge clk)if(!EXTERNAL&&rst_n&&(dut.motor_inst.bts_rpwm||dut.motor_inst.bts_lpwm||dut.motor_inst.bts_r_en||dut.motor_inst.bts_l_en))$fatal(1,"BENCH OUTPUT ENABLED");
initial begin #1000000000;$fatal(1,"timeout");end
initial begin
  repeat(5)@(negedge clk);rst_n=1;repeat(5)@(negedge clk);
  rd(33'h080060000,5,value);check(value==32'h434e5601,"new slave ID");
  rd(33'h080060004,2,value);check(value==50,"new slave PWM period");
  rd(33'h08006002c,2,value);check(value==(EXTERNAL?16:1),"encoder/bench feature identity");
  rd(33'h080050004,2,value);check(value==100,"legacy slave preserved");
  rd(33'h080061004,2,value);check(value==100,"4 KiB boundary decoded");
  rd(33'h180060004,2,value);check(value==100,"33rd address bit decoded");
  command(33'h080060000,25,1);repeat(1100)@(negedge clk);
  rd(33'h080060014,2,value);check(value[0]==EXTERNAL,"only external profile can enable outputs");
  rd(33'h08006001c,2,value);check(value==25,"bench duty committed");
  command(33'h080050000,35,1);repeat(1100)@(negedge clk);check(legacy_en,"legacy enable unaffected");
  rd(33'h08005001c,2,value);check(value==35,"legacy duty committed");
  if(EXTERNAL) begin
    command(33'h080060000,0,0);
    encoder_b=1;repeat(8)@(negedge clk);
    encoder_a=1;repeat(8)@(negedge clk);
    encoder_b=0;repeat(8)@(negedge clk);
    encoder_a=0;repeat(8)@(negedge clk);
    rd(33'h080060024,2,value);check(value==4,"external AB forward through wrapper");
    encoder_a=1;repeat(8)@(negedge clk);
    encoder_b=1;repeat(8)@(negedge clk);
    encoder_a=0;repeat(8)@(negedge clk);
    encoder_b=0;repeat(8)@(negedge clk);
    rd(33'h080060024,2,value);check(value==0,"external AB reverse through wrapper");
    encoder_a=1;encoder_b=1;repeat(8)@(negedge clk);
    rd(33'h080060028,2,value);check(value==1,"external invalid AB transition detected");
    rd(33'h080060024,2,value);check(value==0,"invalid AB does not invent distance");
    wr(33'h080060030,0,0,0,0,2);
  end else begin
  wr(33'h080060030,1,0,3,1,0);wr(33'h080060030,3,3,0,1,0);wr(33'h080060030,2,0,2,1,0);wr(33'h080060030,0,2,0,1,0);
  rd(33'h080060024,2,value);check(value==4,"bench AB forward routed");
  wr(33'h080060030,2,0,0,0,0);wr(33'h080060030,3,0,0,0,0);wr(33'h080060030,1,0,0,0,0);wr(33'h080060030,0,0,0,0,0);
  rd(33'h080060024,2,value);check(value==0,"bench AB reverse routed");
  end
  wr(33'h080050030,0,0,0,2,2);
  fork
    wr(33'h080060008,12,20,0,2,0);
    begin rd(33'h080050004,8,value);check(value==100,"read concurrent with delayed write");end
  join
  command(33'h080060000,12,1);repeat(250100)@(negedge clk);
  rd(33'h080060014,2,value);check(value[8],"watchdog survives output lock");
  command(33'h080060000,0,4);rd(33'h080060014,2,value);check(value[10:8]==0,"explicit clear works through router");
  @(negedge clk);s_wdata=99;s_wvalid=1;@(posedge clk);@(negedge clk);s_wvalid=0;rst_n=0;
  repeat(4)@(negedge clk);rst_n=1;repeat(4)@(negedge clk);
  rd(33'h080060008,0,value);check(value==0,"reset flushes buffered data");
  if(EXTERNAL) begin
    rd(33'h080060024,0,value);check(value==0,"reset with AB=11 does not invent a count");
    rd(33'h080060028,0,value);check(value==0,"reset with AB=11 does not invent an error");
  end
  $display("PASS conveyor_gp0: %0d checks; external_encoder=%0d",checks,EXTERNAL);$finish;
end
endmodule

`timescale 1ns/1ps
// Routes a new 4 KiB aperture while preserving all existing GP0 peripherals.
// Independent AW/W capture, one write and one read outstanding, no combinational
// AW->W route assumption. Both slaves are AXI4-Lite after the vendor converter.
module axi_lite_route #(
    parameter [32:0] BASE = 33'h080060000
)(
    input wire clk,rst_n,
    input wire [32:0] s_awaddr,input wire [2:0] s_awprot,input wire s_awvalid,output wire s_awready,
    input wire [31:0] s_wdata,input wire [3:0] s_wstrb,input wire s_wvalid,output wire s_wready,
    output wire [1:0] s_bresp,output wire s_bvalid,input wire s_bready,
    input wire [32:0] s_araddr,input wire [2:0] s_arprot,input wire s_arvalid,output wire s_arready,
    output wire [31:0] s_rdata,output wire [1:0] s_rresp,output wire s_rvalid,input wire s_rready,
    output wire [32:0] m_awaddr,output wire [2:0] m_awprot,output wire m_awvalid,input wire m_awready,
    output wire [31:0] m_wdata,output wire [3:0] m_wstrb,output wire m_wvalid,input wire m_wready,
    input wire [1:0] m_bresp,input wire m_bvalid,output wire m_bready,
    output wire [32:0] m_araddr,output wire [2:0] m_arprot,output wire m_arvalid,input wire m_arready,
    input wire [31:0] m_rdata,input wire [1:0] m_rresp,input wire m_rvalid,output wire m_rready,
    output wire [11:0] c_awaddr,output wire c_awvalid,input wire c_awready,
    output wire [31:0] c_wdata,output wire [3:0] c_wstrb,output wire c_wvalid,input wire c_wready,
    input wire [1:0] c_bresp,input wire c_bvalid,output wire c_bready,
    output wire [11:0] c_araddr,output wire c_arvalid,input wire c_arready,
    input wire [31:0] c_rdata,input wire [1:0] c_rresp,input wire c_rvalid,output wire c_rready
);
    localparam IDLE=2'd0,SEND=2'd1,REPLY=2'd2;
    reg [1:0] ws,rs;
    reg aw_full,w_full,aw_sent,w_sent,write_conveyor,read_conveyor;
    reg [32:0] awaddr,araddr;
    reg [2:0] awprot,arprot;
    reg [31:0] wdata;reg [3:0] wstrb;
    wire aw_done=aw_sent||(write_conveyor?c_awready:m_awready);
    wire w_done=w_sent||(write_conveyor?c_wready:m_wready);
    assign s_awready=(ws==IDLE)&&!aw_full;
    assign s_wready=(ws==IDLE)&&!w_full;
    assign m_awaddr=awaddr;assign m_awprot=awprot;assign c_awaddr=awaddr[11:0];
    assign m_wdata=wdata;assign c_wdata=wdata;assign m_wstrb=wstrb;assign c_wstrb=wstrb;
    assign m_awvalid=(ws==SEND)&&!aw_sent&&!write_conveyor;
    assign c_awvalid=(ws==SEND)&&!aw_sent&&write_conveyor;
    assign m_wvalid=(ws==SEND)&&!w_sent&&!write_conveyor;
    assign c_wvalid=(ws==SEND)&&!w_sent&&write_conveyor;
    assign s_bvalid=(ws==REPLY)&&(write_conveyor?c_bvalid:m_bvalid);
    assign s_bresp=write_conveyor?c_bresp:m_bresp;
    assign m_bready=(ws==REPLY)&&!write_conveyor&&s_bready;
    assign c_bready=(ws==REPLY)&&write_conveyor&&s_bready;
    assign s_arready=(rs==IDLE);
    assign m_araddr=araddr;assign m_arprot=arprot;assign c_araddr=araddr[11:0];
    assign m_arvalid=(rs==SEND)&&!read_conveyor;
    assign c_arvalid=(rs==SEND)&&read_conveyor;
    assign s_rvalid=(rs==REPLY)&&(read_conveyor?c_rvalid:m_rvalid);
    assign s_rdata=read_conveyor?c_rdata:m_rdata;assign s_rresp=read_conveyor?c_rresp:m_rresp;
    assign m_rready=(rs==REPLY)&&!read_conveyor&&s_rready;
    assign c_rready=(rs==REPLY)&&read_conveyor&&s_rready;
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin
            ws<=IDLE;rs<=IDLE;aw_full<=0;w_full<=0;aw_sent<=0;w_sent<=0;
            write_conveyor<=0;read_conveyor<=0;awaddr<=0;araddr<=0;awprot<=0;arprot<=0;wdata<=0;wstrb<=0;
        end else begin
            if(s_awvalid&&s_awready)begin aw_full<=1;awaddr<=s_awaddr;awprot<=s_awprot;write_conveyor<=(s_awaddr[32:12]==BASE[32:12]);end
            if(s_wvalid&&s_wready)begin w_full<=1;wdata<=s_wdata;wstrb<=s_wstrb;end
            if(ws==IDLE&&aw_full&&w_full)begin ws<=SEND;aw_sent<=0;w_sent<=0;end
            if(ws==SEND)begin
                if(write_conveyor?c_awready:m_awready)aw_sent<=1;
                if(write_conveyor?c_wready:m_wready)w_sent<=1;
                if(aw_done&&w_done)ws<=REPLY;
            end
            if(s_bvalid&&s_bready)begin ws<=IDLE;aw_full<=0;w_full<=0;end
            if(s_arvalid&&s_arready)begin
                araddr<=s_araddr;arprot<=s_arprot;read_conveyor<=(s_araddr[32:12]==BASE[32:12]);rs<=SEND;
            end
            if(rs==SEND&&(read_conveyor?c_arready:m_arready))rs<=REPLY;
            if(s_rvalid&&s_rready)rs<=IDLE;
        end
    end
endmodule

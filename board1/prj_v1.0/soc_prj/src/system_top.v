

module system_top(
    input wire encoder_b,
    input wire encoder_a,
    output wire txs_oe,
    output wire bts_lpwm,
    output wire bts_rpwm,
    output wire bts_l_en,
    output wire bts_r_en,
    // HDMI interface 
    output [2:0] O_hdmi_tx_p,
    output  O_hdmi_clk_p,
  
    // PS I2C0 EMIO
    inout          SCL0,
    inout          SDA0,

    // Board SN65HVD232D: CAN0 over PL IO (F20 TX, F19 RX).
    output wire    CAN0_TXD,
    input wire     CAN0_RXD,


    // Key input
//    inout    [3:0] key,
    // LED output
    output   [2:0] led,
    // Six servo signal outputs on the CEP connector
    output   [5:0] servo_pwm,
    
	input  wire		    I_clk_hs_p  ,
	input  wire	[3:0]	I_data_hs_p , 
	input  wire	[3:0]	I_data_lp_p ,
	input  wire	[3:0]	I_data_lp_n ,

    output wire         O_imx334_clk_24m   ,
    output              O_imax334_rst_n
 );
	
    // The generated external HP1 AXI4 port and Linux CMA allocation are 32-bit.
    parameter  AXI_ADDR_WIDTH = 32 ;
	parameter  AXI_DATA_WIDTH = 256  ;
    parameter    X_SIZE = 1920 ;
    parameter    Y_SIZE = 1080 ;


    
    /**********************************************************
            Signals Definition Connected to PS
    **********************************************************/
 

    wire       [31:0] slave_hp0_axi_awaddr ;
    wire        [1:0] slave_hp0_axi_awburst ;
    wire        [3:0] slave_hp0_axi_awcache ;
    wire        [4:0] slave_hp0_axi_awid ;
    wire        [7:0] slave_hp0_axi_awlen ;
    wire              slave_hp0_axi_awlock ;
    wire        [2:0] slave_hp0_axi_awprot ;
    wire        [3:0] slave_hp0_axi_awqos ;
    wire        [2:0] slave_hp0_axi_awsize ;
    wire              slave_hp0_axi_awvalid ;
    wire              slave_hp0_axi_awready;
    
    wire              slave_hp0_axi_bready ;
    wire        [4:0] slave_hp0_axi_bid ;
    wire        [1:0] slave_hp0_axi_bresp ;
    wire              slave_hp0_axi_bvalid ;
    
    wire       [63:0] slave_hp0_axi_wdata ;
    wire              slave_hp0_axi_wlast ;
    wire        [7:0] slave_hp0_axi_wstrb ;
    wire              slave_hp0_axi_wvalid ;
    wire              slave_hp0_axi_wready;

    wire              f2p_irq ;
    wire       [63:0] pl_gpio_in ;
    wire              i2c0_scl_in ;
    wire              i2c0_sda_in ;
    wire              i2c1_scl_in ;
    wire              i2c1_sda_in ;


    wire              pll_clk_150 ;//synthesis keep;
    wire              pll_clk_150_lock ;
    wire              pll_clk_150_aresetn ;
    
    wire       [63:0] pl_gpio_oe ;
    wire       [63:0] pl_gpio_out ;
	
    wire    [7:0]   mipi_diag_level_bursts;
   
    wire lock_pl ;
    
    
wire    [31:0]                            camera_base_addr; 
wire                                      camera_start; 
wire                                      camera_clear; 


wire p2f_clk0_50m;
wire [63:0] ps_pl_gpio_out;
wire [5:0] servo_status;
assign pl_gpio_in = {8'd0, servo_status, 50'd0};
(* async_reg = "true" *) reg [1:0] servo_reset_pipe; /* fehdl keep="true" */

// Release the servo reset on the same 50 MHz clock that drives its counters.
always @(posedge p2f_clk0_50m or negedge pll_clk_150_aresetn) begin
    if (!pll_clk_150_aresetn)
        servo_reset_pipe <= 2'b00;
    else
        servo_reset_pipe <= {servo_reset_pipe[0], 1'b1};
end

arm_servo_pwm #(
    .CLK_HZ(50000000),
    .PWM_HZ(50)
) arm_servo_pwm_inst (
    .clk(p2f_clk0_50m),
    .rst_n(servo_reset_pipe[1]),
    .ps_gpio(ps_pl_gpio_out),
    .servo_pwm(servo_pwm),
    .status(servo_status)
);

    /**********************************************************
                        PS 
    **********************************************************/
    design_0  design_0_inst (
        .encoder_b(encoder_b),
        .encoder_a(encoder_a),
        .txs_oe(txs_oe),
        .bts_lpwm(bts_lpwm),
        .bts_rpwm(bts_rpwm),
        .bts_l_en(bts_l_en),
        .bts_r_en(bts_r_en),
        .can0_tx_out(CAN0_TXD),
        .can0_rx_in(CAN0_RXD),
        // hp0 aw 
        .hp1_w_us1_awaddr (slave_hp0_axi_awaddr ),
        .hp1_w_us1_awburst (slave_hp0_axi_awburst ),
        .hp1_w_us1_awcache (slave_hp0_axi_awcache ),
        .hp1_w_us1_awid (slave_hp0_axi_awid ),
        .hp1_w_us1_awlen (slave_hp0_axi_awlen ),
        .hp1_w_us1_awlock (slave_hp0_axi_awlock ),
        .hp1_w_us1_awprot (slave_hp0_axi_awprot ),
        .hp1_w_us1_awqos (slave_hp0_axi_awqos ),
        .hp1_w_us1_awsize (slave_hp0_axi_awsize ),
        .hp1_w_us1_awvalid (slave_hp0_axi_awvalid ),
        .hp1_w_us1_awready (slave_hp0_axi_awready ),
        // hp0 w
        .hp1_w_us1_wdata (slave_hp0_axi_wdata ),
        .hp1_w_us1_wlast (slave_hp0_axi_wlast ),
        .hp1_w_us1_wstrb (slave_hp0_axi_wstrb ),
        .hp1_w_us1_wvalid (slave_hp0_axi_wvalid ),
        .hp1_w_us1_wready (slave_hp0_axi_wready ),
        // hp0 b
        .hp1_w_us1_bid (slave_hp0_axi_bid ),
        .hp1_w_us1_bresp (slave_hp0_axi_bresp ),
        .hp1_w_us1_bvalid (slave_hp0_axi_bvalid ),
        .hp1_w_us1_bready (slave_hp0_axi_bready ),
        // This external HP1 master is write-only; keep its read request idle.
        .hp1_w_us1_araddr(32'd0), .hp1_w_us1_arburst(2'b01),
        .hp1_w_us1_arcache(4'd0), .hp1_w_us1_arid(5'd0),
        .hp1_w_us1_arlen(8'd0), .hp1_w_us1_arlock(1'b0),
        .hp1_w_us1_arprot(3'd0), .hp1_w_us1_arqos(4'd0),
        .hp1_w_us1_arsize(3'd3), .hp1_w_us1_arvalid(1'b0),
        .hp1_w_us1_arready(), .hp1_w_us1_rdata(), .hp1_w_us1_rid(),
        .hp1_w_us1_rlast(), .hp1_w_us1_rresp(), .hp1_w_us1_rvalid(),
        .hp1_w_us1_rready(1'b0),
        
        .f2p_irq (f2p_irq ),
        
        
        // iic0 and iic1
        .mipi_iic_i2c0_scl_io(SCL0),
        .mipi_iic_i2c0_sda_io(SDA0),
     
    
        // pl gpio
    
        
        // hdmi
        
        .O_hdmi_tx_p(O_hdmi_tx_p),
        .O_hdmi_clk_p(O_hdmi_clk_p),


        .clk_150m(pll_clk_150), 
        .p2f_clk0_50m(p2f_clk0_50m),
        .rstn_150m(pll_clk_150_aresetn), 
 
        
        // pwm output
        .pwm_out(led[0]),
        .pl_gpio_out(ps_pl_gpio_out),
        .pl_gpio_in(pl_gpio_in),
        .vtg_lock                 (led[1] ), 
        .axis_to_video_lock       (led[2]),
        
        // 
        .camera_base_addr(camera_base_addr), 
        .camera_start(camera_start), 
        .camera_clear(camera_clear)

    );


    // PLIO IO
//    assign pl_gpio_in[0] = key[0];
//    assign pl_gpio_in[1] = key[1];
//    assign pl_gpio_in[2] = key[2];
//    assign pl_gpio_in[3] = key[3];
//    assign key[0] = pl_gpio_oe[0] ? pl_gpio_out[0] : 1'bz;
//    assign key[1] = pl_gpio_oe[1] ? pl_gpio_out[1] : 1'bz;
//    assign key[2] = pl_gpio_oe[2] ? pl_gpio_out[2] : 1'bz;
//    assign key[3] = pl_gpio_oe[3] ? pl_gpio_out[3] : 1'bz;

    // SMA connector


    /**********************************************************
            ADI VDMA ( Connected to MIPI )
    **********************************************************/
    wire	[95:0]	m_axis_tdata		;//synthesis keep;
    wire			m_axis_tuser		;//synthesis keep;
    wire			m_axis_tlast		;//synthesis keep;
    wire			m_axis_tvalid		;//synthesis keep;
    wire			m_axis_tready		;//synthesis keep;
    //wire    [7:0]   mipi_diag_level_bursts;
    /**********************************************************
            MIPI INPUT
    **********************************************************/
	
	wire ud_wclk;//synthesis keep
	wire ud_wvs;//synthesis keep
	wire ud_wde;//synthesis keep
	//wire [256-1:0] ud_wdata;//synthesis keep
    wire [128-1:0] ud_wdata;
	
	wire        S_vi_256b_frame_start;   
    wire        S_vi_256b_valid;        
    wire[255:0] S_vi_256b_data;  
    
	
	
    wire            S_hs_rx_clk    ;
    wire            S_pll_lock     ;
    wire            S_axi_user     ;
    wire            S_axi_valid    ;
    wire            S_axi_last     ;
    wire   [127:0]  S_axi_data     ;
	
	wire   [ 127:0] S_ISP_O_tdata ;
    wire         	S_ISP_O_tlast ;
    wire  			S_ISP_O_tuser ;
    wire         	S_ISP_O_tvalid;
	
	assign ud_wclk   = S_hs_rx_clk;
	assign ud_wvs    = S_ISP_O_tuser ; 
	assign ud_wde    = S_ISP_O_tvalid ; 
	assign ud_wdata  = S_ISP_O_tdata; //{{8'd0, O_axi_data[8-1:0],O_axi_data[16-1:8],O_axi_data[23:16]}, {8'd0, O_axi_data[32-1:24],O_axi_data[40-1:32],O_axi_data[47:40]}, {8'd0, O_axi_data[56-1:48],O_axi_data[64-1:56],O_axi_data[71:64]}, {8'd0, O_axi_data[80-1:72],O_axi_data[88-1:80],O_axi_data[95:88]}} ;  //{{8'd0, O_axi_data[23:0]}, {8'd0, O_axi_data[47:24]}, {8'd0, O_axi_data[71:48]}, {8'd0, O_axi_data[95:72]}} ; //S_vi_256b_data ; //{S_vi_256b_data[96*1-1:96*0],S_vi_256b_data[96*2-1:96*1],S_vi_256b_data[96*3-1:96*2]，S_vi_256b_data[96*4-1:96*3]}; //synthesis keep
	
    wire    [3:0]   S_lane_error    ;

    reg [7:0] cnt_error = 0;
    always@(posedge S_hs_rx_clk)begin
        if(S_lane_error != 4'b0000)
            cnt_error <= cnt_error + 1;
    end

//    assign led[7:3] = cnt_error[4:0];
	assign O_imax334_rst_n = 1'b1;
	wire S_hs_rstn ;
	
  //isp_top u_isp_top (
	//.axi4s_video_aclk(S_hs_rx_clk),
	//.I_rst_n         (S_pll_lock),
	//.I_tlast         (S_axi_last ),
	//.I_tuser         (S_axi_user ),
	//.I_tdata         (S_axi_data ),
	//.I_tvalid        (S_axi_valid),
	//.O_tdata         (S_ISP_O_tdata ),
	//.O_tlast         (S_ISP_O_tlast ),
	//.O_tuser         (S_ISP_O_tuser ),
	//.O_tvalid        (S_ISP_O_tvalid)
  //);
	
    design_top_wrapper  design_top_wrapper_u
    (
    .I_sys_clk_156m     (p2f_clk0_50m     ),
	//.I_clk_hs_p         (I_clk_hs_p         ),
    //.I_clk_lp_p			(I_clk_lp_p			),
	//.I_data_hs_p        (I_data_hs_p        ), 
    //.I_data_lp_p        (I_data_lp_p        ),
    //.I_data_lp_n        (I_data_lp_n        ), 
    //.I_csi_lp_ck_dir	(I_csi_lp_ck_dir	),    
	//.I_csi_lp_d0_dir    (I_csi_lp_d0_dir    ),
    //.I_csi_lp_d1_dir    (I_csi_lp_d1_dir    ),
	//.I_csi_lp_d2_dir    (I_csi_lp_d2_dir    ),
    //.I_csi_lp_d3_dir    (I_csi_lp_d3_dir    ),    	    
	.O_imx334_clk_24m   (O_imx334_clk_24m   ),
	.I_clk_hs_p     	(I_clk_hs_p     ),
	.I_data_hs_p    	(I_data_hs_p    ),
	.I_data_lp_p    	(I_data_lp_p    ),
	.I_data_lp_n    	(I_data_lp_n    ),

    .S_lane_error       (S_lane_error   ),
    .S_hs_rx_clk        (S_hs_rx_clk    ),
	.S_pll_lock			(S_pll_lock		),
    .O_ISP_tdata        (S_ISP_O_tdata     ),
    .O_ISP_tlast        (S_ISP_O_tlast     ),
    .O_ISP_tuser        (S_ISP_O_tuser     ),
    .O_ISP_tvalid       (S_ISP_O_tvalid    )
    );
	
	/*data_96bit_to_256bit u_data_96bit_to_256bit(
	.I_clk                ( S_hs_rx_clk           ),
	.I_rst_n              ( S_hs_rstn                ),
	
	.I_96b_frame_start    ( O_axi_user     ),
	.I_96b_valid          ( O_axi_valid           ),
	.I_96b_data           ( {O_axi_data[24*1-1:24*0],O_axi_data[24*2-1:24*1],O_axi_data[24*3-1:24*2],O_axi_data[24*4-1:24*3]}            ),
	
	.O_256b_frame_start  ( S_vi_256b_frame_start ),
	.O_256b_valid         ( S_vi_256b_valid       ),
	.O_256b_data          ( S_vi_256b_data        )
	);*/
	
	wire [AXI_ADDR_WIDTH-1:  0]      fdma_waddr;   
	wire                             fdma_wareq;   
	wire [15: 0]                     fdma_wsize;                                 
	wire                             fdma_wbusy;   		
	wire [AXI_DATA_WIDTH/4-1 : 0]      fdma_wdata; 
	wire                             fdma_wvalid;  
	wire                             fdma_wready;  
	
	wire [7:0] wbuf_sync;

	
	
	uidbuf_write# (
	.VIDEO_ENABLE(1'b1),  
	.ENABLE_WRITE(1'b1), 
	.ENABLE_READ(1'b0), 
	.AXI_DATA_WIDTH(AXI_DATA_WIDTH/4),
	.AXI_ADDR_WIDTH(AXI_ADDR_WIDTH),
	
	.W_BUFDEPTH(1024*4),
	.W_DATAWIDTH(128),//(256),//96->256
	//.W_BASEADDR(BASEADDR),
	.W_DSIZEBITS(25),
	/*.W_XSIZE(180),
	.W_XSTRIDE(180),
	.W_YSIZE(1080),*/
	.W_XDIV(1),
	//.W_BUFSIZE(BUFSIZE),
	
	.W_XSIZE(X_SIZE/(128/32)),//(120),
	.W_XSTRIDE(X_SIZE/(128/32)),//(120),
	.W_YSIZE(Y_SIZE)
	
	)
	uidbuf_write_u0
	(
	.W_BASEADDR(camera_base_addr),
	//.W_DSIZEBITS(DSIZEBITS),
	.W_BUFSIZE(16'd6),
	
	.I_ui_clk(pll_clk_150),
	.I_ui_rstn(pll_clk_150_aresetn),
	
	.I_W_FS(ud_wvs),
	.I_W_clk(ud_wclk),
	.I_W_wren(ud_wde),
	.I_W_data(ud_wdata), 
	.O_W_sync_cnt(wbuf_sync),
	.I_W_buf(wbuf_sync),
	.O_W_full(),
	
		
	.O_fdma_waddr(fdma_waddr)  ,
	.O_fdma_wareq(fdma_wareq)  ,
	.O_fdma_wsize(fdma_wsize)  ,                                     
	.I_fdma_wbusy(fdma_wbusy)  ,			
	.O_fdma_wdata(fdma_wdata)	 ,
	.I_fdma_wvalid(fdma_wvalid),
	.O_fdma_wready(fdma_wready),
	.O_fmda_wbuf(),	
	.O_fdma_wirq  (fdma_wirq),	
	
	.camera_start       ( camera_start ),
	.camera_done        ( f2p_irq       ),
	.camera_clear_intr  ( camera_clear)
	); 
	
	uiFDMA#
	(
	.M_AXI_ID_WIDTH(5)           ,// matches generated external HP1 AXI4 port
	.M_AXI_ADDR_WIDTH(AXI_ADDR_WIDTH)		,//内存地址位宽
	.M_AXI_DATA_WIDTH(AXI_DATA_WIDTH/4)		,//AXI总线的数据位宽
	.M_AXI_MAX_BURST_LEN (16)                //AXI总线的burst 大小，对于AXI4，支持任意长度，对于AXI3以下最大16
	)
	uiFDMA_inst
	(
	.I_fdma_waddr(fdma_waddr)          ,//FDMA写通道地址
	.I_fdma_wareq(fdma_wareq)          ,//FDMA写通道请求
	.I_fdma_wsize(fdma_wsize)          ,//FDMA写通道一次FDMA的传输大小                                            
	.O_fdma_wbusy(fdma_wbusy)          ,//FDMA处于BUSY状态，AXI总线正在写操作  
					
	.I_fdma_wdata(fdma_wdata)		    ,//FDMA写数据
	.O_fdma_wvalid(fdma_wvalid)        ,//FDMA 写有效
	.I_fdma_wready(fdma_wready)		       ,//FDMA写准备好，用户可以写数据
	
	.I_fdma_raddr({AXI_ADDR_WIDTH{1'b0}})          ,// FDMA读通道地址
	.I_fdma_rareq(1'b0)          ,// FDMA读通道请求
	.I_fdma_rsize(16'd0)          ,// FDMA读通道一次FDMA的传输大小                                      
	.O_fdma_rbusy()          ,// FDMA处于BUSY状态，AXI总线正在读操作 
					
	.O_fdma_rdata()		    ,// FDMA读数据
	.O_fdma_rvalid()        ,// FDMA 读有效
	.I_fdma_rready(1'b0)		    ,// FDMA读准备好，用户可以读数据
	
	//以下为AXI总线信号		
	.M_AXI_ACLK                             (pll_clk_150),
	.M_AXI_ARESETN                          (pll_clk_150_aresetn),
	// Master Interface Write Address Ports
	
	.M_AXI_AWID                             (slave_hp0_axi_awid),
	.M_AXI_AWADDR                           (slave_hp0_axi_awaddr),
	.M_AXI_AWLEN(slave_hp0_axi_awlen),
	.M_AXI_AWSIZE(slave_hp0_axi_awsize),
	.M_AXI_AWBURST                          (slave_hp0_axi_awburst),
	.M_AXI_AWLOCK                           (slave_hp0_axi_awlock),
	.M_AXI_AWCACHE                          (slave_hp0_axi_awcache),
	.M_AXI_AWPROT                           (slave_hp0_axi_awprot),
	.M_AXI_AWQOS                            (slave_hp0_axi_awqos),
	.M_AXI_AWVALID                          (slave_hp0_axi_awvalid),
	.M_AXI_AWREADY                          (slave_hp0_axi_awready),
	// Master Interface Write Data Ports
	.M_AXI_WDATA                            (slave_hp0_axi_wdata),
	.M_AXI_WSTRB                            (slave_hp0_axi_wstrb),
	.M_AXI_WLAST                            (slave_hp0_axi_wlast),
	.M_AXI_WVALID                           (slave_hp0_axi_wvalid),
	.M_AXI_WREADY                           (slave_hp0_axi_wready),
	
	// Master Interface Write Response Ports
	.M_AXI_BID                              (slave_hp0_axi_bid),
	.M_AXI_BRESP                            (slave_hp0_axi_bresp),
	.M_AXI_BVALID                           (slave_hp0_axi_bvalid),
	.M_AXI_BREADY(slave_hp0_axi_bready),
    .M_AXI_WID(),
    .M_AXI_ARID(), .M_AXI_ARADDR(), .M_AXI_ARLEN(), .M_AXI_ARSIZE(),
    .M_AXI_ARBURST(), .M_AXI_ARLOCK(), .M_AXI_ARCACHE(), .M_AXI_ARPROT(),
    .M_AXI_ARQOS(), .M_AXI_ARVALID(), .M_AXI_ARREADY(1'b0),
    .M_AXI_RID(5'd0), .M_AXI_RDATA({AXI_DATA_WIDTH/4{1'b0}}),
    .M_AXI_RRESP(2'b00), .M_AXI_RLAST(1'b0), .M_AXI_RVALID(1'b0),
    .M_AXI_RREADY()
	
	
	
	);				 
 
 
endmodule

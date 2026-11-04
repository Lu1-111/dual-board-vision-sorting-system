
`timescale 1ns / 1ns

module uidbuf_write#(
parameter  integer                   VIDEO_ENABLE   = 1,//使能视频帧支持功能 
parameter  integer                   ENABLE_WRITE   = 1,//使能写通道
parameter  integer                   ENABLE_READ    = 1,//使能读通道

parameter  integer                   AXI_DATA_WIDTH = 128,//AXI总线数据位宽
parameter  integer                   AXI_ADDR_WIDTH = 32, //AXI总线地址位宽

parameter  integer                   W_BUFDEPTH     = 2048, //写通道AXI设置FIFO缓存大小
parameter  integer                   W_DATAWIDTH    = 32,  //写通道AXI设置数据位宽大小
//parameter  [AXI_ADDR_WIDTH -1'b1: 0] W_BASEADDR     = 0, //写通道设置内存起始地址
parameter  integer                   W_DSIZEBITS    = 24, //写通道设置缓存数据的增量地址大小，用于FDMA DBUF 计算帧缓存起始地址
parameter  integer                   W_XSIZE        = 1920, //写通道设置X方向的数据大小，代表了每次FDMA 传输的数据长度
parameter  integer                   W_XSTRIDE      = 1920, //写通道设置X方向的Stride值，主要用于图形缓存应用
parameter  integer                   W_YSIZE        = 1080, //写通道设置Y方向值，代表了进行了多少次XSIZE传输
parameter  integer                   W_XDIV         = 2 //写通道对X方向数据拆分为XDIV次传输，减少FIFO的使用
//parameter  integer                   W_BUFSIZE      = 3 //写通道设置帧缓存大小，目前最大支持128帧，可以修改参数支持更缓存数

)
(
//input wire [15:0] W_DSIZEBITS,
input wire [AXI_ADDR_WIDTH-1:0] W_BASEADDR,     
input wire [15:0] W_BUFSIZE,
input wire                                  I_ui_clk, //和FDMA AXI总线时钟一致
input wire                                  I_ui_rstn, //和FDMA AXI复位一致
//sensor input -W_FIFO--------------
input wire                                  I_W_clk, //用户写数据接口时钟 
input wire                                  I_W_FS, //用户写数据接口同步信号，对于非视频帧一般设置为1
input wire                                  I_W_wren, //用户写数据使能
input wire     [W_DATAWIDTH-1'b1 : 0]       I_W_data, //用户写数据
output reg     [7   :0]                     O_W_sync_cnt =0, //写通道BUF帧同步输出
input  wire    [7   :0]                     I_W_buf, // 写通道BUF帧同步输入
output wire                                 O_W_full,

//----------fdma signals write-------       
output wire    [AXI_ADDR_WIDTH-1'b1: 0]     O_fdma_waddr, //FDMA写通道地址
output wire                                 O_fdma_wareq, //FDMA写通道请求
output wire    [15  :0]                     O_fdma_wsize, //FDMA写通道一次FDMA的传输大小                                     
input  wire                                 I_fdma_wbusy, //FDMA处于BUSY状态，AXI总线正在写操作   
output wire    [AXI_DATA_WIDTH-1'b1:0]      O_fdma_wdata, //FDMA写数据
input  wire                                 I_fdma_wvalid, //FDMA 写有效
output wire                                 O_fdma_wready, //FDMA写准备好，用户可以写数据
output reg     [7   :0]                     O_fmda_wbuf =0, //FDMA的写帧缓存号输出
output wire                                 O_fdma_wirq, //FDMA一次写完成的数据传输完成后，产生中断。   

input  wire                                 camera_start,//synthesis keep
output wire                                 camera_done,//synthesis keep
input  wire                                 camera_clear_intr//synthesis keep

);    

// Ceiling log2: a power-of-two depth must not allocate twice the RAM.
function integer clog2;
  input integer value;
  begin 
    value = value-1;
    for (clog2=0; value>0; clog2=clog2+1)
      value = value>>1;
    end 
  endfunction


//FDMA读写状态机的状态值，一般4个状态值即可 
localparam S_IDLE  =  3'd0;  
localparam S_RST   =  3'd1;  
localparam S_DATA1 =  3'd2;   
localparam S_DATA2 =  3'd3; 
localparam S_FRAME_DONE =  3'd4; 
localparam S_DRAIN = 3'd5;
localparam S_RETRY = 3'd6;

// 通过设置通道使能，可以优化代码的利用率
generate  if(ENABLE_WRITE == 1)begin : FDMA_WRITE_ENABLE

localparam WFIFO_DEPTH = W_BUFDEPTH; //写通道FIFO深度
localparam W_WR_DATA_COUNT_WIDTH = clog2(WFIFO_DEPTH); //计算FIFO的写通道位宽
localparam W_RD_DATA_COUNT_WIDTH = clog2(WFIFO_DEPTH*W_DATAWIDTH/AXI_DATA_WIDTH);//clog2(WFIFO_DEPTH/(AXI_DATA_WIDTH/W_DATAWIDTH))+1;

wire [15:0] WYBUF_SIZE           = (W_BUFSIZE - 1'b1); //写通道需要完成多少次XSIZE操作
localparam WY_BURST_TIMES       = (W_YSIZE*W_XDIV); //写通道需要完成的FDMA burst 操作次数，XDIV用于把XSIZE分解多次传输
localparam FDMA_WX_BURST        = (W_XSIZE*W_DATAWIDTH/AXI_DATA_WIDTH)/W_XDIV; //FDMA BURST 一次的大小
localparam WX_BURST_ADDR_INC    = (W_XSIZE*(W_DATAWIDTH/8))/W_XDIV; //FDMA每次burst之后的地址增加
localparam WX_LAST_ADDR_INC     = (W_XSTRIDE-W_XSIZE)*(W_DATAWIDTH/8) + WX_BURST_ADDR_INC; //根据stride值计算出来最后一次地址


assign                                  O_fdma_wready = 1'b1;
reg                                     O_fdma_wareq_r= 1'b0;
reg                                     W_FIFO_Rst=0; 
wire                                    W_FS;
reg [2 :0]                              W_MS=0; //synthesis keep
reg [W_DSIZEBITS-1'b1:0]                W_addr=0; 
reg [15:0]                              W_bcnt=0; //synthesis keep
wire[W_RD_DATA_COUNT_WIDTH-1'b1 :0]     W_rcnt;//synthesis keep
reg                                     W_REQ=0; 
reg [5 :0]                              wirq_dly_cnt =0;
reg [3 :0]                              wdiv_cnt =0;
reg [7 :0]                              wrst_cnt =0;
reg [7 :0]                              O_fmda_wbufn;

// Remember an actual rejected pixel word, not merely a full FIFO. The
// source level persists until FIFO reset, so even a one-cycle overflow
// crosses safely. Reset release is shared with the FIFO write domain.
wire W_wr_busy;
reg overflow_latched = 1'b0; // synthesis keep
(* async_reg = "true" *) reg overflow_meta = 1'b0; // synthesis keep
(* async_reg = "true" *) reg overflow_sync = 1'b0; // synthesis keep
always @(posedge I_W_clk) begin
    if (W_wr_busy)
        overflow_latched <= 1'b0;
    else if (I_W_wren && O_W_full)
        overflow_latched <= 1'b1;
end
always @(posedge I_ui_clk) begin
    if (!I_ui_rstn) begin
        overflow_meta <= 1'b0;
        overflow_sync <= 1'b0;
    end else begin
        overflow_meta <= overflow_latched;
        overflow_sync <= overflow_meta;
    end
end


assign O_fdma_wsize = FDMA_WX_BURST;
assign O_fdma_wirq = (wirq_dly_cnt>0); 

assign O_fdma_waddr = W_BASEADDR + W_addr ; //W_BASEADDR + {O_fmda_wbufn,W_addr};//由于FPGA逻辑做乘法比较复杂，因此通过设置高位地址实现缓存设置

reg [2:0] W_MS_r =0;
always @(posedge I_ui_clk)
    if (!I_ui_rstn) W_MS_r <= S_IDLE;
    else W_MS_r <= W_MS;

//每次FDMA DBUF 完成一帧数据传输后，产生中断，这个中断持续60个周期的uiclk,这里的延迟必须足够ZYNQ IP核识别到这个中断
always @(posedge I_ui_clk) begin
    if(I_ui_rstn == 1'b0)begin
        wirq_dly_cnt <= 6'd0;
        O_fmda_wbuf <=0;
    end
    else if((W_MS_r == S_DATA2) && (W_MS == S_FRAME_DONE))begin
        wirq_dly_cnt <= 60;
        O_fmda_wbuf <= O_fmda_wbufn;
    end
    else if(wirq_dly_cnt >0)
        wirq_dly_cnt <= wirq_dly_cnt - 1'b1;
end

//帧同步，对于视频有效
fs_cap #
(
.VIDEO_ENABLE(VIDEO_ENABLE)
)
fs_cap_W0
(
 .I_clk(I_ui_clk),
 .I_rstn(I_ui_rstn),
 .I_vs(I_W_FS),
 .O_fs_cap(W_FS)
);

assign O_fdma_wareq = O_fdma_wareq_r;
reg [7:0] W0_Fbuf ;//synthesis keep
reg camera_done_d1 ,camera_done_d2 ;//synthesis keep

 always @(posedge I_ui_clk)
   if (!I_ui_rstn) camera_done_d2 <= 1'b0;
   else camera_done_d2 <= camera_done_d1 ;
 assign   camera_done = camera_done_d2 ; //camera_done_d1 & (~camera_done_d2) ;
//写通道状态机，采用4个状态值描述

reg camera_work_enable  = 0;

 always @(posedge I_ui_clk) begin
    if(!I_ui_rstn)begin 
	 camera_work_enable <= 0 ;
	end
	else if(camera_start)
     camera_work_enable <= 1 ;
    else if(camera_done)
     camera_work_enable <= 0 ;	
	end

 always @(posedge I_ui_clk) begin
    if(!I_ui_rstn)begin
        W_MS         <= S_IDLE;
        W_FIFO_Rst   <= 0;
        W_addr       <= 0;
        O_W_sync_cnt <= 0;
        W_bcnt       <= 0;
        wrst_cnt     <= 0;
        wdiv_cnt     <= 0;
        O_fmda_wbufn    <= 0;
        W0_Fbuf <= 0;
        O_fdma_wareq_r <= 1'd0;
        camera_done_d1 <= 0 ;
    end   
    else if (VIDEO_ENABLE && overflow_sync &&
             ((W_MS == S_DATA1) || (W_MS == S_DATA2))) begin
        // The last request may be accepted on this very edge. Withdraw it,
        // then observe BUSY in S_DRAIN before resetting any FIFO contents.
        O_fdma_wareq_r <= 1'b0;
        W_MS <= S_DRAIN;
    end
    else begin
      case(W_MS)
        S_IDLE:begin
          camera_done_d1 <= 0 ;
		  if(camera_work_enable)
		   begin

			//camera_done_d1 <= 0 ;
			W_addr <= 0;
			W_bcnt <= 0;
			wrst_cnt <= 0;
			wdiv_cnt <=0;
			if(W_FS) begin //帧同步，对于非视频数据一般常量为1
				W_MS <= S_RST;
				if(O_W_sync_cnt < WYBUF_SIZE) //输出帧同步计数器
					O_W_sync_cnt <= O_W_sync_cnt + 1'b1; 
				else 
					O_W_sync_cnt <= 0;  
		   end	 
		  else
     		W_MS <= S_IDLE ;  
          end
       end
       S_RST:begin//帧同步，对于非视频数据直接跳过,对于视频数据，会同步每一帧，并且复位数据FIFO
           O_fmda_wbufn <= W0_Fbuf ; //I_W_buf;
           wrst_cnt <= wrst_cnt + 1'b1;
           if((VIDEO_ENABLE == 1) && (wrst_cnt < 40))
                W_FIFO_Rst <= 1;
           else if((VIDEO_ENABLE == 1) && (wrst_cnt < 100))
                W_FIFO_Rst <= 0;
           else if(O_fdma_wirq == 1'b0) begin
                W_MS <= S_DATA1;
           end
       end
        S_DATA1:begin //发送写FDMA请求
          if(I_fdma_wbusy == 1'b0 && W_REQ )begin
             O_fdma_wareq_r  <= 1'b1; 
          end 
          else if(I_fdma_wbusy == 1'b1) begin
             O_fdma_wareq_r  <= 1'b0;
             W_MS    <= S_DATA2;
          end          
         end
        S_DATA2:begin //写有效数据
            if(I_fdma_wbusy == 1'b0)begin
                if(W_bcnt == WY_BURST_TIMES - 1'b1) //判断是否传输完毕
                    W_MS <= S_FRAME_DONE ; //S_IDLE;
                else begin
                    if(wdiv_cnt < W_XDIV - 1'b1)begin//如果对XSIZE做了分次传输，一个XSIZE也需要XDIV次FDMA完成传输
                        W_addr <= W_addr +  WX_BURST_ADDR_INC;  //计算地址增量
                        wdiv_cnt <= wdiv_cnt + 1'b1;
                     end
                    else begin
                        W_addr <= W_addr + WX_LAST_ADDR_INC; //计算最后一次地址增量，最后一次地址根据stride 计算
                        wdiv_cnt <= 0;
                    end
                    W_bcnt <= W_bcnt + 1'b1;
                    W_MS    <= S_DATA1;
                end 
            end
         end
        S_DRAIN:begin
            // Requests are issued only with a complete line buffered. Let
            // that line finish, including every AXI B response; never abort
            // AW/W mid-transaction or reset the external AXI writer.
            O_fdma_wareq_r <= 1'b0;
            if (!I_fdma_wbusy) begin
                W_FIFO_Rst <= 1'b1;
                W_MS <= S_RETRY;
            end
        end
        S_RETRY:begin
            // Keep the pending software request and buffer number. A bad
            // frame produces no DONE/IRQ and no completed-buffer advance.
            // Hold reset through the rest of the frame, then reuse S_RST
            // to restart at a fresh VS with the usual reset-release guard.
            W_FIFO_Rst <= 1'b1;
            W_addr <= 0;
            W_bcnt <= 0;
            wdiv_cnt <= 0;
            wrst_cnt <= 0;
            if (W_FS && !overflow_sync)
                W_MS <= S_RST;
        end
        S_FRAME_DONE:begin //写有效数据
            camera_done_d1 <= 1 ;
            if(camera_clear_intr) begin
          		W_MS<=S_IDLE; 
                if(W0_Fbuf < WYBUF_SIZE) //输出帧同步计数器
                  W0_Fbuf <= W0_Fbuf + 1'b1; 
                else 
                  W0_Fbuf <= 0;  
            end
         end 
         default: W_MS <= S_IDLE; 
       endcase
    end
 end 
 
 
 

//写通道的数据FIFO，采用了原语调用xpm_fifo_async fifo，当FIFO存储的数据阈值达到一定量，一般满足一次FDMA的burst即可发出请求
wire W_rbusy;
// Use the FIFO's synchronized read reset and require a complete line.
always @(posedge I_ui_clk)
    if (!I_ui_rstn || W_FIFO_Rst || W_rbusy)
        W_REQ <= 1'b0;
    else
        W_REQ <= (W_rcnt >= FDMA_WX_BURST);
     
wfifo #( 
.DATA_WIDTH_W(W_DATAWIDTH), 
.DATA_WIDTH_R(AXI_DATA_WIDTH), 
.ADDR_WIDTH_W(W_WR_DATA_COUNT_WIDTH), 
.ADDR_WIDTH_R(W_RD_DATA_COUNT_WIDTH), 
.AL_FULL_NUM(WFIFO_DEPTH-2), 
.AL_EMPTY_NUM(2), 
.SHOW_AHEAD_EN(1'b0) , 
.OUTREG_EN ("NOREG")
) 
u_wfifo(
.rst((I_ui_rstn == 1'b0) || (W_FIFO_Rst == 1'b1)), //asynchronous port,active hight
.clkw(I_W_clk),  //write clock
.clkr(I_ui_clk),  //read clock
.we(I_W_wren),  //write enable,active hight
.di(I_W_data),  //write data
.re(I_fdma_wvalid),  //read enable,active hight
.dout(O_fdma_wdata),  //read data
//.valid(),  //read data valid flag
.full_flag(O_W_full),  //fifo full flag
//.empty_flag(),  //fifo empty flag
//.afull(),  //fifo almost full flag
//.aempty(),  //fifo almost empty flag
//.wrusedw(W_rcnt),  //stored data number in fifo
.rdusedw(W_rcnt), //available data number for read
.rd_rst_busy(W_rbusy),
.wr_rst_busy(W_wr_busy)
) ;

end
else begin : FDMA_WRITE_DISABLE

//----------fdma signals write-------       
assign O_fdma_waddr = 0;
assign O_fdma_wareq = 0;
assign O_fdma_wsize = 0;                                    
assign O_fdma_wdata = 0;
assign O_fdma_wready = 0;
assign O_fdma_wirq = 0; 
assign O_W_full = 0;

end
endgenerate


endmodule



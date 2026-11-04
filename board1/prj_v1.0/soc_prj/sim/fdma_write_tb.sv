`timescale 1ns/1ps
// Exercise the real capture FIFO, line scheduler and AXI writer together.
module fdma_write_tb #(parameter integer XSIZE=9, YSIZE=2, FIFO_DEPTH=1024,
    parameter realtime PIXEL_HALF=7.0, CORE_HALF=5.0,
    parameter integer LINE_GAP=0, DDR_STALL_CYCLES=0);
    localparam LINE_BEATS=XSIZE*2, FRAME_BEATS=LINE_BEATS*YSIZE;
    localparam BURSTS=(LINE_BEATS+15)/16, FRAME_BURSTS=BURSTS*YSIZE;
    reg clk=0, pixel_clk=0, rst_n=0;
    always #(CORE_HALF) clk=~clk;
    always #(PIXEL_HALF) pixel_clk=~pixel_clk;
    reg start=0, clear_irq=0, vs=0, pixel_valid=0;
    reg [127:0] pixels=0;
    wire [31:0] line_addr;
    wire line_request, busy, pop, line_ready, full, done, irq;
    wire [15:0] line_size;
    wire [63:0] data;
    wire [7:0] sync_count, buffer_index;
    uidbuf_write #(.AXI_DATA_WIDTH(64),.AXI_ADDR_WIDTH(32),
        .W_BUFDEPTH(FIFO_DEPTH),.W_DATAWIDTH(128),.W_XSIZE(XSIZE),.W_XSTRIDE(XSIZE),
        .W_YSIZE(YSIZE),.W_XDIV(1)) capture (
        .W_BASEADDR(32'h10000),.W_BUFSIZE(16'd6),
        .I_ui_clk(clk),.I_ui_rstn(rst_n),.I_W_clk(pixel_clk),.I_W_FS(vs),
        .I_W_wren(pixel_valid),.I_W_data(pixels),.O_W_sync_cnt(sync_count),
        .I_W_buf(sync_count),.O_W_full(full),.O_fdma_waddr(line_addr),
        .O_fdma_wareq(line_request),.O_fdma_wsize(line_size),.I_fdma_wbusy(busy),
        .O_fdma_wdata(data),.I_fdma_wvalid(pop),.O_fdma_wready(line_ready),
        .O_fmda_wbuf(buffer_index),.O_fdma_wirq(irq),.camera_start(start),
        .camera_done(done),.camera_clear_intr(clear_irq));
    wire [31:0] awaddr;
    wire [7:0] awlen;
    wire [2:0] awsize;
    wire [63:0] wdata;
    wire [7:0] wstrb;
    wire awvalid,wvalid,wlast,bready;
    reg awready=0,wready=0,bvalid=0,memory_pause=0;
    uiFDMA #(.M_AXI_ID_WIDTH(5),.M_AXI_ADDR_WIDTH(32),.M_AXI_DATA_WIDTH(64),
        .M_AXI_MAX_BURST_LEN(16)) writer (
        .I_fdma_waddr(line_addr),.I_fdma_wareq(line_request),.I_fdma_wsize(line_size),
        .O_fdma_wbusy(busy),.I_fdma_wdata(data),.O_fdma_wvalid(pop),
        .I_fdma_wready(line_ready),.I_fdma_raddr(32'd0),.I_fdma_rareq(1'b0),
        .I_fdma_rsize(16'd0),.O_fdma_rbusy(),.O_fdma_rdata(),.O_fdma_rvalid(),
        .I_fdma_rready(1'b0),.M_AXI_ACLK(clk),.M_AXI_ARESETN(rst_n),
        .M_AXI_AWID(),.M_AXI_AWADDR(awaddr),.M_AXI_AWLEN(awlen),.M_AXI_AWSIZE(awsize),
        .M_AXI_AWBURST(),.M_AXI_AWLOCK(),.M_AXI_AWCACHE(),.M_AXI_AWPROT(),
        .M_AXI_AWQOS(),.M_AXI_AWVALID(awvalid),.M_AXI_AWREADY(awready),
        .M_AXI_WID(),.M_AXI_WDATA(wdata),.M_AXI_WSTRB(wstrb),.M_AXI_WLAST(wlast),
        .M_AXI_WVALID(wvalid),.M_AXI_WREADY(wready),.M_AXI_BID(5'd0),
        .M_AXI_BRESP(2'b00),.M_AXI_BVALID(bvalid),.M_AXI_BREADY(bready),
        .M_AXI_ARID(),.M_AXI_ARADDR(),.M_AXI_ARLEN(),.M_AXI_ARSIZE(),
        .M_AXI_ARBURST(),.M_AXI_ARLOCK(),.M_AXI_ARCACHE(),.M_AXI_ARPROT(),
        .M_AXI_ARQOS(),.M_AXI_ARVALID(),.M_AXI_ARREADY(1'b0),.M_AXI_RID(5'd0),
        .M_AXI_RDATA(64'd0),.M_AXI_RRESP(2'b00),.M_AXI_RLAST(1'b0),
        .M_AXI_RVALID(1'b0),.M_AXI_RREADY());
    integer cycle=0,words=0,addresses=0,responses=0,irq_edges=0,checks=0;
    integer tag=0,frame=0,last_stalls=0,max_outstanding=0;
    integer bdelay=-1, expected_len, completed_bursts=0;
    reg prev_irq=0;
    reg w_stalled=0,aw_stalled=0;
    reg [72:0] w_held;
    reg [42:0] aw_held;
    integer write_wraps=0,read_wraps=0;
    reg [31:0] previous_write_pointer=0,previous_read_pointer=0;
    // Count real pointer rollovers separately from FIFO reset-to-zero.
    always @(posedge pixel_clk or negedge rst_n) begin
        if(!rst_n)begin write_wraps=0;previous_write_pointer=0;end
        else if(capture.FDMA_WRITE_ENABLE.u_wfifo.wr_rst)previous_write_pointer=0;
        else begin
            if(capture.FDMA_WRITE_ENABLE.u_wfifo.wr_addr<previous_write_pointer)write_wraps=write_wraps+1;
            previous_write_pointer=capture.FDMA_WRITE_ENABLE.u_wfifo.wr_addr;
        end
    end
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n)begin read_wraps=0;previous_read_pointer=0;end
        else if(capture.FDMA_WRITE_ENABLE.u_wfifo.rd_rst)previous_read_pointer=0;
        else begin
            if(capture.FDMA_WRITE_ENABLE.u_wfifo.rd_addr<previous_read_pointer)read_wraps=read_wraps+1;
            previous_read_pointer=capture.FDMA_WRITE_ENABLE.u_wfifo.rd_addr;
        end
    end
    task check(input bit condition,input string message);
        begin checks=checks+1;if(!condition)$fatal(1,"%s cycle=%0d words=%0d AW=%0d B=%0d",message,cycle,words,addresses,responses);end
    endtask
    always @(negedge clk) begin
        awready = rst_n && !memory_pause && cycle%11>=4;
        wready = rst_n && !memory_pause && cycle%5>=2;
    end
    // One finite DDR service interruption, in addition to per-burst stalls.
    initial if (DDR_STALL_CYCLES>0) begin
        wait(wvalid);@(posedge clk);#1;memory_pause=1;
        repeat(DDR_STALL_CYCLES)@(posedge clk);
        #1;memory_pause=0;
    end
    always @(posedge clk) begin
        if(!rst_n) begin
            cycle=0;words=0;addresses=0;responses=0;irq_edges=0;
            completed_bursts=0;bdelay=-1;bvalid<=0;
            max_outstanding=0;
            w_stalled=0;aw_stalled=0;prev_irq=0;
        end else begin
            cycle=cycle+1;
            check((^{line_request,busy,pop,done,irq})!==1'bx,"control contains X");
            check(busy || !wvalid,"write data remained after DDR completion");
            if(w_stalled)check(wvalid && {wdata,wstrb,wlast}===w_held,"W payload changed under backpressure");
            if(aw_stalled)check(awvalid && {awaddr,awlen,awsize}===aw_held,"AW payload changed under backpressure");
            w_stalled=wvalid&&!wready;w_held={wdata,wstrb,wlast};
            aw_stalled=awvalid&&!awready;aw_held={awaddr,awlen,awsize};
            if(wvalid&&!wready&&wlast)last_stalls=last_stalls+1;
            if(awvalid&&awready) begin
                expected_len=LINE_BEATS-(addresses%BURSTS)*16;
                if(expected_len>16)expected_len=16;
                check(awlen==expected_len-1 && awsize==3,"AXI burst geometry");
                check(awaddr==32'h10000+((addresses%FRAME_BURSTS)/BURSTS)*LINE_BEATS*8+(addresses%BURSTS)*128,"line/burst address");
                addresses=addresses+1;
                if(addresses-responses>max_outstanding)max_outstanding=addresses-responses;
            end
            if(wvalid&&wready) begin
                check(wdata===64'(tag+words%FRAME_BEATS),"FIFO data loss or reordering");
                check(wstrb==8'hff,"write strobes");
                check(wlast==(((words%LINE_BEATS)%16==15)||(words%LINE_BEATS==LINE_BEATS-1)),"WLAST position");
                if(wlast)completed_bursts=completed_bursts+1;
                words=words+1;
            end
            if(bvalid&&bready) begin
                bvalid<=0;responses=responses+1;bdelay=-1;
            end else if(addresses>responses&&completed_bursts>responses&&!bvalid) begin
                if(bdelay<0)bdelay=17;
                else if(bdelay==0)bvalid<=1;
                else bdelay=bdelay-1;
                check(busy && !done,"completion before write response");
            end
            if(irq&&!prev_irq)irq_edges=irq_edges+1;
            prev_irq=irq;
        end
    end
    task push(input integer first,input integer count);
        integer i;
        begin
            for(i=first;i<first+count;i=i+1)begin
                @(negedge pixel_clk);pixel_valid=1;
                pixels={64'(tag+2*i+1),64'(tag+2*i)};
                check(!full,"unexpected FIFO overflow");
                if(LINE_GAP>0 && (i+1)%XSIZE==0)begin
                    @(negedge pixel_clk);pixel_valid=0;
                    repeat(LINE_GAP)@(negedge pixel_clk);
                end
            end
            @(negedge pixel_clk);pixel_valid=0;
        end
    endtask
    task begin_frame;
        begin
            @(negedge clk);start=1;
            repeat(2)@(negedge clk);start=0;
            @(negedge pixel_clk);vs=1;
            repeat(5)@(negedge pixel_clk);vs=0;
            wait(capture.FDMA_WRITE_ENABLE.W_MS==2);
            repeat(5)@(negedge clk);
        end
    endtask
    initial begin
        // Guard the physical geometry, not only data ordering. The old log2
        // helper passed traffic tests while silently doubling every RAM depth.
        if ((1 << capture.FDMA_WRITE_ENABLE.W_WR_DATA_COUNT_WIDTH) != FIFO_DEPTH)
            $fatal(1,"write FIFO allocation differs from configured depth");
        if ((1 << capture.FDMA_WRITE_ENABLE.W_RD_DATA_COUNT_WIDTH) != FIFO_DEPTH*2)
            $fatal(1,"read FIFO allocation differs from 128-to-64 geometry");
        repeat(6)@(negedge clk);rst_n=1;
        for(frame=0;frame<7;frame=frame+1)begin
            tag=frame*1000000;
            begin_frame();
            push(0,XSIZE-1); // Deliberately shorter than a complete line.
            repeat(60)@(negedge clk);
            check(addresses==frame*FRAME_BURSTS,"request with an incomplete line");
            push(XSIZE-1,XSIZE*(YSIZE-1)+1);
            wait(done===1'b1);@(negedge clk);
            check(words==(frame+1)*FRAME_BEATS && responses==(frame+1)*FRAME_BURSTS,"frame completion accounting");
            check(buffer_index==frame%6,"buffer index reset/wrap");
            check(sync_count==(frame+1)%6,"frame sync count width/wrap");
            check(irq_edges==frame+1,"one completion IRQ per frame");
            if(XSIZE*YSIZE>=2*FIFO_DEPTH)
                check(write_wraps>=frame+1&&read_wraps>=frame+1,"FIFO pointers did not wrap within each frame");
            clear_irq=1;repeat(3)@(negedge clk);clear_irq=0;
            wait(done===1'b0);repeat(8)@(negedge clk);
        end
        check(last_stalls>0,"last-beat backpressure was not exercised");
        check(max_outstanding>=2,"pipelined write responses were not exercised");
        // Abort an active burst, then prove that no stale FIFO/state survives.
        tag=0;begin_frame();push(0,XSIZE*YSIZE);
        wait(words>7*FRAME_BEATS+3);@(negedge clk);rst_n=0;
        repeat(8)@(negedge clk);rst_n=1;
        repeat(8)@(negedge clk);
        check(!awvalid&&!wvalid&&!busy&&!done,"reset did not return to idle");
        begin_frame();push(0,XSIZE*YSIZE);wait(done===1'b1);@(negedge clk);
        check(words==FRAME_BEATS&&responses==FRAME_BURSTS&&buffer_index==0,"recovery after mid-burst reset");
        if(XSIZE*YSIZE>=2*FIFO_DEPTH)
            check(write_wraps>0&&read_wraps>0,"pointer wrap missing after reset recovery");
        $display("PASS fdma_write XSIZE=%0d YSIZE=%0d FIFO_DEPTH=%0d: %0d checks; 8 frames, async FIFO, AW/W stalls, delayed B, reset and buffer wrap; FIFO pointer wraps W=%0d R=%0d",XSIZE,YSIZE,FIFO_DEPTH,checks,write_wraps,read_wraps);
        $finish;
    end
    initial begin #(YSIZE>2?30000000:3000000);$fatal(1,"DMA test timed out");end
endmodule


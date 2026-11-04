`timescale 1ns/1ps
// Exercise the real capture FIFO, line scheduler and AXI writer together.
module capture_overflow_tb #(parameter integer XSIZE=9, YSIZE=24, FIFO_DEPTH=32, XDIV=1,
    parameter realtime PIXEL_HALF=3.2, CORE_HALF=3.333333);
    localparam LINE_BEATS=XSIZE*2/XDIV, FRAME_BEATS=XSIZE*2*YSIZE;
    localparam BURSTS=(LINE_BEATS+15)/16, FRAME_BURSTS=BURSTS*YSIZE*XDIV;
    reg clk=0, pixel_clk=0, rst_n=0;
    reg core_clock_running=1;
    always #(CORE_HALF) if(core_clock_running) clk=~clk;
    reg pixel_clock_running=1;
    always #(PIXEL_HALF) if(pixel_clock_running) pixel_clk=~pixel_clk;
    reg start=0, clear_irq=0, vs=0, pixel_valid=0;
    reg [127:0] pixels=0;
    wire [31:0] line_addr;
    wire line_request, busy, pop, line_ready, full, done, irq;
    wire [15:0] line_size;
    wire [63:0] data;
    wire [7:0] sync_count, buffer_index;
    uidbuf_write #(.AXI_DATA_WIDTH(64),.AXI_ADDR_WIDTH(32),
        .W_BUFDEPTH(FIFO_DEPTH),.W_DATAWIDTH(128),.W_XSIZE(XSIZE),.W_XSTRIDE(XSIZE),
        .W_YSIZE(YSIZE),.W_XDIV(XDIV)) capture (
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
    reg awready=0,wready=0,bvalid=0;
    integer pause_mode=0;
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
    integer checks=0, cycle=0, words=0, addresses=0, responses=0;
    integer attempt_words=0, attempt_addresses=0, expected_tag=0;
    integer completed_bursts=0, bdelay=-1, irq_edges=0, mode, repetition;
    integer produced=0, reset_while_busy=0, retry_entries=0;
    integer rejected_words=0;
    reg prev_irq=0, w_stalled=0, aw_stalled=0;
    reg [72:0] w_held;
    reg [42:0] aw_held;
    reg [2:0] previous_state=0;
    always @(posedge pixel_clk) begin
        if(!rst_n)rejected_words=0;
        else if(pixel_valid && full && !capture.FDMA_WRITE_ENABLE.W_wr_busy)rejected_words++;
    end
    task check(input bit condition,input string message);
        begin checks++;if(!condition)$fatal(1,"%s cycle=%0d state=%0d W=%0d AW=%0d B=%0d",message,cycle,capture.FDMA_WRITE_ENABLE.W_MS,words,addresses,responses);end
    endtask
    always @(negedge clk) begin
        awready=rst_n && pause_mode!=1 && cycle%5!=0;
        wready=rst_n && pause_mode!=2 && cycle%4!=0;
    end
    always @(posedge clk) begin
        if(!rst_n)begin
            cycle=0;words=0;addresses=0;responses=0;completed_bursts=0;
            bdelay=-1;bvalid<=0;irq_edges=0;prev_irq=0;
            w_stalled=0;aw_stalled=0;previous_state=0;
        end else begin
            cycle++;
            check((^{line_request,busy,pop,done,irq})!==1'bx,"unknown control");
            check(!capture.FDMA_WRITE_ENABLE.W_FIFO_Rst || !busy,"FIFO reset during outstanding AXI transaction");
            if(w_stalled)check(wvalid && {wdata,wstrb,wlast}===w_held,"W changed during backpressure");
            if(aw_stalled)check(awvalid && {awaddr,awlen,awsize}===aw_held,"AW changed during backpressure");
            w_stalled=wvalid&&!wready;w_held={wdata,wstrb,wlast};
            aw_stalled=awvalid&&!awready;aw_held={awaddr,awlen,awsize};
            if(capture.FDMA_WRITE_ENABLE.W_MS==5 || capture.FDMA_WRITE_ENABLE.W_MS==6)begin
                check(!done && !irq,"bad frame published as complete");
                check(!line_request,"new line request during overflow recovery");
            end
            if(capture.FDMA_WRITE_ENABLE.W_MS==6 && previous_state!=6)retry_entries++;
            previous_state=capture.FDMA_WRITE_ENABLE.W_MS;
            if(awvalid&&awready)begin
                check(awsize==3,"AXI size");
                check(awaddr==32'h10000+(attempt_addresses/BURSTS)*LINE_BEATS*8+(attempt_addresses%BURSTS)*128,"retry did not restart frame address");
                attempt_addresses++;addresses++;
            end
            if(wvalid&&wready)begin
                check(wdata===64'(expected_tag+attempt_words),"old/bad frame data leaked into retry or active line damaged");
                check(wstrb==8'hff,"AXI strobes");
                check(wlast==(((attempt_words%LINE_BEATS)%16==15)||(attempt_words%LINE_BEATS==LINE_BEATS-1)),"WLAST position");
                attempt_words++;words++;
                if(wlast)completed_bursts++;
            end
            if(bvalid&&bready)begin
                bvalid<=0;responses++;bdelay=-1;
            end else if(addresses>responses&&completed_bursts>responses&&!bvalid)begin
                if(bdelay<0)bdelay=9;
                else if(bdelay==0 && pause_mode!=3)bvalid<=1;
                else if(bdelay>0)bdelay--;
                check(busy && !done,"completion before AXI response");
            end
            if(irq&&!prev_irq)irq_edges++;
            prev_irq=irq;
        end
    end
    task reset_dut;
        begin
            @(negedge clk);rst_n=0;start=0;clear_irq=0;vs=0;pixel_valid=0;pause_mode=0;
            pixel_clock_running=1;core_clock_running=1;attempt_words=0;attempt_addresses=0;expected_tag=0;
            repeat(8)@(negedge clk);rst_n=1;
            repeat(8)@(negedge pixel_clk);
        end
    endtask
    task pulse_vs;
        begin
            @(negedge pixel_clk);vs=1;
            repeat(8)@(negedge pixel_clk);vs=0;
        end
    endtask
    task begin_frame;
        begin
            @(negedge clk);start=1;
            repeat(2)@(negedge clk);start=0;
            pulse_vs();
            wait(capture.FDMA_WRITE_ENABLE.W_MS==2);
            repeat(5)@(negedge clk);
        end
    endtask
    task send_words(input integer first,input integer count,input integer gap);
        integer i;
        begin
            for(i=first;i<first+count;i++)begin
                @(negedge pixel_clk);pixel_valid=1;
                pixels={64'(expected_tag+2*i+1),64'(expected_tag+2*i)};
                if(gap>0)begin
                    @(negedge pixel_clk);pixel_valid=0;
                    repeat(gap)@(negedge pixel_clk);
                end
            end
            @(negedge pixel_clk);pixel_valid=0;
        end
    endtask
    task prepare_retry;
        begin
            wait(capture.FDMA_WRITE_ENABLE.W_MS==6);
            repeat(12)@(negedge clk);
            check(!busy && addresses==responses,"retry before every B response");
            check(!done && irq_edges==0 && buffer_index==0,"bad frame advanced completion state");
            check(capture.FDMA_WRITE_ENABLE.W_FIFO_Rst,"bad frame cache not held reset");
            attempt_words=0;attempt_addresses=0;
        end
    endtask
    task fill_without_rejection;
        begin
            produced=0;@(negedge pixel_clk);
            while(!full)begin
                pixel_valid=1;
                pixels={64'(2*produced+1),64'(2*produced)};
                @(posedge pixel_clk);produced++;
                @(negedge pixel_clk);
            end
            pixel_valid=0;
            repeat(10)@(negedge clk);
            check(rejected_words==0 && !capture.FDMA_WRITE_ENABLE.overflow_sync,"full without rejection treated as overflow");
        end
    endtask
    task retry_frame;
        begin
            pulse_vs();
            wait(capture.FDMA_WRITE_ENABLE.W_MS==2);
            repeat(5)@(negedge clk);
        end
    endtask
    task good_frame(input bit retry_request);
        begin
            @(negedge clk);expected_tag=1000000;attempt_words=0;attempt_addresses=0;
            if(retry_request)retry_frame();else begin_frame();
            send_words(0,XSIZE*YSIZE,12);
            wait(done);@(negedge clk);
            check(attempt_words==FRAME_BEATS,"recovered frame is incomplete");
            check(attempt_addresses==FRAME_BURSTS && addresses==responses,"recovered AXI frame accounting");
            check(irq_edges==1 && buffer_index==0 && sync_count==1,"retry changed IRQ/buffer/request semantics");
            clear_irq=1;repeat(3)@(negedge clk);clear_irq=0;
            wait(!done);repeat(8)@(negedge clk);
            check(!busy && !line_request,"writer not idle after acknowledgement");
        end
    endtask
    initial begin
        // Repeated overflow within ONE software request, independently stall
        // AW, W and B. A VS during congestion must not reset an AXI transfer.
        for(mode=1;mode<=3;mode++)begin
            $display("case stalled mode=%0d",mode);reset_dut();pause_mode=mode;begin_frame();
            for(repetition=0;repetition<2;repetition++)begin
                if(repetition>0)begin pause_mode=mode;retry_frame();end
                send_words(0,XSIZE*YSIZE,0);
                wait(capture.FDMA_WRITE_ENABLE.overflow_sync);
                repeat(8)@(negedge clk);
                check(busy && capture.FDMA_WRITE_ENABLE.W_MS==5,"overflow did not drain outstanding line");
                pulse_vs();repeat(20)@(negedge clk);
                check(!done && !irq && !capture.FDMA_WRITE_ENABLE.W_FIFO_Rst,"premature recovery while DDR blocked");
                pause_mode=0;prepare_retry();
            end
            good_frame(1);
            if(mode==3)begin
                @(negedge clk);expected_tag=2000000;attempt_words=0;attempt_addresses=0;
                begin_frame();send_words(0,XSIZE*YSIZE,12);
                wait(done);@(negedge clk);
                check(attempt_words==FRAME_BEATS && addresses==responses,"next capture after retry is incomplete");
                check(irq_edges==2 && buffer_index==1 && sync_count==2,"next request lost buffer/IRQ sequence");
            end
        end
        // Accumulate overflow with the UI clock paused, first before a
        // request exists and then with a request awaiting acceptance. This
        // exercises both an idle drain and the request/BUSY race on resume.
        for(mode=0;mode<2;mode++)begin
            reset_dut();pause_mode=2;begin_frame();
            if(mode==1)begin
                send_words(0,XSIZE/XDIV,0);
                wait(line_request && !busy);
            end
            @(negedge clk);core_clock_running=0;
            if(mode==0)send_words(0,XSIZE*YSIZE,0);
            else send_words(XSIZE/XDIV,XSIZE*YSIZE-XSIZE/XDIV,0);
            check(capture.FDMA_WRITE_ENABLE.overflow_latched,"overflow before request acceptance not exercised");
            core_clock_running=1;
            wait(capture.FDMA_WRITE_ENABLE.W_MS==5 || capture.FDMA_WRITE_ENABLE.W_MS==6);
            pause_mode=0;prepare_retry();good_frame(1);
        end
        // Merely reaching full is legal: stop before any rejected write,
        // resume DDR and finish this same frame without a retry.
        $display("case full without loss");reset_dut();pause_mode=2;begin_frame();
        fill_without_rejection();
        pause_mode=0;
        wait(!full);
        send_words(produced,XSIZE*YSIZE-produced,12);
        wait(done);@(negedge clk);
        check(attempt_words==FRAME_BEATS && irq_edges==1,"full-but-valid frame lost");
        $display("full case complete");
        // One rejected input cycle, followed by no further pixels. A pulse
        // synchronizer alone could miss this; the sticky level must survive.
        reset_dut();pause_mode=2;begin_frame();fill_without_rejection();
        @(negedge pixel_clk);pixel_valid=1;pixels='1;
        @(negedge pixel_clk);pixel_valid=0;
        wait(capture.FDMA_WRITE_ENABLE.W_MS==5);
        check(rejected_words==1,"single-cycle rejection not exercised");
        pause_mode=0;prepare_retry();good_frame(1);
        // A stopped capture clock must not lose the sticky overflow level.
        reset_dut();pause_mode=2;begin_frame();send_words(0,XSIZE*YSIZE,0);
        wait(capture.FDMA_WRITE_ENABLE.overflow_sync);
        @(negedge pixel_clk);pixel_clock_running=0;
        pause_mode=0;wait(capture.FDMA_WRITE_ENABLE.W_MS==6);
        repeat(30)@(negedge clk);
        check(!done && capture.FDMA_WRITE_ENABLE.W_FIFO_Rst,"clock-stop recovery not held");
        pixel_clock_running=1;prepare_retry();good_frame(1);
        // Reset during drain and while waiting for a fresh frame. Reuse the
        // existing global AXI reset, then check a wholly new capture.
        for(mode=0;mode<2;mode++)begin
            reset_dut();pause_mode=2;begin_frame();send_words(0,XSIZE*YSIZE,0);
            wait(capture.FDMA_WRITE_ENABLE.W_MS==5);
            if(mode==1)begin pause_mode=0;prepare_retry();end
            else reset_while_busy++;
            reset_dut();good_frame(0);
        end
        check(retry_entries>=11 && reset_while_busy==1,"recovery/reset cases not exercised");
        $display("PASS capture_overflow XSIZE=%0d DEPTH=%0d XDIV=%0d: %0d checks; repeated bad frames, AW/W/B stalls, full without loss, stopped clock, resets, exact recovered pixels and IRQ",XSIZE,FIFO_DEPTH,XDIV,checks);
        $finish;
    end
    initial begin #(XSIZE>100?15000000:5000000);$fatal(1,"overflow recovery timed out state=%0d mode=%0d words=%0d attempt=%0d produced=%0d",capture.FDMA_WRITE_ENABLE.W_MS,mode,words,attempt_words,produced);end
endmodule

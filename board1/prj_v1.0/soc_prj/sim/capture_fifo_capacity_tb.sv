`timescale 1ns/1ps
// Exercise the configured capture RAM at its real full boundary while DDR
// makes no progress. Rejected writes must not overwrite unread pixels.
module capture_fifo_capacity_tb #(parameter integer DEPTH=4096);
    reg clk=0, pixel_clk=0, rst_n=0, we=0, pop=0;
    always #3.333 clk=~clk;
    always #3.2 pixel_clk=~pixel_clk;
    reg [127:0] input_data=0;
    wire [63:0] output_data;
    wire full;
    integer checks=0, accepted=0, i;
    uidbuf_write #(.W_BUFDEPTH(DEPTH),.W_DATAWIDTH(128),.AXI_DATA_WIDTH(64),
        .W_XSIZE(480),.W_XSTRIDE(480),.W_YSIZE(1080)) capture (
        .W_BASEADDR(32'd0),.W_BUFSIZE(16'd6),.I_ui_clk(clk),.I_ui_rstn(rst_n),
        .I_W_clk(pixel_clk),.I_W_FS(1'b0),.I_W_wren(we),.I_W_data(input_data),
        .O_W_sync_cnt(),.I_W_buf(8'd0),.O_W_full(full),.O_fdma_waddr(),
        .O_fdma_wareq(),.O_fdma_wsize(),.I_fdma_wbusy(1'b1),
        .O_fdma_wdata(output_data),.I_fdma_wvalid(pop),.O_fdma_wready(),
        .O_fmda_wbuf(),.O_fdma_wirq(),.camera_start(1'b0),
        .camera_done(),.camera_clear_intr(1'b0));
    task check(input bit condition,input string message);
        begin checks++;if(!condition)$fatal(1,"%s",message);end
    endtask
    task drain(input integer count,input integer tag);
        integer j;
        begin
            // Wait for synchronized write pointer to reach the read domain.
            repeat(8)@(negedge clk);
            for(j=0;j<count*2;j++)begin
                @(negedge clk);pop=1;
                @(posedge clk);#1;
                check(output_data===64'(tag+j),"unread data overwritten, lost or reordered");
            end
            @(negedge clk);pop=0;
            repeat(8)@(negedge pixel_clk);
        end
    endtask
    initial begin
        check((1<<capture.FDMA_WRITE_ENABLE.W_WR_DATA_COUNT_WIDTH)==DEPTH,"write allocation exceeds declared depth");
        check((1<<capture.FDMA_WRITE_ENABLE.W_RD_DATA_COUNT_WIDTH)==DEPTH*2,"gearbox read allocation incorrect");
        repeat(8)@(negedge clk);rst_n=1;
        repeat(8)@(negedge pixel_clk);
        while(!full && accepted<=DEPTH)begin
            we=1;input_data={64'(2*accepted+1),64'(2*accepted)};
            @(posedge pixel_clk);accepted++;
            @(negedge pixel_clk);
        end
        // The existing FIFO reserves one word to distinguish full/empty.
        check(full && accepted==DEPTH-1,"unexpected usable FIFO capacity");
        repeat(16)begin
            input_data='1;
            @(posedge pixel_clk);#1;
            check(capture.FDMA_WRITE_ENABLE.u_wfifo.wr_addr==DEPTH-1,"full FIFO accepted a write");
            @(negedge pixel_clk);
        end
        we=0;
        drain(accepted,0);
        check(!full,"full did not clear after DDR resumed");
        // Refill across the address rollover, then verify every 64-bit half.
        for(i=0;i<32;i++)begin
            @(negedge pixel_clk);we=1;
            input_data={64'(20000+2*i+1),64'(20000+2*i)};
        end
        @(negedge pixel_clk);we=0;
        drain(32,20000);
        check(capture.FDMA_WRITE_ENABLE.u_wfifo.empty_flag,"FIFO not empty after drain");
        $display("PASS capture_fifo_capacity DEPTH=%0d usable=%0d: %0d checks; full, rejected writes, drain, refill and rollover",DEPTH,accepted,checks);
        $finish;
    end
    initial begin #300000;$fatal(1,"capacity test timed out");end
endmodule

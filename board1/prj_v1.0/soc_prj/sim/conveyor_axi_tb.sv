`timescale 1ns/1ps
module conveyor_axi_tb;
    reg clk=0;
    always #500 clk=~clk; // accelerated 1 MHz hardware configuration
    reg rst_n=0;
    reg [11:0] awaddr=0, araddr=0;
    reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
    wire awready,wready,bvalid,arready,rvalid;
    reg [31:0] wdata=0;
    reg [3:0] wstrb=0;
    wire [31:0] rdata;
    wire [1:0] bresp,rresp;
    reg estop=0,driver_fault=0;
    reg encoder_a=0,encoder_b=0;
    wire pwm,dir,en;
    wire rpwm,lpwm,r_en,l_en;
    integer checks=0, high_count=0, i;
    reg [31:0] value;
    conveyor_axi #(.CLK_HZ(1000000),.PWM_HZ(10000),
        .WATCHDOG_MS(2),.REVERSE_GAP_US(10)) dut (
        .aclk(clk),.aresetn(rst_n),
        .s_axi_awaddr(awaddr),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_araddr(araddr),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
        .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
        .estop(estop),.driver_fault(driver_fault),.motor_pwm(pwm),.motor_dir(dir),.motor_enable(en),
        .encoder_a(encoder_a),.encoder_b(encoder_b),
        .bts_rpwm(rpwm),.bts_lpwm(lpwm),.bts_r_en(r_en),.bts_l_en(l_en)
    );
    always @(negedge clk) if(rst_n) begin
        if((rpwm && lpwm) || r_en!=en || l_en!=en ||
           rpwm!=(pwm && !dir) || lpwm!=(pwm && dir))
            $fatal(1,"BTS7960 truth table mismatch");
    end
    task encoder_state;
        input [1:0] ab;
        begin
            @(negedge clk);encoder_a=ab[1];encoder_b=ab[0];
            repeat(5) @(negedge clk);
        end
    endtask
    task encoder_forward;
        begin encoder_state(1);encoder_state(3);encoder_state(2);encoder_state(0);end
    endtask
    task check;
        input condition;
        input [8*100-1:0] message;
        begin
            checks=checks+1;
            if(!condition) begin $display("FAIL: %0s at %0t",message,$time);$fatal(1);end
        end
    endtask
    task write_reg;
        input [11:0] addr;
        input [31:0] data;
        input [3:0] strobes;
        input integer address_delay,data_delay,stall;
        input [1:0] response;
        begin
            fork
                begin
                    repeat(address_delay) @(negedge clk);
                    @(negedge clk);awaddr=addr;awvalid=1;
                    @(posedge clk);while(!awready) @(posedge clk);
                    @(negedge clk);awvalid=0;
                end
                begin
                    repeat(data_delay) @(negedge clk);
                    @(negedge clk);wdata=data;wstrb=strobes;wvalid=1;
                    @(posedge clk);while(!wready) @(posedge clk);
                    @(negedge clk);wvalid=0;
                end
            join
            wait(bvalid);#1;check(bresp==response,"AXI write response");
            repeat(stall) begin
                @(negedge clk);check(bvalid && bresp==response,"B response held under backpressure");
            end
            @(negedge clk);bready=1;
            @(posedge clk);@(negedge clk);bready=0;
        end
    endtask
    task read_reg;
        input [11:0] addr;
        input integer stall;
        input [1:0] response;
        output [31:0] result;
        begin
            @(negedge clk);araddr=addr;arvalid=1;
            @(posedge clk);while(!arready) @(posedge clk);
            @(negedge clk);arvalid=0;
            wait(rvalid);#1;result=rdata;check(rresp==response,"AXI read response");
            repeat(stall) begin
                @(negedge clk);check(rvalid && rdata==result && rresp==response,
                                       "R response held under backpressure");
            end
            @(negedge clk);rready=1;
            @(posedge clk);@(negedge clk);rready=0;
        end
    endtask
    task command;
        input [31:0] ticks,control;
        begin
            write_reg(8'h08,ticks,4'hf,0,3,0,0);
            write_reg(8'h0c,control,4'hf,3,0,0,0);
            write_reg(8'h10,32'hc0de0001,4'hf,0,0,0,0);
            repeat(4) @(negedge clk);
        end
    endtask
    task sample_period;
        input integer expected;
        begin
            high_count=0;
            repeat(100) begin @(negedge clk);if(pwm) high_count=high_count+1;end
            check(high_count==expected,"PWM high cycles in complete period");
        end
    endtask
    initial begin
        #20000000;$display("FAIL: simulation timeout");$fatal(1);
    end
    initial begin
        repeat(3) @(negedge clk);check(!pwm && !en,"reset disables output");
        rst_n=1;repeat(3) @(negedge clk);
        read_reg(0,5,0,value);check(value==32'h434e5601,"version ID");
        read_reg(4,0,0,value);check(value==100,"PWM period");
        read_reg(8'h18,0,0,value);check(value==2000,"watchdog ticks");
        read_reg(8'h20,0,0,value);check(value==1000000,"clock frequency");
        check(!en && !pwm,"power-up stays disabled");
        encoder_forward();read_reg(8'h24,0,0,value);check(value==4,"AB x4 forward");
        encoder_state(2);encoder_state(3);encoder_state(1);encoder_state(0);
        read_reg(8'h24,0,0,value);check(value==0,"AB x4 reverse");
        encoder_state(3);read_reg(8'h28,0,0,value);check(value==1,"invalid AB transition counted");
        read_reg(8'h24,0,0,value);check(value==0,"invalid AB transition ignored");
        encoder_state(0);
        @(negedge clk);dut.encoder_count=32'hfffffffe;
        encoder_forward();read_reg(8'h24,0,0,value);check(value==2,"encoder wraps modulo 32 bits");
        // Writes to shadows do not apply themselves.
        write_reg(8'h08,50,4'hf,0,4,3,0);
        write_reg(8'h0c,1,4'hf,4,0,3,0);
        repeat(20) @(negedge clk);check(!en,"uncommitted shadow cannot enable");
        write_reg(8'h10,32'hc0de0000,4'hf,0,0,1,2);
        check(!en,"invalid apply key cannot enable");
        write_reg(8'h10,32'hc0de0001,4'h7,0,0,1,2);
        check(!en,"partial apply cannot enable");
        write_reg(8'h10,32'hc0de0001,4'hf,1,0,0,0);
        repeat(20) @(negedge clk);check(en && !dir,"valid forward command");
        sample_period(50);sample_period(50);
        // Update byte strobes; no implicit motor update.
        write_reg(8'h08,32'hffff004b,4'b0001,2,0,0,0);
        read_reg(8'h08,0,0,value);check(value==75,"byte strobes preserve other bytes");
        read_reg(8'h1c,0,0,value);check(value==50,"active duty isolated from shadow");
        write_reg(8'h10,32'hc0de0001,4'hf,0,2,0,0);
        repeat(4) @(negedge clk);sample_period(75);
        // Direction transition creates a gap with EN and PWM low.
        write_reg(8'h0c,3,4'hf,0,0,0,0);
        write_reg(8'h10,32'hc0de0001,4'hf,0,0,0,0);
        check(!en && !pwm && dir,"reverse disables bridge for the gap");
        for(i=0;i<5;i=i+1) begin @(negedge clk);check(!en && !pwm,"reversal dead time");end
        repeat(12) @(negedge clk);check(en && dir,"reverse resumes after gap");
        sample_period(75);
        command(100,3);sample_period(100);
        command(0,3);check(!en && !pwm,"zero duty disables");
        command(30,1);repeat(15) @(negedge clk);
        command(0,0);check(!en && !pwm,"disable applies promptly");
        command(101,1);read_reg(8'h14,0,0,value);
        check(value[10] && !en,"out-of-range duty latches fault");
        command(20,1);check(!en,"fault blocks enable");
        command(0,4);read_reg(8'h14,0,0,value);check(value[10:8]==0 && !en,"clear remains disabled");
        command(20,1);repeat(15) @(negedge clk);
        repeat(1800) @(negedge clk);
        command(20,1);repeat(1700) @(negedge clk);check(en,"heartbeat refreshes watchdog");
        repeat(400) @(negedge clk);read_reg(8'h14,4,0,value);
        check(value[8] && !en && !pwm,"watchdog shuts down and latches");
        command(20,1);check(!en,"watchdog does not auto restart");
        command(0,4);command(20,1);repeat(15) @(negedge clk);
        driver_fault=1;repeat(5) @(negedge clk);
        read_reg(8'h14,0,0,value);check(value[9] && !en && !pwm,"external driver fault");
        command(0,4);read_reg(8'h14,0,0,value);check(value[9],"active fault cannot clear");
        driver_fault=0;repeat(5) @(negedge clk);command(0,4);command(20,1);
        repeat(15) @(negedge clk);estop=1;repeat(5) @(negedge clk);
        check(!en && !pwm,"estop disables");
        estop=0;repeat(5) @(negedge clk);command(0,4);
        command(0,5);read_reg(8'h14,0,0,value);check(value[10],"clear-plus-enable rejected");
        command(0,4);command(10,8);read_reg(8'h14,0,0,value);check(value[10],"reserved control bit rejected");
        write_reg(8'h00,0,4'hf,0,0,2,2);
        write_reg(8'h09,0,4'hf,0,2,0,2);
        read_reg(8'h01,2,2,value);read_reg(8'hfc,2,2,value);
        write_reg(12'h108,99,4'hf,0,0,0,2);
        read_reg(12'h100,0,2,value);
        // Reset aborts a half write and a stalled response; no latent enable.
        @(negedge clk);awaddr=8'h10;awvalid=1;
        @(posedge clk);@(negedge clk);awvalid=0;rst_n=0;
        repeat(3) @(negedge clk);check(!en && !pwm && !bvalid && !rvalid,"reset flushes AXI state");
        rst_n=1;repeat(3) @(negedge clk);
        read_reg(8'h14,0,0,value);check(value==0,"reset clears status");
        $display("PASS conveyor_axi: %0d checks",checks);$finish;
    end
endmodule

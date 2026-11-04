`timescale 1ns/1ps
module servo_watchdog_tb;
    localparam integer HZ=1000000, PERIOD=HZ/50, TIMEOUT=HZ/1000*80;
    reg clk=0,rst_n=0;
    reg [63:0] gpio=0;
    wire [5:0] out;
    wire [5:0] status;
    integer checks=0,j;
    reg [31:0] held;
    always #5 clk=~clk;
    arm_servo_pwm #(.CLK_HZ(HZ),.WATCHDOG_MS(80)) dut(clk,rst_n,gpio,out,status);
    task check(input bit ok,input string why);
        begin checks++;if(!ok)$fatal(1,"%s",why);end
    endtask
    task command(input bit enabled,input [7:0] position,input bit valid);
        begin
            @(negedge clk);
            gpio[63:56]=valid?8'ha6:8'ha5;
            gpio[48]=enabled;gpio[47:0]={6{position}};
            repeat(3)@(negedge clk);gpio[49]=!gpio[49];
            repeat(8)@(negedge clk);
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);rst_n=1;
        // A legacy client used A5 and disabled automatically on connection.
        // It must not clear the new protocol's manual-rearm latch.
        gpio={8'ha5,6'd0,1'b1,1'b0,48'd0};
        repeat(10)@(negedge clk);
        check(dut.link_lost&&!dut.active_enable,"legacy disable cannot clear new protection");
        command(1,64,1);repeat(PERIOD)@(negedge clk);
        check(out==0&&dut.link_lost,"boot needs disable/enable handshake");
        check(status[5:4]==2'b10&&status[2:0]==3'b001,"status identifies reset/rearm and inactive PWM");
        command(0,64,1);command(1,64,1);
        repeat(PERIOD*2)@(negedge clk);
        check(dut.active_enable&&!dut.link_lost,"enable after committed disable");
        check(status[3]==gpio[49]&&status[2:0]==3'b110,"status acknowledges accepted and applied enable");
        held=dut.pulse_ticks[0];check(held==500+64*7,"initial pulse width");
        for(j=0;j<10;j++)begin
            repeat(PERIOD)@(negedge clk);command(1,64,1);
            check(!dut.link_lost&&dut.pulse_ticks[0]==held,"idle heartbeat");
        end
        // Invalid commands cannot extend the lease.
        repeat(5)begin repeat(PERIOD)@(negedge clk);command(1,255,0);end
        check(dut.link_lost&&dut.active_enable,"timeout holds PWM enabled");
        check(status[2:0]==3'b111,"watchdog status reports actual active hold");
        for(j=0;j<6;j++)check(dut.pulse_ticks[j]==held,"timeout preserves all axes");
        command(1,255,1);repeat(PERIOD*2)@(negedge clk);
        check(dut.link_lost&&dut.pulse_ticks[0]==held,"late heartbeat cannot revive motion");
        command(0,255,1);repeat(PERIOD*2)@(negedge clk);
        check(!dut.link_lost&&!dut.active_enable&&out==0,"explicit disable");
        check(status[3]==gpio[49]&&status[2:0]==3'b000,"disable acknowledgement and output feedback");
        command(1,100,1);repeat(PERIOD*2)@(negedge clk);
        check(!dut.link_lost&&dut.active_enable&&dut.pulse_ticks[0]==1200,"explicit re-enable");
        // Reset while the enabled GPIO word remains must not auto-resume.
        #2;rst_n=0;#1;check(out==0,"reset clears outputs");
        repeat(5)@(negedge clk);rst_n=1;
        repeat(PERIOD*2)@(negedge clk);
        check(!dut.active_enable&&dut.link_lost,"stale GPIO after reset is rejected");
        $display("PASS servo_watchdog: %0d checks",checks);$finish;
    end
    initial begin #20000000;$fatal(1,"watchdog test timeout");end
endmodule

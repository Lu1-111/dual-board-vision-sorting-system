`timescale 1ns/1ps
module pwm_reset_tb;
    reg clk=0,rst_n=0;
    wire out;
    integer checks=0;
    always #5 clk=~clk;
    pwm #(.CLK_FREQ(100),.PWM_FREQ(1),.DUTY_CYCLE(50)) dut(clk,rst_n,out);
    task check(input bit ok,input string why);
        begin checks++;if(!ok)$fatal(1,"%s",why);end
    endtask
    task release_reset;
        begin
            @(negedge clk);#2;rst_n=1;
            @(posedge clk);#1;check(!dut.local_rst_n&&dut.counter==0,"first release edge");
            @(posedge clk);#1;check(dut.local_rst_n&&dut.counter==0,"second release edge");
            @(posedge clk);#1;check(dut.counter==1&&out,"counter resumes on local clock");
        end
    endtask
    initial begin
        #1;rst_n=1;#1;rst_n=0;#1;
        check(!out&&dut.counter==0,"async assertion");
        release_reset();
        repeat(7) begin
            @(negedge clk);#1;rst_n=0;#1;
            check(!out&&dut.counter==0,"short reset asserts immediately");
            release_reset();
        end
        $display("PASS pwm_reset: %0d checks",checks);$finish;
    end
endmodule

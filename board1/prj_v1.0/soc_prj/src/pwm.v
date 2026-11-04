`timescale 1ns / 1ns
module pwm #(
    parameter CLK_FREQ = 50000000, 
    parameter PWM_FREQ = 2,     
    parameter DUTY_CYCLE = 50     
)(
    input wire clk,               
    input wire rst_n,              
    output reg pwm_out            
);

    localparam PERIOD = CLK_FREQ / PWM_FREQ; 
    localparam DUTY_COUNT = (DUTY_CYCLE)*( PERIOD / 100);
    reg [31:0] counter;                    
    // rst_n can come from a video-domain lock indicator. Assert at once,
    // but release only after two edges of this PWM's own clock.
    (* async_reg = "true" *) reg [1:0] reset_release; /* fehdl keep="true" */
    wire local_rst_n = reset_release[1];
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) reset_release <= 2'b00;
        else reset_release <= {reset_release[0], 1'b1};
    end

    always @(posedge clk or negedge local_rst_n) begin
        if (!local_rst_n) begin
            counter <= 32'd0;
        end else begin
            if (counter < PERIOD) begin
                counter <= counter + 1;
            end else begin
                counter <= 32'd0;
            end
        end
    end

    
    always @(posedge clk or negedge local_rst_n) begin
        if (!local_rst_n) begin
            pwm_out <= 1'b0;
        end else begin
            if (counter < DUTY_COUNT) begin
                pwm_out <= 1'b1;
            end else begin
                pwm_out <= 1'b0;
            end
        end
    end

endmodule

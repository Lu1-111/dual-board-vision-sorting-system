`timescale 1ns / 1ps

// Six servo outputs driven by the PS PL-GPIO bus. GPIO bits 0..47 hold six
// unsigned 8-bit positions (axis 0 in the low byte). Bit 48 enables outputs,
// bit 49 toggles to commit a new pose, and bits 63..56 must equal 8'hA6.
module arm_servo_pwm #(
    parameter integer CLK_HZ = 50000000,
    parameter integer PWM_HZ = 50,
    parameter integer WATCHDOG_MS = 1000
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [63:0] ps_gpio,
    output wire [5:0]  servo_pwm,
    // PS GPIO 55..50: protocol signature, accepted commit, actual enable,
    // pending enable, rearm latch. This is control feedback, not shaft position.
    output wire [5:0]  status
);
    localparam integer PERIOD_TICKS = CLK_HZ / PWM_HZ;
    // The reference arm uses 500..2500 us pulses at a 20 ms frame rate.
    localparam integer MIN_TICKS = CLK_HZ / 2000;
    localparam integer STEP_TICKS = (CLK_HZ / 500) / 255;

    (* async_reg = "true" *) reg [63:0] gpio_meta; /* fehdl keep="true" */
    (* async_reg = "true" *) reg [63:0] gpio_sync; /* fehdl keep="true" */
    (* async_reg = "true" *) reg [2:0] commit_sync; /* fehdl keep="true" */
    reg [7:0] pending_position [0:5];
    reg pending_enable;
    reg [31:0] pulse_ticks [0:5];
    reg active_enable;
    reg [31:0] period_count;
    localparam integer WATCHDOG_TICKS = (CLK_HZ / 1000) * WATCHDOG_MS;
    reg [31:0] heartbeat_ticks;
    reg link_lost;
    reg accepted_commit;
    assign status = {2'b10, accepted_commit, active_enable, pending_enable, link_lost};
    wire valid_commit = (commit_sync[2] != commit_sync[1]) &&
                        (gpio_sync[63:56] == 8'hA6);
    integer i;

    // The PS holds all data stable before toggling commit. A third synchronizer
    // stage on commit ensures the synchronized data is ready when sampled.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            gpio_meta <= 64'd0;
            gpio_sync <= 64'd0;
            commit_sync <= 3'd0;
            pending_enable <= 1'b0;
            active_enable <= 1'b0;
            period_count <= 32'd0;
            heartbeat_ticks <= 0;
            // Also reject a stale enabled GPIO word after a PL reset.
            link_lost <= 1'b1;
            accepted_commit <= 1'b0;
            for (i = 0; i < 6; i = i + 1) begin
                pending_position[i] <= 8'd128;
                pulse_ticks[i] <= MIN_TICKS + 128 * STEP_TICKS;
            end
        end else begin
            gpio_meta <= ps_gpio;
            gpio_sync <= gpio_meta;
            commit_sync <= {commit_sync[1:0], ps_gpio[49]};

            // A fresh enabled command cannot clear a timeout. Keep the last
            // applied pulse widths until an explicit disable/re-enable cycle.
            if (valid_commit && (!link_lost || !gpio_sync[48])) begin
                for (i = 0; i < 6; i = i + 1)
                    pending_position[i] <= gpio_sync[i*8 +: 8];
                pending_enable <= gpio_sync[48];
                heartbeat_ticks <= 0;
                link_lost <= 1'b0;
                accepted_commit <= commit_sync[1];
            end else if (pending_enable && !link_lost) begin
                if (heartbeat_ticks >= WATCHDOG_TICKS - 1)
                    link_lost <= 1'b1;
                else heartbeat_ticks <= heartbeat_ticks + 1'b1;
            end

            if (period_count == PERIOD_TICKS - 1) begin
                period_count <= 32'd0;
                if (!link_lost) begin
                    active_enable <= pending_enable;
                    for (i = 0; i < 6; i = i + 1)
                        pulse_ticks[i] <= MIN_TICKS + pending_position[i] * STEP_TICKS;
                end
            end else begin
                period_count <= period_count + 1'b1;
            end
        end
    end

    genvar axis;
    generate for (axis = 0; axis < 6; axis = axis + 1) begin : g_servo
        assign servo_pwm[axis] = active_enable &&
                                 (period_count < pulse_ticks[axis]);
    end endgenerate
endmodule

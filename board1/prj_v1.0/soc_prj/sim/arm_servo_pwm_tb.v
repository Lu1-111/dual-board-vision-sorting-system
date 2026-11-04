`timescale 1ns / 1ps

module arm_servo_pwm_tb;
    localparam integer CLK_HZ = 5100000;
    localparam integer PERIOD = CLK_HZ / 50;
    localparam integer MIN_TICKS = CLK_HZ / 2000;
    localparam integer STEP_TICKS = (CLK_HZ / 500) / 255;
    reg clk = 1'b0;
    reg rst_n = 1'b0;
    reg [63:0] ps_gpio = 64'd0;
    wire [5:0] servo_pwm;
    integer high_count [0:5];
    integer i;
    integer axis;

    always #5 clk = ~clk;

    arm_servo_pwm #(.CLK_HZ(CLK_HZ)) dut (
        .clk(clk), .rst_n(rst_n), .ps_gpio(ps_gpio), .servo_pwm(servo_pwm)
    );

    task measure_frame;
        begin
            for (axis = 0; axis < 6; axis = axis + 1)
                high_count[axis] = 0;
            wait (dut.period_count == 0);
            for (i = 0; i < PERIOD; i = i + 1) begin
                @(negedge clk);
                for (axis = 0; axis < 6; axis = axis + 1)
                    if (servo_pwm[axis]) high_count[axis] = high_count[axis] + 1;
            end
        end
    endtask

    task expect_width;
        input integer axis_index;
        input integer expected;
        begin
            if (high_count[axis_index] != expected) begin
                $display("FAIL axis %0d: got %0d, expected %0d",
                         axis_index, high_count[axis_index], expected);
                $fatal(1);
            end
        end
    endtask

    initial begin
        repeat (4) @(negedge clk);
        if (servo_pwm !== 6'b0) $fatal(1, "outputs active during reset");
        rst_n = 1'b1;

        // GPIOBackend opens every session with a committed disable.
        ps_gpio = {8'hA6, 6'd0, 1'b1, 1'b0, 48'd0};
        repeat (8) @(negedge clk);
        ps_gpio[49] = 1'b0;
        repeat (8) @(negedge clk);

        // axis 0 = minimum, axis 1 = maximum, axes 2..5 = midpoint.
        ps_gpio = {8'hA6, 6'd0, 1'b1, 1'b1,
                   8'd128, 8'd128, 8'd128, 8'd128, 8'd255, 8'd0};
        repeat (PERIOD * 2) @(negedge clk);
        measure_frame();
        expect_width(0, MIN_TICKS);
        expect_width(1, MIN_TICKS + 255 * STEP_TICKS);
        for (axis = 2; axis < 6; axis = axis + 1)
            expect_width(axis, MIN_TICKS + 128 * STEP_TICKS);

        // New data without a commit toggle must not change the outputs.
        ps_gpio[7:0] = 8'd255;
        repeat (PERIOD) @(negedge clk);
        measure_frame();
        expect_width(0, MIN_TICKS);

        // A commit with a wrong command marker is ignored.
        ps_gpio[63:56] = 8'h00;
        ps_gpio[49] = 1'b0;
        repeat (PERIOD * 2) @(negedge clk);
        measure_frame();
        expect_width(0, MIN_TICKS);

        // A committed pose changes all six channels on the same frame edge.
        ps_gpio[63:56] = 8'hA6;
        ps_gpio[47:0] = {8'd255, 8'd224, 8'd192,
                         8'd128, 8'd64, 8'd0};
        ps_gpio[49] = 1'b1;
        repeat (PERIOD * 2) @(negedge clk);
        measure_frame();
        expect_width(0, MIN_TICKS);
        expect_width(1, MIN_TICKS + 64 * STEP_TICKS);
        expect_width(2, MIN_TICKS + 128 * STEP_TICKS);
        expect_width(3, MIN_TICKS + 192 * STEP_TICKS);
        expect_width(4, MIN_TICKS + 224 * STEP_TICKS);
        expect_width(5, MIN_TICKS + 255 * STEP_TICKS);

        // Committed disable takes effect at the next frame boundary.
        ps_gpio[48] = 1'b0;
        ps_gpio[49] = 1'b0;
        repeat (PERIOD * 2) @(negedge clk);
        measure_frame();
        for (axis = 0; axis < 6; axis = axis + 1)
            expect_width(axis, 0);
        $display("PASS arm_servo_pwm_tb");
        $finish;
    end
endmodule

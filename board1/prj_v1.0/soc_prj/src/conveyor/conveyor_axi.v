`timescale 1ns/1ps
// Independent AXI4-Lite slave. No connection to the arm's 64-bit GPIO protocol.
// BTS7960/IBT-2: forward RPWM=PWM, LPWM=0; reverse swaps them; both enables
// fall on stop/fault. AB encoder is decoded at x4. PS closes the speed loop.
// Reset must assert asynchronously and deassert synchronously to aclk.
module conveyor_axi #(
    parameter integer CLK_HZ = 50000000,
    parameter integer PWM_HZ = 20000,
    parameter integer WATCHDOG_MS = 250,
    parameter integer REVERSE_GAP_US = 1000,
    parameter integer BENCH_ONLY = 0,
    // Commissioning profile: no calibrated encoder; <=100% duty and <=1 s.
    // The continuous-run deadline is independent of the heartbeat watchdog.
    parameter integer JOG_ONLY = 0,
    // Integration capability, not proof of a healthy or calibrated sensor.
    parameter integer ENCODER_CONNECTED = 0,
    parameter integer JOG_LIMIT_MS = 1000
)(
    input wire aclk, input wire aresetn,
    input wire [11:0] s_axi_awaddr, input wire s_axi_awvalid,
    output wire s_axi_awready,
    input wire [31:0] s_axi_wdata, input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid, output wire s_axi_wready,
    output reg [1:0] s_axi_bresp, output reg s_axi_bvalid,
    input wire s_axi_bready,
    input wire [11:0] s_axi_araddr, input wire s_axi_arvalid,
    output wire s_axi_arready,
    output reg [31:0] s_axi_rdata, output reg [1:0] s_axi_rresp,
    output reg s_axi_rvalid, input wire s_axi_rready,
    input wire estop, input wire driver_fault,
    input wire encoder_a, input wire encoder_b,
    output wire motor_pwm, output wire motor_dir, output wire motor_enable,
    output wire bts_rpwm, output wire bts_lpwm,
    output wire bts_r_en, output wire bts_l_en
);
    localparam integer PERIOD = CLK_HZ / PWM_HZ;
    localparam integer WD_TICKS = (CLK_HZ / 1000) * WATCHDOG_MS;
    localparam integer GAP_TICKS = (CLK_HZ / 1000000) * REVERSE_GAP_US;
    localparam [63:0] JOG_TICKS = (64'd1 * CLK_HZ * JOG_LIMIT_MS) / 1000;
    reg [31:0] jog_count;
    reg aw_pending, w_pending;
    reg [11:0] awaddr;
    reg [31:0] wdata, shadow_duty, shadow_control;
    reg [3:0] wstrb;
    reg apply;
    reg [31:0] command_duty, command_control;
    reg [1:0] estop_sync, fault_sync;
    reg requested_enable, direction;
    reg [31:0] duty, phase, watchdog_count, gap_count;
    reg [2:0] faults; // watchdog, external fault, invalid command
    (* KEEP = "TRUE", ASYNC_REG = "TRUE" *) reg [1:0] enc_a_sync, enc_b_sync;
    reg [1:0] enc_previous;
    reg [2:0] enc_ready;
    reg [31:0] encoder_count, encoder_errors;
    reg [1:0] bench_ab;
    wire [1:0] enc_ab = {enc_a_sync[1], enc_b_sync[1]};
    wire external_fault = estop_sync[1] | fault_sync[1];
    wire running = requested_enable && (faults == 0) && !external_fault;
    wire jog_expired = JOG_ONLY && running && (jog_count >= JOG_TICKS-1);
    wire [31:0] status = {21'd0, faults, 5'd0, (gap_count != 0),
                          direction, motor_enable};
    assign motor_enable = aresetn && !BENCH_ONLY && running && (gap_count == 0);
    assign motor_pwm = motor_enable && (phase < duty);
    assign motor_dir = direction;
    assign bts_rpwm = motor_pwm && !direction;
    assign bts_lpwm = motor_pwm && direction;
    assign bts_r_en = motor_enable;
    assign bts_l_en = motor_enable;
    assign s_axi_awready = !aw_pending && !s_axi_bvalid;
    assign s_axi_wready = !w_pending && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid;

    function [31:0] merge_bytes;
        input [31:0] old_value, new_value;
        input [3:0] strobes;
        integer i;
        begin
            merge_bytes = old_value;
            for (i=0; i<4; i=i+1)
                if (strobes[i]) merge_bytes[i*8 +: 8] = new_value[i*8 +: 8];
        end
    endfunction

    // Separate address/data capture supports either AXI write arrival order.
    // Exactly one outstanding write and read; responses survive backpressure.
    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            aw_pending<=0; w_pending<=0; awaddr<=0; wdata<=0; wstrb<=0;
            s_axi_bvalid<=0; s_axi_bresp<=0; s_axi_rvalid<=0;
            s_axi_rdata<=0; s_axi_rresp<=0;
            shadow_duty<=0; shadow_control<=0;
            apply<=0; command_duty<=0; command_control<=0;
            bench_ab<=0;
        end else begin
            apply <= 0;
            if (s_axi_awready && s_axi_awvalid) begin
                awaddr<=s_axi_awaddr; aw_pending<=1;
            end
            if (s_axi_wready && s_axi_wvalid) begin
                wdata<=s_axi_wdata; wstrb<=s_axi_wstrb; w_pending<=1;
            end
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid<=0;
            if (aw_pending && w_pending && !s_axi_bvalid) begin
                aw_pending<=0; w_pending<=0; s_axi_bvalid<=1; s_axi_bresp<=0;
                case (awaddr)
                    8'h08: shadow_duty<=merge_bytes(shadow_duty,wdata,wstrb);
                    8'h0c: shadow_control<=merge_bytes(shadow_control,wdata,wstrb);
                    8'h10: begin
                        if ((wstrb == 4'hf) && (wdata == 32'hc0de0001)) begin
                            command_duty<=shadow_duty;
                            command_control<=shadow_control;
                            apply<=1;
                        end else s_axi_bresp<=2'b10;
                    end
                    8'h30: begin
                        if(BENCH_ONLY && wstrb[0] && (wdata[31:2]==0))bench_ab<=wdata[1:0];
                        else s_axi_bresp<=2'b10;
                    end
                    default: s_axi_bresp<=2'b10;
                endcase
            end
            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid<=0;
            if (s_axi_arready && s_axi_arvalid) begin
                s_axi_rvalid<=1; s_axi_rresp<=0;
                case (s_axi_araddr)
                    8'h00: s_axi_rdata<=32'h434e5601;
                    8'h04: s_axi_rdata<=PERIOD;
                    8'h08: s_axi_rdata<=shadow_duty;
                    8'h0c: s_axi_rdata<=shadow_control;
                    8'h14: s_axi_rdata<=status;
                    8'h18: s_axi_rdata<=WD_TICKS;
                    8'h1c: s_axi_rdata<=duty;
                    8'h20: s_axi_rdata<=CLK_HZ;
                    8'h24: s_axi_rdata<=encoder_count;
                    8'h28: s_axi_rdata<=encoder_errors;
                    // Bit 2 advertises full-duty jog support (legacy jog was 25%).
                    8'h2c: s_axi_rdata<=(BENCH_ONLY ? 32'd1 : 32'd0) | (JOG_ONLY ? 32'd6 : 32'd0) | ((ENCODER_CONNECTED && !BENCH_ONLY) ? 32'd16 : 32'd0) | ((JOG_ONLY && JOG_LIMIT_MS == 8000) ? 32'd8 : 32'd0);
                    8'h30: s_axi_rdata<={30'd0,bench_ab};
                    8'h34: s_axi_rdata<=JOG_LIMIT_MS;
                    default: begin s_axi_rdata<=0; s_axi_rresp<=2'b10; end
                endcase
            end
        end
    end

    // 00->01->11->10->00 counts +4. Counters wrap modulo 2^32.
    // Reject simultaneous two-bit changes instead of inventing a direction.
    // Synchronizers are not a substitute for input voltage conditioning.
    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            enc_a_sync<=0; enc_b_sync<=0; enc_previous<=0; enc_ready<=0;
            encoder_count<=0; encoder_errors<=0;
        end else begin
            enc_a_sync<={enc_a_sync[0],(BENCH_ONLY ? bench_ab[1] : encoder_a)};
            enc_b_sync<={enc_b_sync[0],(BENCH_ONLY ? bench_ab[0] : encoder_b)};
            enc_ready<={enc_ready[1:0],1'b1};
            enc_previous<=enc_ab;
            if (enc_ready[2]) begin
                case ({enc_previous,enc_ab})
                    4'b0001,4'b0111,4'b1110,4'b1000: encoder_count<=encoder_count+1'b1;
                    4'b0010,4'b1011,4'b1101,4'b0100: encoder_count<=encoder_count-1'b1;
                    4'b0000,4'b0101,4'b1010,4'b1111: begin end
                    default: encoder_errors<=encoder_errors+1'b1;
                endcase
            end
        end
    end

    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            estop_sync<=0; fault_sync<=0; requested_enable<=0; direction<=0;
            duty<=0; phase<=0; watchdog_count<=0; gap_count<=0; faults<=0; jog_count<=0;
        end else begin
            estop_sync<={estop_sync[0],estop};
            fault_sync<={fault_sync[0],driver_fault};
            if (phase == PERIOD-1) phase<=0;
            else phase<=phase+1'b1;
            if (gap_count != 0) gap_count<=gap_count-1'b1;
            if (JOG_ONLY && running) jog_count<=jog_count+1'b1;
            else jog_count<=0;
            if (running) begin
                if (watchdog_count >= WD_TICKS-1) begin
                    faults[0]<=1; requested_enable<=0;
                end else watchdog_count<=watchdog_count+1'b1;
            end else watchdog_count<=0;
            if (apply) begin
                watchdog_count<=0;
                if ((command_duty > PERIOD) || (command_control[31:3] != 0) ||
                    (command_control[2] && (command_control[0] || command_duty!=0))) begin
                    faults[2]<=1; requested_enable<=0; duty<=0;
                end else if (command_control[2]) begin
                    // Fault clearing never starts the motor.
                    if (!external_fault) faults<=0;
                    requested_enable<=0; duty<=0;
                end else if (!command_control[0]) begin
                    requested_enable<=0; duty<=0;
                end else if ((faults == 0) && !external_fault &&
                             !(running && watchdog_count >= WD_TICKS-1)) begin
                    // Zero speed is a disabled output, not an assumed brake mode.
                    requested_enable <= (command_duty != 0);
                    duty<=command_duty;
                    // Gap also applies on restart, including a rapid off/reverse/on.
                    if (!requested_enable || direction != command_control[1]) begin
                        direction<=command_control[1]; gap_count<=GAP_TICKS;
                        phase<=0;
                    end
                end
            end
            // Wins over a simultaneous heartbeat or reversal command.
            if (jog_expired) begin faults[2]<=1; requested_enable<=0; duty<=0; end
            // External fault always wins over APPLY, including a clear request.
            if (external_fault) begin faults[1]<=1; requested_enable<=0; duty<=0; end
        end
    end
endmodule

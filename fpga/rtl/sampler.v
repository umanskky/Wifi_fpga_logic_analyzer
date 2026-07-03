// ============================================================
// sampler.v
//
// Simple 8-channel digital sampler
//
// Version: v0.5 pre-trigger support
//
// Features:
//   - immediate capture
//   - triggered capture
//   - pre-trigger capture using circular buffer
//   - rising/falling edge trigger
//   - configurable trigger channel
//   - configurable sample-rate divider
//   - configurable sample count
//   - internal sample buffer
//   - sequential readout interface
//
// sample_rate_div:
//   sample period = sample_rate_div + 1 clk cycles
//
// Modes:
//
// 1. Immediate capture:
//      trigger_enable = 0
//      START -> capture N samples -> DONE
//
// 2. Triggered capture without pre-trigger:
//      trigger_enable = 1
//      pretrigger_count = 0
//      START -> ARMED -> trigger -> capture N samples -> DONE
//
// 3. Triggered capture with pre-trigger:
//      trigger_enable = 1
//      pretrigger_count > 0
//      START -> ARMED
//      ARMED writes samples into circular buffer
//      trigger -> capture post-trigger samples
//      readout starts from pre-trigger start address
// ============================================================

module sampler #(
    parameter SAMPLE_WIDTH = 8,
    parameter ADDR_WIDTH   = 12
)(
    input  wire                     clk,
    input  wire                     rst_n,

    // Digital input channels
    input  wire [SAMPLE_WIDTH-1:0]  sample_in,

    // Control
    input  wire                     start,
    input  wire                     clear,

    input  wire [15:0]              sample_rate_div,
    input  wire [15:0]              sample_count_target,

    // Trigger config
    input  wire                     trigger_enable,
    input  wire [2:0]               trigger_channel,
    input  wire                     trigger_edge,

    // Pre-trigger config
    input  wire [15:0]              pretrigger_count,

    // Status
    output reg                      busy,
    output reg                      armed,
    output reg                      done,
    output reg                      overflow,
    output reg  [15:0]              samples_captured,

    // Readout interface
    input  wire                     read_reset,
    input  wire                     read_en,
    output reg  [SAMPLE_WIDTH-1:0]  read_data,
    output reg                      read_valid,
    output reg                      read_empty
);

    // ------------------------------------------------------------
    // Constants
    // ------------------------------------------------------------
    localparam integer BUFFER_DEPTH = (1 << ADDR_WIDTH);
    localparam [15:0]  BUFFER_DEPTH_16 = (1 << ADDR_WIDTH);

    localparam [2:0] ST_IDLE      = 3'd0;
    localparam [2:0] ST_ARMED     = 3'd1;
    localparam [2:0] ST_CAPTURING = 3'd2;
    localparam [2:0] ST_DONE      = 3'd3;

    // ------------------------------------------------------------
    // Sample memory
    // ------------------------------------------------------------
    reg [SAMPLE_WIDTH-1:0] sample_mem [0:BUFFER_DEPTH-1];

    // ------------------------------------------------------------
    // Internal registers
    // ------------------------------------------------------------
    reg [2:0] state;

    reg [ADDR_WIDTH-1:0] wr_addr;
    reg [ADDR_WIDTH-1:0] rd_addr;
    reg [ADDR_WIDTH-1:0] read_start_addr;

    reg [15:0] div_cnt;
    reg [15:0] capture_limit;
    reg [15:0] read_count;

    // Number of valid samples collected before trigger
    reg [15:0] pre_samples_available;
    reg [15:0] actual_pretrigger_count;

    reg start_d;
    wire start_pulse;

    assign start_pulse = start & ~start_d;

    // ------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------
    function [15:0] min16;
        input [15:0] a;
        input [15:0] b;
        begin
            if (a < b) begin
                min16 = a;
            end else begin
                min16 = b;
            end
        end
    endfunction

    // ------------------------------------------------------------
    // Trigger edge detection
    // ------------------------------------------------------------
    reg [SAMPLE_WIDTH-1:0] sample_in_d;

    wire trigger_current;
    wire trigger_previous;

    assign trigger_current  = sample_in[trigger_channel];
    assign trigger_previous = sample_in_d[trigger_channel];

    wire trigger_rising;
    wire trigger_falling;
    wire trigger_hit;

    assign trigger_rising  = (trigger_previous == 1'b0) && (trigger_current == 1'b1);
    assign trigger_falling = (trigger_previous == 1'b1) && (trigger_current == 1'b0);

    assign trigger_hit = trigger_edge ? trigger_rising : trigger_falling;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sample_in_d <= {SAMPLE_WIDTH{1'b0}};
        end else begin
            sample_in_d <= sample_in;
        end
    end

    // ------------------------------------------------------------
    // Start edge detector
    // ------------------------------------------------------------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            start_d <= 1'b0;
        end else begin
            start_d <= start;
        end
    end

    // ------------------------------------------------------------
    // Capture state machine
    // ------------------------------------------------------------
    reg [15:0] pre_limit_tmp;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state                   <= ST_IDLE;

            busy                    <= 1'b0;
            armed                   <= 1'b0;
            done                    <= 1'b0;
            overflow                <= 1'b0;

            wr_addr                 <= {ADDR_WIDTH{1'b0}};
            read_start_addr         <= {ADDR_WIDTH{1'b0}};

            div_cnt                 <= 16'd0;
            capture_limit           <= 16'd0;
            samples_captured        <= 16'd0;

            pre_samples_available   <= 16'd0;
            actual_pretrigger_count <= 16'd0;
        end else begin

            if (clear) begin
                state                   <= ST_IDLE;

                busy                    <= 1'b0;
                armed                   <= 1'b0;
                done                    <= 1'b0;
                overflow                <= 1'b0;

                wr_addr                 <= {ADDR_WIDTH{1'b0}};
                read_start_addr         <= {ADDR_WIDTH{1'b0}};

                div_cnt                 <= 16'd0;
                capture_limit           <= 16'd0;
                samples_captured        <= 16'd0;

                pre_samples_available   <= 16'd0;
                actual_pretrigger_count <= 16'd0;
            end else begin

                case (state)

                    // ------------------------------------------------
                    // IDLE
                    // ------------------------------------------------
                    ST_IDLE: begin
                        busy  <= 1'b0;
                        armed <= 1'b0;

                        if (start_pulse) begin
                            done                    <= 1'b0;
                            overflow                <= 1'b0;

                            wr_addr                 <= {ADDR_WIDTH{1'b0}};
                            read_start_addr         <= {ADDR_WIDTH{1'b0}};

                            div_cnt                 <= 16'd0;
                            samples_captured        <= 16'd0;
                            pre_samples_available   <= 16'd0;
                            actual_pretrigger_count <= 16'd0;

                            // sample_count_target = 0 means full buffer
                            if (sample_count_target == 16'd0) begin
                                capture_limit <= BUFFER_DEPTH_16;
                            end else if (sample_count_target > BUFFER_DEPTH_16) begin
                                capture_limit <= BUFFER_DEPTH_16;
                                overflow      <= 1'b1;
                            end else begin
                                capture_limit <= sample_count_target;
                            end

                            if (trigger_enable) begin
                                state <= ST_ARMED;
                                armed <= 1'b1;
                                busy  <= 1'b0;
                            end else begin
                                state <= ST_CAPTURING;
                                armed <= 1'b0;
                                busy  <= 1'b1;
                            end
                        end
                    end

                    // ------------------------------------------------
                    // ARMED
                    //
                    // If pretrigger_count > 0, continuously write
                    // samples into circular buffer.
                    //
                    // If trigger_hit, compute read_start_addr and
                    // continue capturing post-trigger samples.
                    // ------------------------------------------------
                    ST_ARMED: begin
                        armed <= 1'b1;
                        busy  <= 1'b0;

                        if (trigger_hit) begin
                            // Clamp actual pretrigger count:
                            // actual_pre = min(pretrigger_count,
                            //                  pre_samples_available,
                            //                  capture_limit)
                            pre_limit_tmp = min16(pretrigger_count, pre_samples_available);
                            pre_limit_tmp = min16(pre_limit_tmp, capture_limit);

                            actual_pretrigger_count <= pre_limit_tmp;
                            samples_captured        <= pre_limit_tmp;

                            // wr_addr points to next write position.
                            // Start reading actual_pre samples before wr_addr.
                            read_start_addr <= wr_addr - pre_limit_tmp[ADDR_WIDTH-1:0];

                            // If all requested samples are already pre-trigger
                            // samples, finish immediately.
                            if (pre_limit_tmp >= capture_limit) begin
                                state <= ST_DONE;
                                armed <= 1'b0;
                                busy  <= 1'b0;
                                done  <= 1'b1;
                            end else begin
                                state   <= ST_CAPTURING;
                                armed   <= 1'b0;
                                busy    <= 1'b1;
                                div_cnt <= 16'd0;
                            end
                        end else begin
                            // Pre-trigger circular recording
                            if (pretrigger_count != 16'd0) begin
                                if (div_cnt == 16'd0) begin
                                    sample_mem[wr_addr] <= sample_in;

                                    wr_addr <= wr_addr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};

                                    if (pre_samples_available < BUFFER_DEPTH_16) begin
                                        pre_samples_available <= pre_samples_available + 16'd1;
                                    end

                                    div_cnt <= sample_rate_div;
                                end else begin
                                    div_cnt <= div_cnt - 16'd1;
                                end
                            end
                        end
                    end

                    // ------------------------------------------------
                    // CAPTURING
                    //
                    // Immediate mode:
                    //   read_start_addr = 0
                    //   samples_captured starts at 0
                    //
                    // Triggered mode:
                    //   read_start_addr points to first pre-trigger sample
                    //   samples_captured starts at actual_pretrigger_count
                    // ------------------------------------------------
                    ST_CAPTURING: begin
                        armed <= 1'b0;
                        busy  <= 1'b1;

                        if (div_cnt == 16'd0) begin
                            sample_mem[wr_addr] <= sample_in;

                            wr_addr          <= wr_addr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};
                            samples_captured <= samples_captured + 16'd1;

                            div_cnt <= sample_rate_div;

                            if ((samples_captured + 16'd1) >= capture_limit) begin
                                state <= ST_DONE;
                                busy  <= 1'b0;
                                done  <= 1'b1;
                            end
                        end else begin
                            div_cnt <= div_cnt - 16'd1;
                        end
                    end

                    // ------------------------------------------------
                    // DONE
                    // ------------------------------------------------
                    ST_DONE: begin
                        busy  <= 1'b0;
                        armed <= 1'b0;
                        done  <= 1'b1;

                        if (start_pulse) begin
                            done                    <= 1'b0;
                            overflow                <= 1'b0;

                            wr_addr                 <= {ADDR_WIDTH{1'b0}};
                            read_start_addr         <= {ADDR_WIDTH{1'b0}};

                            div_cnt                 <= 16'd0;
                            samples_captured        <= 16'd0;
                            pre_samples_available   <= 16'd0;
                            actual_pretrigger_count <= 16'd0;

                            if (sample_count_target == 16'd0) begin
                                capture_limit <= BUFFER_DEPTH_16;
                            end else if (sample_count_target > BUFFER_DEPTH_16) begin
                                capture_limit <= BUFFER_DEPTH_16;
                                overflow      <= 1'b1;
                            end else begin
                                capture_limit <= sample_count_target;
                            end

                            if (trigger_enable) begin
                                state <= ST_ARMED;
                                armed <= 1'b1;
                                busy  <= 1'b0;
                            end else begin
                                state <= ST_CAPTURING;
                                armed <= 1'b0;
                                busy  <= 1'b1;
                            end
                        end
                    end

                    default: begin
                        state <= ST_IDLE;
                    end

                endcase
            end
        end
    end

    // ------------------------------------------------------------
    // Readout logic
    //
    // For pre-trigger, read_start_addr may not be zero.
    // Readout walks sample_mem circularly from read_start_addr.
    // ------------------------------------------------------------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rd_addr    <= {ADDR_WIDTH{1'b0}};
            read_count <= 16'd0;

            read_data  <= {SAMPLE_WIDTH{1'b0}};
            read_valid <= 1'b0;
            read_empty <= 1'b1;
        end else begin
            read_valid <= 1'b0;

            if (clear || read_reset) begin
                rd_addr    <= read_start_addr;
                read_count <= 16'd0;

                read_data  <= {SAMPLE_WIDTH{1'b0}};
                read_valid <= 1'b0;

                if (samples_captured == 16'd0) begin
                    read_empty <= 1'b1;
                end else begin
                    read_empty <= 1'b0;
                end
            end else begin

                if (read_en) begin
                    if (read_count < samples_captured) begin
                        read_data  <= sample_mem[rd_addr];
                        read_valid <= 1'b1;

                        rd_addr    <= rd_addr + {{(ADDR_WIDTH-1){1'b0}}, 1'b1};
                        read_count <= read_count + 16'd1;

                        if ((read_count + 16'd1) >= samples_captured) begin
                            read_empty <= 1'b1;
                        end else begin
                            read_empty <= 1'b0;
                        end
                    end else begin
                        read_data  <= {SAMPLE_WIDTH{1'b0}};
                        read_valid <= 1'b0;
                        read_empty <= 1'b1;
                    end
                end else begin
                    if (read_count >= samples_captured) begin
                        read_empty <= 1'b1;
                    end else begin
                        read_empty <= 1'b0;
                    end
                end
            end
        end
    end

endmodule
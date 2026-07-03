// ============================================================
// spi_slave_regs.v
//
// SPI register interface + sampler integration
//
// Version: v0.5 pre-trigger support
//
// Target: Cyclone IV / DE0-Nano
//
// SPI mode: 0
// CPOL = 0, CPHA = 0
//
// Register map:
//   0x00 FPGA_ID
//   0x01 VERSION
//   0x02 STATUS
//   0x03 CONTROL
//   0x04 SAMPLE_RATE_DIV_L
//   0x05 SAMPLE_RATE_DIV_H
//   0x06 SAMPLE_COUNT_L
//   0x07 SAMPLE_COUNT_H
//   0x08 SAMPLES_DONE_L
//   0x09 SAMPLES_DONE_H
//   0x0A SAMPLER_DATA
//   0x0B TEST_PATTERN
//   0x0C INPUT_MODE
//   0x0D TRIGGER_CFG
//   0x0E PRETRIGGER_COUNT_L
//   0x0F PRETRIGGER_COUNT_H
//
// CONTROL:
//   bit 0 = start
//   bit 1 = clear
//   bit 2 = read_reset
//
// STATUS:
//   bit 0 = alive
//   bit 1 = busy
//   bit 2 = done
//   bit 3 = overflow
//   bit 4 = read_empty
//   bit 5 = read_valid
//   bit 6 = armed
//
// TRIGGER_CFG:
//   bit 0    = trigger_enable
//   bit 1    = trigger_edge, 0 falling, 1 rising
//   bits 4:2 = trigger_channel
// ============================================================

module spi_slave_regs (
    input  wire clk,
    input  wire rst_n,

    // SPI pins
    input  wire spi_sck,
    input  wire spi_cs_n,
    input  wire spi_mosi,
    output wire spi_miso,

    // Logic analyzer external inputs
    input  wire [7:0] logic_inputs,

    // Debug / external register outputs
    output reg  [7:0] control_reg,
    output wire [7:0] status_reg
);

    // ------------------------------------------------------------
    // Register map
    // ------------------------------------------------------------
    localparam [6:0] REG_FPGA_ID            = 7'h00;
    localparam [6:0] REG_VERSION            = 7'h01;
    localparam [6:0] REG_STATUS             = 7'h02;
    localparam [6:0] REG_CONTROL            = 7'h03;

    localparam [6:0] REG_SAMPLE_RATE_DIV_L  = 7'h04;
    localparam [6:0] REG_SAMPLE_RATE_DIV_H  = 7'h05;

    localparam [6:0] REG_SAMPLE_COUNT_L     = 7'h06;
    localparam [6:0] REG_SAMPLE_COUNT_H     = 7'h07;

    localparam [6:0] REG_SAMPLES_DONE_L     = 7'h08;
    localparam [6:0] REG_SAMPLES_DONE_H     = 7'h09;

    localparam [6:0] REG_SAMPLER_DATA       = 7'h0A;
    localparam [6:0] REG_TEST_PATTERN       = 7'h0B;
    localparam [6:0] REG_INPUT_MODE         = 7'h0C;
    localparam [6:0] REG_TRIGGER_CFG        = 7'h0D;
    localparam [6:0] REG_PRETRIGGER_COUNT_L = 7'h0E;
    localparam [6:0] REG_PRETRIGGER_COUNT_H = 7'h0F;

    localparam [7:0] FPGA_ID_VALUE          = 8'hA5;
    localparam [7:0] VERSION_VALUE          = 8'h05;

    // ------------------------------------------------------------
    // User registers
    // ------------------------------------------------------------
    reg [15:0] sample_rate_div_reg;
    reg [15:0] sample_count_target_reg;
    reg [7:0]  test_pattern_reg;
    reg [7:0]  input_mode_reg;
    reg [7:0]  trigger_cfg_reg;
    reg [15:0] pretrigger_count_reg;

    wire [7:0] sampler_input_mux;

    assign sampler_input_mux = input_mode_reg[0] ? logic_inputs : test_pattern_reg;

    wire       trigger_enable;
    wire       trigger_edge;
    wire [2:0] trigger_channel;

    assign trigger_enable  = trigger_cfg_reg[0];
    assign trigger_edge    = trigger_cfg_reg[1];
    assign trigger_channel = trigger_cfg_reg[4:2];

    // ------------------------------------------------------------
    // Sampler wires / pulses
    // ------------------------------------------------------------
    reg sampler_start_pulse;
    reg sampler_clear_pulse;
    reg sampler_read_reset_pulse;
    reg sampler_read_en_pulse;

    wire        sampler_busy;
    wire        sampler_armed;
    wire        sampler_done;
    wire        sampler_overflow;
    wire [15:0] sampler_samples_captured;

    wire [7:0]  sampler_read_data;
    wire        sampler_read_valid;
    wire        sampler_read_empty;

    sampler #(
        .SAMPLE_WIDTH(8),
        .ADDR_WIDTH(12)
    ) u_sampler (
        .clk                 (clk),
        .rst_n               (rst_n),

        .sample_in           (sampler_input_mux),

        .start               (sampler_start_pulse),
        .clear               (sampler_clear_pulse),

        .sample_rate_div     (sample_rate_div_reg),
        .sample_count_target (sample_count_target_reg),

        .trigger_enable      (trigger_enable),
        .trigger_channel     (trigger_channel),
        .trigger_edge        (trigger_edge),

        .pretrigger_count    (pretrigger_count_reg),

        .busy                (sampler_busy),
        .armed               (sampler_armed),
        .done                (sampler_done),
        .overflow            (sampler_overflow),
        .samples_captured    (sampler_samples_captured),

        .read_reset          (sampler_read_reset_pulse),
        .read_en             (sampler_read_en_pulse),
        .read_data           (sampler_read_data),
        .read_valid          (sampler_read_valid),
        .read_empty          (sampler_read_empty)
    );

    assign status_reg = {
        1'b0,                 // bit 7 reserved
        sampler_armed,        // bit 6
        sampler_read_valid,   // bit 5
        sampler_read_empty,   // bit 4
        sampler_overflow,     // bit 3
        sampler_done,         // bit 2
        sampler_busy,         // bit 1
        1'b1                  // bit 0 alive
    };

    // ------------------------------------------------------------
    // Synchronize SPI signals to clk domain
    // ------------------------------------------------------------
    reg [2:0] sck_sync;
    reg [2:0] cs_sync;
    reg [2:0] mosi_sync;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sck_sync  <= 3'b000;
            cs_sync   <= 3'b111;
            mosi_sync <= 3'b000;
        end else begin
            sck_sync  <= {sck_sync[1:0], spi_sck};
            cs_sync   <= {cs_sync[1:0], spi_cs_n};
            mosi_sync <= {mosi_sync[1:0], spi_mosi};
        end
    end

    wire sck_rising  = (sck_sync[2:1] == 2'b01);
    wire sck_falling = (sck_sync[2:1] == 2'b10);

    wire cs_active   = (cs_sync[2] == 1'b0);
    wire cs_start    = (cs_sync[2:1] == 2'b10);
    wire cs_end      = (cs_sync[2:1] == 2'b01);

    wire mosi_sample = mosi_sync[2];

    // ------------------------------------------------------------
    // SPI internal state
    // ------------------------------------------------------------
    reg [2:0] bit_cnt;
    reg [7:0] rx_shift;
    reg [7:0] tx_shift;

    reg       command_phase;
    reg       rw_read;
    reg [6:0] reg_addr;

    reg       skip_next_falling;

    assign spi_miso = (!spi_cs_n) ? tx_shift[7] : 1'bZ;

    // ------------------------------------------------------------
    // Special handling for SAMPLER_DATA read
    // ------------------------------------------------------------
    reg       sampler_data_read_pending;
    reg [2:0] sampler_data_wait;

    // ------------------------------------------------------------
    // Register read function
    // ------------------------------------------------------------
    function [7:0] read_register;
        input [6:0] addr;
        begin
            case (addr)
                REG_FPGA_ID:            read_register = FPGA_ID_VALUE;
                REG_VERSION:            read_register = VERSION_VALUE;
                REG_STATUS:             read_register = status_reg;
                REG_CONTROL:            read_register = control_reg;

                REG_SAMPLE_RATE_DIV_L:  read_register = sample_rate_div_reg[7:0];
                REG_SAMPLE_RATE_DIV_H:  read_register = sample_rate_div_reg[15:8];

                REG_SAMPLE_COUNT_L:     read_register = sample_count_target_reg[7:0];
                REG_SAMPLE_COUNT_H:     read_register = sample_count_target_reg[15:8];

                REG_SAMPLES_DONE_L:     read_register = sampler_samples_captured[7:0];
                REG_SAMPLES_DONE_H:     read_register = sampler_samples_captured[15:8];

                REG_SAMPLER_DATA:       read_register = sampler_read_data;

                REG_TEST_PATTERN:       read_register = test_pattern_reg;
                REG_INPUT_MODE:         read_register = input_mode_reg;
                REG_TRIGGER_CFG:        read_register = trigger_cfg_reg;

                REG_PRETRIGGER_COUNT_L: read_register = pretrigger_count_reg[7:0];
                REG_PRETRIGGER_COUNT_H: read_register = pretrigger_count_reg[15:8];

                default:                read_register = 8'h00;
            endcase
        end
    endfunction

    // ------------------------------------------------------------
    // Main SPI logic
    // ------------------------------------------------------------
    reg [7:0] rx_byte_next;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            bit_cnt                   <= 3'd0;
            rx_shift                  <= 8'h00;
            tx_shift                  <= 8'h00;

            command_phase             <= 1'b1;
            rw_read                   <= 1'b0;
            reg_addr                  <= 7'h00;

            skip_next_falling         <= 1'b0;

            control_reg               <= 8'h00;
            sample_rate_div_reg       <= 16'd49;
            sample_count_target_reg   <= 16'd16;
            test_pattern_reg          <= 8'h55;
            input_mode_reg            <= 8'h00;
            trigger_cfg_reg           <= 8'h00;
            pretrigger_count_reg      <= 16'd0;

            sampler_start_pulse       <= 1'b0;
            sampler_clear_pulse       <= 1'b0;
            sampler_read_reset_pulse  <= 1'b0;
            sampler_read_en_pulse     <= 1'b0;

            sampler_data_read_pending <= 1'b0;
            sampler_data_wait         <= 3'd0;
        end else begin
            // Default pulse values
            sampler_start_pulse      <= 1'b0;
            sampler_clear_pulse      <= 1'b0;
            sampler_read_reset_pulse <= 1'b0;
            sampler_read_en_pulse    <= 1'b0;

            // ----------------------------------------------------
            // Delayed load for SAMPLER_DATA read
            // ----------------------------------------------------
            if (sampler_data_read_pending) begin
                if (sampler_data_wait != 3'd0) begin
                    sampler_data_wait <= sampler_data_wait - 3'd1;
                end else begin
                    tx_shift                  <= sampler_read_data;
                    sampler_data_read_pending <= 1'b0;
                    skip_next_falling         <= 1'b1;
                end
            end

            // ----------------------------------------------------
            // New transaction
            // ----------------------------------------------------
            if (cs_start) begin
                bit_cnt                   <= 3'd0;
                rx_shift                  <= 8'h00;
                tx_shift                  <= 8'h00;
                command_phase             <= 1'b1;
                rw_read                   <= 1'b0;
                reg_addr                  <= 7'h00;
                skip_next_falling         <= 1'b0;
                sampler_data_read_pending <= 1'b0;
                sampler_data_wait         <= 3'd0;
            end

            // ----------------------------------------------------
            // End transaction
            // ----------------------------------------------------
            if (cs_end) begin
                bit_cnt                   <= 3'd0;
                rx_shift                  <= 8'h00;
                tx_shift                  <= 8'h00;
                command_phase             <= 1'b1;
                skip_next_falling         <= 1'b0;
                sampler_data_read_pending <= 1'b0;
                sampler_data_wait         <= 3'd0;
            end

            if (cs_active) begin

                // ------------------------------------------------
                // MOSI sampled on rising edge
                // ------------------------------------------------
                if (sck_rising) begin
                    rx_byte_next = {rx_shift[6:0], mosi_sample};

                    if (bit_cnt == 3'd7) begin
                        bit_cnt  <= 3'd0;
                        rx_shift <= 8'h00;

                        if (command_phase) begin
                            rw_read       <= rx_byte_next[7];
                            reg_addr      <= rx_byte_next[6:0];
                            command_phase <= 1'b0;

                            if (rx_byte_next[7]) begin
                                if (rx_byte_next[6:0] == REG_SAMPLER_DATA) begin
                                    sampler_read_en_pulse     <= 1'b1;
                                    sampler_data_read_pending <= 1'b1;
                                    sampler_data_wait         <= 3'd2;
                                    tx_shift                  <= 8'h00;
                                end else begin
                                    tx_shift          <= read_register(rx_byte_next[6:0]);
                                    skip_next_falling <= 1'b1;
                                end
                            end else begin
                                tx_shift          <= 8'h00;
                                skip_next_falling <= 1'b0;
                            end
                        end else begin
                            if (!rw_read) begin
                                case (reg_addr)
                                    REG_CONTROL: begin
                                        control_reg <= rx_byte_next;

                                        if (rx_byte_next[0]) begin
                                            sampler_start_pulse <= 1'b1;
                                        end

                                        if (rx_byte_next[1]) begin
                                            sampler_clear_pulse <= 1'b1;
                                        end

                                        if (rx_byte_next[2]) begin
                                            sampler_read_reset_pulse <= 1'b1;
                                        end
                                    end

                                    REG_SAMPLE_RATE_DIV_L: begin
                                        sample_rate_div_reg[7:0] <= rx_byte_next;
                                    end

                                    REG_SAMPLE_RATE_DIV_H: begin
                                        sample_rate_div_reg[15:8] <= rx_byte_next;
                                    end

                                    REG_SAMPLE_COUNT_L: begin
                                        sample_count_target_reg[7:0] <= rx_byte_next;
                                    end

                                    REG_SAMPLE_COUNT_H: begin
                                        sample_count_target_reg[15:8] <= rx_byte_next;
                                    end

                                    REG_TEST_PATTERN: begin
                                        test_pattern_reg <= rx_byte_next;
                                    end

                                    REG_INPUT_MODE: begin
                                        input_mode_reg <= rx_byte_next;
                                    end

                                    REG_TRIGGER_CFG: begin
                                        trigger_cfg_reg <= rx_byte_next;
                                    end

                                    REG_PRETRIGGER_COUNT_L: begin
                                        pretrigger_count_reg[7:0] <= rx_byte_next;
                                    end

                                    REG_PRETRIGGER_COUNT_H: begin
                                        pretrigger_count_reg[15:8] <= rx_byte_next;
                                    end

                                    default: begin
                                        // Ignore writes to read-only or unknown registers
                                    end
                                endcase
                            end

                            command_phase     <= 1'b1;
                            skip_next_falling <= 1'b0;
                        end
                    end else begin
                        bit_cnt  <= bit_cnt + 3'd1;
                        rx_shift <= rx_byte_next;
                    end
                end

                // ------------------------------------------------
                // MISO shifted on falling edge
                // ------------------------------------------------
                if (sck_falling) begin
                    if (skip_next_falling) begin
                        skip_next_falling <= 1'b0;
                    end else begin
                        tx_shift <= {tx_shift[6:0], 1'b0};
                    end
                end
            end
        end
    end

endmodule
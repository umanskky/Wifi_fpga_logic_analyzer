// ============================================================
// de0_nano_top.v
//
// DE0-Nano top-level for SPI + sampler bring-up
//
// Clean channel mapping:
//
//   LA_CH[0] = physical gpio8
//   LA_CH[1] = physical gpio9
//   LA_CH[2] = physical gpio10
//   LA_CH[3] = physical gpio11
//   LA_CH[4] = physical gpio12
//   LA_CH[5] = physical gpio13
//   LA_CH[6] = physical gpio14
//   LA_CH[7] = physical gpio15
// ============================================================

module de0_nano_top (
    input  wire        CLOCK_50,
    input  wire [1:0]  KEY,
    output wire [7:0]  LED,

    // SPI pins
    input  wire        FPGA_SPI_SCK,
    input  wire        FPGA_SPI_CS_N,
    input  wire        FPGA_SPI_MOSI,
    output wire        FPGA_SPI_MISO,

    // Logic analyzer channels
    input  wire [7:0]  LA_CH
);

    // KEY[0] is active-low reset
    wire rst_n;
    assign rst_n = KEY[0];

    wire [7:0] control_reg;
    wire [7:0] status_reg;

    spi_slave_regs u_spi_slave_regs (
        .clk          (CLOCK_50),
        .rst_n        (rst_n),

        .spi_sck      (FPGA_SPI_SCK),
        .spi_cs_n     (FPGA_SPI_CS_N),
        .spi_mosi     (FPGA_SPI_MOSI),
        .spi_miso     (FPGA_SPI_MISO),

        .logic_inputs (LA_CH),

        .control_reg  (control_reg),
        .status_reg   (status_reg)
    );

    // Debug:
    // LED0 = alive
    // LED1 = busy
    // LED2 = done
    // LED3 = overflow
    // LED4 = read_empty
    // LED5 = read_valid
    assign LED = status_reg;

endmodule
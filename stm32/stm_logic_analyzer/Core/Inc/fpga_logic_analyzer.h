#ifndef FPGA_LOGIC_ANALYZER_H
#define FPGA_LOGIC_ANALYZER_H

#include "fpga_spi.h"
#include <stdint.h>

typedef enum
{
    FPGA_LA_INPUT_TEST_PATTERN = 0,
    FPGA_LA_INPUT_EXTERNAL     = 1
} fpga_la_input_mode_t;

typedef enum
{
    FPGA_LA_TRIGGER_FALLING = 0,
    FPGA_LA_TRIGGER_RISING  = 1
} fpga_la_trigger_edge_t;

typedef struct
{
    fpga_spi_t *spi;
} fpga_la_t;

typedef struct
{
    fpga_la_input_mode_t input_mode;

    uint16_t sample_rate_div;
    uint16_t sample_count;
    uint16_t pretrigger_count;

    uint8_t trigger_enable;
    uint8_t trigger_channel;
    fpga_la_trigger_edge_t trigger_edge;

} fpga_la_capture_config_t;

HAL_StatusTypeDef fpga_la_init(
    fpga_la_t *la,
    fpga_spi_t *spi
);

HAL_StatusTypeDef fpga_la_check_version(
    fpga_la_t *la
);

HAL_StatusTypeDef fpga_la_set_input_mode(
    fpga_la_t *la,
    fpga_la_input_mode_t mode
);

HAL_StatusTypeDef fpga_la_set_test_pattern(
    fpga_la_t *la,
    uint8_t pattern
);

HAL_StatusTypeDef fpga_la_set_sample_rate_div(
    fpga_la_t *la,
    uint16_t div
);

HAL_StatusTypeDef fpga_la_set_sample_count(
    fpga_la_t *la,
    uint16_t count
);

HAL_StatusTypeDef fpga_la_set_pretrigger_count(
    fpga_la_t *la,
    uint16_t count
);

HAL_StatusTypeDef fpga_la_set_trigger(
    fpga_la_t *la,
    uint8_t enable,
    uint8_t channel,
    fpga_la_trigger_edge_t edge
);

HAL_StatusTypeDef fpga_la_clear(
    fpga_la_t *la
);

HAL_StatusTypeDef fpga_la_start(
    fpga_la_t *la
);

HAL_StatusTypeDef fpga_la_read_reset(
    fpga_la_t *la
);

HAL_StatusTypeDef fpga_la_get_status(
    fpga_la_t *la,
    uint8_t *status
);

HAL_StatusTypeDef fpga_la_wait_armed_or_done(
    fpga_la_t *la,
    uint32_t timeout,
    uint8_t *final_status
);

HAL_StatusTypeDef fpga_la_wait_done(
    fpga_la_t *la,
    uint32_t timeout,
    uint8_t *final_status
);

HAL_StatusTypeDef fpga_la_get_captured_count(
    fpga_la_t *la,
    uint16_t *count
);

HAL_StatusTypeDef fpga_la_read_samples(
    fpga_la_t *la,
    uint8_t *buffer,
    uint16_t count
);

HAL_StatusTypeDef fpga_la_capture_immediate(
    fpga_la_t *la,
    fpga_la_input_mode_t input_mode,
    uint16_t sample_rate_div,
    uint16_t sample_count,
    uint8_t *buffer,
    uint16_t buffer_size,
    uint16_t *samples_read
);

HAL_StatusTypeDef fpga_la_capture_triggered(
    fpga_la_t *la,
    uint16_t sample_rate_div,
    uint16_t sample_count,
    uint16_t pretrigger_count,
    uint8_t trigger_channel,
    fpga_la_trigger_edge_t trigger_edge,
    uint8_t *buffer,
    uint16_t buffer_size,
    uint16_t *samples_read
);

HAL_StatusTypeDef fpga_la_capture(
    fpga_la_t *la,
    const fpga_la_capture_config_t *cfg,
    uint8_t *buffer,
    uint16_t buffer_size,
    uint16_t *samples_read
);
		
#endif
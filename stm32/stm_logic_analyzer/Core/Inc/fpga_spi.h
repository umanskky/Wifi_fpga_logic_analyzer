#ifndef FPGA_SPI_H
#define FPGA_SPI_H

#include "main.h"
#include <stdint.h>

#define FPGA_REG_ID                  0x00
#define FPGA_REG_VERSION             0x01
#define FPGA_REG_STATUS              0x02
#define FPGA_REG_CONTROL             0x03

#define FPGA_REG_SAMPLE_RATE_DIV_L   0x04
#define FPGA_REG_SAMPLE_RATE_DIV_H   0x05

#define FPGA_REG_SAMPLE_COUNT_L      0x06
#define FPGA_REG_SAMPLE_COUNT_H      0x07

#define FPGA_REG_SAMPLES_DONE_L      0x08
#define FPGA_REG_SAMPLES_DONE_H      0x09

#define FPGA_REG_SAMPLER_DATA        0x0A
#define FPGA_REG_TEST_PATTERN        0x0B
#define FPGA_REG_INPUT_MODE          0x0C
#define FPGA_REG_TRIGGER_CFG         0x0D

#define FPGA_REG_PRETRIGGER_COUNT_L  0x0E
#define FPGA_REG_PRETRIGGER_COUNT_H  0x0F

#define FPGA_EXPECTED_ID             0xA5
#define FPGA_EXPECTED_VERSION        0x05

#define FPGA_CONTROL_START           0x01
#define FPGA_CONTROL_CLEAR           0x02
#define FPGA_CONTROL_READ_RESET      0x04

#define FPGA_STATUS_ALIVE            0x01
#define FPGA_STATUS_BUSY             0x02
#define FPGA_STATUS_DONE             0x04
#define FPGA_STATUS_OVERFLOW         0x08
#define FPGA_STATUS_READ_EMPTY       0x10
#define FPGA_STATUS_READ_VALID       0x20
#define FPGA_STATUS_ARMED            0x40

#define FPGA_INPUT_MODE_TEST_PATTERN 0x00
#define FPGA_INPUT_MODE_EXTERNAL     0x01

#define FPGA_TRIGGER_ENABLE          0x01
#define FPGA_TRIGGER_EDGE_RISING     0x02
#define FPGA_TRIGGER_EDGE_FALLING    0x00
#define FPGA_TRIGGER_CH_SHIFT        2


typedef struct
{
    SPI_HandleTypeDef *hspi;

    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
} fpga_spi_t;

HAL_StatusTypeDef fpga_init(
    fpga_spi_t *dev,
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *cs_port,
    uint16_t cs_pin
);

HAL_StatusTypeDef fpga_read_reg(
    fpga_spi_t *dev,
    uint8_t addr,
    uint8_t *data
);

HAL_StatusTypeDef fpga_write_reg(
    fpga_spi_t *dev,
    uint8_t addr,
    uint8_t data
);

HAL_StatusTypeDef fpga_check_id(fpga_spi_t *dev);

#endif
#include "fpga_spi.h"

static void fpga_cs_low(fpga_spi_t *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

static void fpga_cs_high(fpga_spi_t *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

static void fpga_delay_short(void)
{
    for (volatile uint32_t i = 0; i < 10000; i++) {
        __NOP();
    }
}

HAL_StatusTypeDef fpga_init(
    fpga_spi_t *dev,
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *cs_port,
    uint16_t cs_pin
)
{
    if (dev == NULL || hspi == NULL || cs_port == NULL)
    {
        return HAL_ERROR;
    }

    dev->hspi = hspi;
    dev->cs_port = cs_port;
    dev->cs_pin = cs_pin;

    fpga_cs_high(dev);

    return HAL_OK;
}

HAL_StatusTypeDef fpga_read_reg(
    fpga_spi_t *dev,
    uint8_t addr,
    uint8_t *data
)
{
    if (dev == NULL || data == NULL) {
        return HAL_ERROR;
    }

    uint8_t tx_cmd = 0x80u | (addr & 0x7Fu);
    uint8_t tx_dummy = 0x00u;

    uint8_t rx_dummy = 0x00u;
    uint8_t rx_data = 0x00u;

    HAL_StatusTypeDef st;

    fpga_cs_low(dev);
    fpga_delay_short();

    // Byte 0: command
    st = HAL_SPI_TransmitReceive(
        dev->hspi,
        &tx_cmd,
        &rx_dummy,
        1,
        100
    );

    if (st != HAL_OK) {
        fpga_cs_high(dev);
        return st;
    }

    // Give FPGA time to decode command and load tx_shift
    fpga_delay_short();

    // Byte 1: read data
    st = HAL_SPI_TransmitReceive(
        dev->hspi,
        &tx_dummy,
        &rx_data,
        1,
        100
    );

    fpga_delay_short();
    fpga_cs_high(dev);

    if (st != HAL_OK) {
        return st;
    }

    *data = rx_data;
    return HAL_OK;
}

HAL_StatusTypeDef fpga_write_reg(
    fpga_spi_t *dev,
    uint8_t addr,
    uint8_t data
)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    uint8_t tx_addr = addr & 0x7Fu;
    uint8_t tx_data = data;

    uint8_t rx_dummy = 0x00u;

    HAL_StatusTypeDef st;

    fpga_cs_low(dev);
    fpga_delay_short();

    // Byte 0: register address
    st = HAL_SPI_TransmitReceive(
        dev->hspi,
        &tx_addr,
        &rx_dummy,
        1,
        100
    );

    if (st != HAL_OK) {
        fpga_cs_high(dev);
        return st;
    }

    fpga_delay_short();

    // Byte 1: data
    st = HAL_SPI_TransmitReceive(
        dev->hspi,
        &tx_data,
        &rx_dummy,
        1,
        100
    );

    fpga_delay_short();
    fpga_cs_high(dev);

    return st;
}

HAL_StatusTypeDef fpga_check_id(fpga_spi_t *dev)
{
    uint8_t id = 0x00;

    HAL_StatusTypeDef st = fpga_read_reg(dev, FPGA_REG_ID, &id);

    if (st != HAL_OK)
    {
        return st;
    }

    if (id != FPGA_EXPECTED_ID)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}
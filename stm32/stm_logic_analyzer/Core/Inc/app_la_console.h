#ifndef APP_LA_CONSOLE_H
#define APP_LA_CONSOLE_H

#include "main.h"
#include <stdint.h>

void app_la_init(
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *fpga_cs_port,
    uint16_t fpga_cs_pin,
    UART_HandleTypeDef *debug_uart,
    UART_HandleTypeDef *esp_uart
);

void app_la_start(void);
void app_la_process(void);
void app_la_uart_rx_cplt_callback(UART_HandleTypeDef *huart);

#endif

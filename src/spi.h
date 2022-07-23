#ifndef _HW_SPI_H
#define _HW_SPI_H

#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include "gpio.h"


typedef struct
{
    SPI_HandleTypeDef hspi;
    Gpio_t Nss;   // First character is upper-case for compatiblity with LoRaMac-node
    Gpio_t mosi;
    Gpio_t miso;
    Gpio_t sclk;
} Spi_t;


//! @brief Initialize SPI channel
//! @param[in] speed SPI communication speed [hz]

void spi_init(Spi_t *spi, uint32_t speed);

//! @brief Deinitialize SPI channel

void spi_deinit(Spi_t *spi);

//! @brief Initialize the SPI IOs

void spi_io_init(Spi_t *spi);

//! @brief Deinitialize the SPI IOs

void spi_io_deinit(Spi_t *spi);


uint16_t SpiInOut(Spi_t *obj, uint16_t outData);

#endif // _HW_SPI_H

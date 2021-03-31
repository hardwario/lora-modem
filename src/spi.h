#ifndef _HW_SPI_H
#define _HW_SPI_H

#include "common.h"

//! @brief Initialize SPI channel
//! @param[in] speed SPI communication speed [hz]

void spi_init(uint32_t speed);

//! @brief Deinitialize SPI channel

void spi_deinit(void);

//! @brief Initialize the SPI IOs

void spi_io_init(void);

//! @brief Deinitialize the SPI IOs

void spi_io_deinit(void);

//! @brief outData and receives inData
//! @param [IN] tx Byte to be sent
//! @retval Received byte.

uint8_t spi_transfer(uint8_t tx);

#endif // _HW_SPI_H

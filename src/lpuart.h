#ifndef __VCOM_H
#define __VCOM_H

#include "common.h"

#ifndef UART_BAUDRATE
#define UART_BAUDRATE 9600
#endif

//! @brief  init vcom
//! @param  callback when Tx buffer has been sent

void lpuart_init(void (*Txcb)(void));

//! @brief  Init receiver of vcom
//! @param  callback When Rx char is received

void lpuart_set_rx_callback(void (*RxCb)(uint8_t *rxChar));

//! @brief  Write buffer data in dma mode
//! @param  buffer pointer to buffer
//! @param  size of buffer p_data to be sent

void lpuart_async_write(uint8_t *buffer, uint16_t size);

//! @brief  DeInit

void lpuart_deinit(void);

//!@brief  Init IO

void vcom_io_init(void);

//! @brief  DeInit IO

vcom_io_deinit(void);

#endif // __VCOM_H

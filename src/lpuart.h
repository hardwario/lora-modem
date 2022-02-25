#ifndef __LPUART_H
#define __LPUART_H

#include "common.h"


//! @brief Init lpuart
//! @param[in] callback when Tx buffer has been sent

void lpuart_init(unsigned int baudrate, void (*Txcb)(void));

//! @brief Init receiver of lpuart
//! @param[in] callback When Rx char is received

void lpuart_set_rx_callback(void (*RxCb)(uint8_t *rxChar));

//! @brief Write buffer data in dma mode
//! @param[in] buffer pointer to buffer
//! @param[in] length of buffer p_data to be sent

void lpuart_async_write(uint8_t *buffer, size_t length);

//! @brief DeInit

void lpuart_deinit(void);

//!@brief Init IO

void lpuart_io_init(void);

//! @brief DeInit IO

void lpuart_io_deinit(void);

#endif // __LPUART_H

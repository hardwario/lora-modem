#ifndef __LPUART_H
#define __LPUART_H

#include "common.h"

typedef void (*lpuart_rx_callback_f)(const uint8_t *data, size_t len);


//! @brief Init lpuart
//! @param[in] callback when Tx buffer has been sent

void lpuart_init(unsigned int baudrate, void (*Txcb)(void));

//! @brief Init receiver of lpuart
//! @param[in] callback When Rx char is received

void lpuart_set_rx_callback(lpuart_rx_callback_f cb);

//! @brief Write buffer data in dma mode
//! @param[in] buffer pointer to buffer
//! @param[in] length of buffer p_data to be sent

void lpuart_async_write(uint8_t *buffer, size_t length);

//! @brief DeInit

void lpuart_deinit(void);

void lpuart_disable_rx_dma(void);
void lpuart_enable_rx_dma(void);

#endif // __LPUART_H

#ifndef _USART_H
#define _USART_H

#include "common.h"

#ifndef USART_TX_BUFFER_SIZE
#define USART_TX_BUFFER_SIZE 1024
#endif

#ifndef USART_RX_BUFFER_SIZE
#define USART_RX_BUFFER_SIZE 512
#endif

//! @brief  Init usart

void usart_init(void);

//! @brief Write data to usart
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be written
//! @return Number of bytes written

size_t usart_write(const char *buffer, size_t length);

//! @brief Read data from usart
//! @param[in] buffer Pointer to destination buffer
//! @param[in] length Number of bytes to be read
//! @return Number of bytes read

size_t usart_read(char *buffer, size_t length);

//!@brief  Init IO

void usart_io_init(void);

//! @brief  DeInit IO

void usart_io_deinit(void);

#endif // _USART_H

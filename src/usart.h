#ifndef __USART_H__
#define __USART_H__

#include <stddef.h>

//! @brief  Init usart

void usart_init(void);

//! @brief Write data to usart
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be written
//! @return Number of bytes written

size_t usart_write(const char *buffer, size_t length);


#endif /* __USART_H__ */

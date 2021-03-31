#ifndef _CONSOLE_H
#define _CONSOLE_H

#include "common.h"

//! @brief Initialize

void console_init(void);

//! @brief Write data to console
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be written
//! @return Number of bytes written

size_t console_write(const char *buffer, size_t length);

//! @brief Read data from console
//! @param[in] buffer Pointer to destination buffer
//! @param[in] length Number of bytes to be read
//! @return Number of bytes read

size_t console_read(char *buffer, size_t length);

#endif

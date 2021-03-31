#ifndef _FIFO_H
#define _FIFO_H

#include "common.h"

//! @brief Structure of FIFO instance
typedef struct
{
    void *_buffer;
    size_t _size;
    volatile size_t _head;
    volatile size_t _tail;

} fifo_t;

//! @brief Initialize FIFO buffer
//! @param[in] fifo FIFO instance
//! @param[in] buffer Pointer to buffer where FIFO holds data
//! @param[in] size Size of buffer where FIFO holds data

void fifo_init(fifo_t *fifo, void *buffer, size_t size);

//! @brief Get spaces
//! @param[in] fifo FIFO instance
//! @return Number of bytes

size_t fifo_get_spaces(fifo_t *fifo);

//! @brief Get available
//! @param[in] fifo FIFO instance
//! @return Number of bytes

size_t fifo_get_available(fifo_t *fifo);

//! @brief Write data to FIFO
//! @param[in] fifo FIFO instance
//! @param[in] buffer Pointer to buffer from which data will be written
//! @param[in] length Number of requested bytes to be written
//! @return Number of bytes written

size_t fifo_write(fifo_t *fifo, const void *buffer, size_t length);

//! @brief Read data from FIFO
//! @param[in] fifo FIFO instance
//! @param[out] buffer Pointer to buffer where data will be read
//! @param[in] length Number of requested bytes to be read
//! @return Number of bytes read

size_t fifo_read(fifo_t *fifo, void *buffer, size_t length);

#endif

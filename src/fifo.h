#ifndef _FIFO_H
#define _FIFO_H

#include "common.h"

typedef struct
{
    void *_buffer;
    size_t _size;
    volatile size_t _head;
    volatile size_t _tail;

} fifo_t;

void fifo_init(fifo_t *fifo, void *buffer, size_t size);
size_t fifo_get_spaces(fifo_t *fifo);
size_t fifo_get_available(fifo_t *fifo);
size_t fifo_write(fifo_t *fifo, const void *buffer, size_t length);
size_t fifo_read(fifo_t *fifo, void *buffer, size_t length);

#endif

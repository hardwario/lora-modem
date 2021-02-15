#include "fifo.h"
#include "irq.h"

void fifo_init(fifo_t *fifo, void *buffer, size_t size)
{
    memset(fifo, 0, sizeof(*fifo));

    fifo->_buffer = buffer;
    fifo->_size = size;
}

size_t fifo_get_spaces(fifo_t *fifo)
{
    size_t spaces;

    irq_disable();

    if (fifo->_head >= fifo->_tail)
    {
        spaces = fifo->_size - fifo->_head + fifo->_tail - 1;

        irq_enable();

        return spaces;
    }

    spaces = fifo->_tail - fifo->_head - 1;

    irq_enable();

    return spaces;
}

size_t fifo_get_available(fifo_t *fifo)
{
    size_t available;

    irq_disable();

    if (fifo->_head > fifo->_tail)
    {
        available = fifo->_head - fifo->_tail;

        irq_enable();

        return available;
    }

    available = fifo->_size - fifo->_tail + fifo->_head;

    irq_enable();

    return available;
}

size_t fifo_write(fifo_t *fifo, const void *buffer, size_t length)
{
    const uint8_t *p = buffer;

    irq_disable();

    for (size_t i = 0; i < length; i++)
    {
        if (fifo->_head + 1 == fifo->_tail || (fifo->_head + 1 == fifo->_size && fifo->_tail == 0))
        {
            irq_enable();

            return i;
        }

        *((uint8_t *)fifo->_buffer + fifo->_head++) = *p++;

        if (fifo->_head == fifo->_size)
        {
            fifo->_head = 0;
        }
    }

    irq_enable();

    return length;
}

size_t fifo_read(fifo_t *fifo, void *buffer, size_t length)
{
    uint8_t *p = buffer;

    irq_disable();

    for (size_t i = 0; i < length; i++)
    {
        if (fifo->_tail != fifo->_head)
        {
            *p++ = *((uint8_t *)fifo->_buffer + fifo->_tail);

            if (++fifo->_tail == fifo->_size)
            {
                fifo->_tail = 0;
            }
        }
        else
        {
            irq_enable();

            return i;
        }
    }

    irq_enable();

    return length;
}

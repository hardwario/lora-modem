#include "irq.h"

typedef struct
{
    volatile int semaphore;
    volatile bool enabled;

} irq_t;

static irq_t _irq = { 0, false };

void irq_init(void)
{
    memset(&_irq, 0, sizeof(_irq));

    __enable_irq();
}

void irq_disable(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();

    if (_irq.semaphore++ == 0)
    {
        _irq.enabled = (primask & 1) == 0 ? true : false;
    }
}

void irq_enable(void)
{
    if (_irq.semaphore != 0)
    {
        if (--_irq.semaphore == 0 && _irq.enabled)
        {
            __enable_irq();
        }
    }
}

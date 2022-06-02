#ifndef __IRQ_H__
#define __IRQ_H__

#include <stm/include/cmsis_compiler.h>


__STATIC_FORCEINLINE uint32_t disable_irq(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    return mask;
}


__STATIC_FORCEINLINE void reenable_irq(uint32_t mask)
{
    __set_PRIMASK(mask);
}


__STATIC_FORCEINLINE void enable_irq(void)
{
    __enable_irq();
}


#endif // __IRQ_H__
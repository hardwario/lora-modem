#ifndef _HW_GPIO_H
#define _HW_GPIO_H

#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include "common.h"

typedef void(gpio_irq_handler_t)(void *context);

//! @brief Initializes the given GPIO object
//! @param [IN] port where x can be (A..E and H)
//! @param [IN] pin specifies the port bit to be written.
//! @param [IN] init_struct  GPIO_InitTypeDef intit structure

void gpio_init(GPIO_TypeDef *port, uint16_t pin, GPIO_InitTypeDef *init_struct);

//! @brief Records the interrupt handler for the GPIO  object
//! @param [IN] port where x can be (A..E and H)
//! @param [IN] pin specifies the port bit to be written.
//! @param [IN] prio       NVIC priority (0 is highest)
//! @param [IN] irqHandler  points to the  function to execute

void gpio_set_irq(GPIO_TypeDef *port, uint16_t pin, uint32_t prio,  gpio_irq_handler_t *irqHandler);

//! @brief Writes the given value to the GPIO output
//! @param [IN] pin specifies the port bit to be written.
//! @param [IN] value New GPIO output value

void gpio_write(GPIO_TypeDef *port, uint16_t pin,  uint32_t value);

//! @brief Reads the current GPIO input value
//! @param [IN] port where x can be (A..E and H)
//! @param [IN] pin specifies the port bit to be written.
//! @retval value Current GPIO input value

uint32_t gpio_read(GPIO_TypeDef *port, uint16_t pin);

//! @brief Execute the interrupt from the object
//! @param [IN] port where x can be (A..E and H)
//! @param [IN] pin specifies the port bit to be written.

void gpio_hal_msp_irq_handler(uint16_t pin);

typedef struct
{
    void *port;
    uint16_t pinIndex;
} Gpio_t;

#endif // _HW_GPIO_H


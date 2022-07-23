#include "gpio.h"

static gpio_irq_handler_t *_gpio_irq[16] = {NULL};
static uint8_t HW_GPIO_Getbit_pos(uint16_t pin);

void gpio_init(GPIO_TypeDef *port, uint16_t pin, GPIO_InitTypeDef *init_struct)
{

    switch ((uint32_t) port)
    {
    case GPIOA_BASE:
        __HAL_RCC_GPIOA_CLK_ENABLE();
        break;
    case GPIOB_BASE:
        __HAL_RCC_GPIOB_CLK_ENABLE();
        break;
    case GPIOC_BASE:
        __HAL_RCC_GPIOC_CLK_ENABLE();
        break;
    case GPIOD_BASE:
        __HAL_RCC_GPIOD_CLK_ENABLE();
        break;
    case GPIOH_BASE:
    default:
        __HAL_RCC_GPIOH_CLK_ENABLE();
    }

    init_struct->Pin = pin;

    HAL_GPIO_Init(port, init_struct);
}

void gpio_set_irq(GPIO_TypeDef *port, uint16_t pin, uint32_t prio, gpio_irq_handler_t *irqHandler)
{
    (void) port;
    IRQn_Type irq_nb;

    uint32_t bit_pos = HW_GPIO_Getbit_pos(pin);

    if (irqHandler != NULL)
    {
        _gpio_irq[bit_pos] = irqHandler;

        switch (pin)
        {
        case GPIO_PIN_0:
        case GPIO_PIN_1:
            irq_nb = EXTI0_1_IRQn;
            break;
        case GPIO_PIN_2:
        case GPIO_PIN_3:
            irq_nb = EXTI2_3_IRQn;
            break;
        case GPIO_PIN_4:
        case GPIO_PIN_5:
        case GPIO_PIN_6:
        case GPIO_PIN_7:
        case GPIO_PIN_8:
        case GPIO_PIN_9:
        case GPIO_PIN_10:
        case GPIO_PIN_11:
        case GPIO_PIN_12:
        case GPIO_PIN_13:
        case GPIO_PIN_14:
        case GPIO_PIN_15:
        default:
            irq_nb = EXTI4_15_IRQn;
        }

        HAL_NVIC_SetPriority(irq_nb, prio, 0);

        HAL_NVIC_EnableIRQ(irq_nb);
    }
    else
    {
        _gpio_irq[bit_pos] = NULL;
    }
}

void gpio_hal_msp_irq_handler(uint16_t pin)
{
    uint32_t bit_pos = HW_GPIO_Getbit_pos(pin);

    if (_gpio_irq[bit_pos] != NULL)
    {
        _gpio_irq[bit_pos](NULL);
    }
}

void gpio_write(GPIO_TypeDef *port, uint16_t pin, uint32_t value)
{
    HAL_GPIO_WritePin(port, pin, (GPIO_PinState)value);
}

uint32_t gpio_read(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin);
}

static uint8_t HW_GPIO_Getbit_pos(uint16_t pin)
{
    uint8_t pin_pos = 0;

    if ((pin & 0xFF00) != 0)
    {
        pin_pos |= 0x8;
    }
    if ((pin & 0xF0F0) != 0)
    {
        pin_pos |= 0x4;
    }
    if ((pin & 0xCCCC) != 0)
    {
        pin_pos |= 0x2;
    }
    if ((pin & 0xAAAA) != 0)
    {
        pin_pos |= 0x1;
    }

    return pin_pos;
}

void EXTI0_1_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);
}

void EXTI2_3_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);
}

void EXTI4_15_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);

    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
}


void GpioWrite(Gpio_t *obj, uint32_t value)
{
    gpio_write(obj->port, obj->pinIndex, value);
}

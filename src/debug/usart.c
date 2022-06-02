#include "usart.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_usart.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include "cbuf.h"
#include "irq.h"
#include "system.h"
#include "halt.h"


#ifndef USART_TX_BUFFER_SIZE
#define USART_TX_BUFFER_SIZE 1024
#endif


static char tx_buffer[USART_TX_BUFFER_SIZE];
static cbuf_t tx_fifo;


void usart_init(void)
{
    cbuf_init(&tx_fifo, tx_buffer, sizeof(tx_buffer));
    uint32_t masked = disable_irq();

    __USART1_CLK_ENABLE();

    LL_USART_InitTypeDef params = {
        .BaudRate            = 115200,
        .DataWidth           = LL_USART_DATAWIDTH_8B,
        .StopBits            = LL_USART_STOPBITS_1,
        .Parity              = LL_USART_PARITY_NONE,
        .TransferDirection   = LL_USART_DIRECTION_TX,
        .HardwareFlowControl = LL_USART_HWCONTROL_NONE,
        .OverSampling        = LL_USART_OVERSAMPLING_16
    };

    if (LL_USART_Init(USART1, &params) != 0) goto error;

    LL_USART_Enable(USART1);

    LL_USART_DisableIT_TXE(USART1);
    LL_USART_EnableIT_TC(USART1);

    // Configure interrupts
    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    // Configure GPIO
    __GPIOA_CLK_ENABLE();
    __GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {
        .Pin       = GPIO_PIN_9,
        .Mode      = GPIO_MODE_AF_PP,
        .Pull      = GPIO_NOPULL,
        .Speed     = GPIO_SPEED_HIGH,
        .Alternate = GPIO_AF4_USART1
    };

    HAL_GPIO_Init(GPIOA, &gpio);

    reenable_irq(masked);
    return;
error:
    reenable_irq(masked);
    halt("Error while initializing USART port");
}


size_t usart_write(const char *buffer, size_t length)
{
    cbuf_view_t v;
    uint32_t masked = disable_irq();
    cbuf_tail(&tx_fifo, &v);
    reenable_irq(masked);

    size_t stored = cbuf_copy_in(&v, buffer, length);

    system_wait_hsi();

    masked = disable_irq();
    cbuf_produce(&tx_fifo, stored);

    // Enable the transmission buffer empty interrupt which will pickup the data
    // written to the FIFO by the above code and start transmitting it.
    if (!LL_USART_IsEnabledIT_TXE(USART1)) {
        system_disallow_stop_mode(SYSTEM_MODULE_USART);
        LL_USART_EnableIT_TXE(USART1);
    }

    reenable_irq(masked);
    return stored;
}


void USART1_IRQHandler(void)
{
    uint8_t c;

    if (LL_USART_IsEnabledIT_TXE(USART1) && LL_USART_IsActiveFlag_TXE(USART1)) {
        if (cbuf_get(&tx_fifo, &c, 1) != 0) {
            LL_USART_TransmitData8(USART1, c);
        } else {
            LL_USART_DisableIT_TXE(USART1);
        }
    }

    if (LL_USART_IsActiveFlag_TC(USART1)) {
        LL_USART_ClearFlag_TC(USART1);
        system_allow_stop_mode(SYSTEM_MODULE_USART);
    }
}

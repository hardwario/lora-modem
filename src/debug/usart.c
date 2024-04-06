#include "usart.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_usart.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include "cbuf.h"
#include "irq.h"
#include "system.h"
#include "halt.h"

#if DEBUG_LOG != 3

#if DEBUG_LOG == 1
#  define PORT       USART1
#  define IRQn       USART1_IRQn
#  define CLK_ENABLE __USART1_CLK_ENABLE
#  define PIN        GPIO_PIN_9
#  define ALTERNATE  GPIO_AF4_USART1
#elif DEBUG_LOG == 2
#  define PORT       USART2
#  define IRQn       USART2_IRQn
#  define CLK_ENABLE __USART2_CLK_ENABLE
#  define PIN        GPIO_PIN_2
#  define ALTERNATE  GPIO_AF4_USART2
#else
#  error Unsupported DEBUG_LOG value
#endif


#ifndef USART_TX_BUFFER_SIZE
#define USART_TX_BUFFER_SIZE 1024
#endif


static char tx_buffer[USART_TX_BUFFER_SIZE];
static cbuf_t tx_fifo;


void usart_init(void)
{
    cbuf_init(&tx_fifo, tx_buffer, sizeof(tx_buffer));
    uint32_t masked = disable_irq();

    CLK_ENABLE();

    LL_USART_InitTypeDef params = {
        .BaudRate            = 115200,
        .DataWidth           = LL_USART_DATAWIDTH_8B,
        .StopBits            = LL_USART_STOPBITS_1,
        .Parity              = LL_USART_PARITY_NONE,
        .TransferDirection   = LL_USART_DIRECTION_TX,
        .HardwareFlowControl = LL_USART_HWCONTROL_NONE,
        .OverSampling        = LL_USART_OVERSAMPLING_16
    };

    if (LL_USART_Init(PORT, &params) != 0) goto error;

    LL_USART_Enable(PORT);

    LL_USART_DisableIT_TXE(PORT);
    LL_USART_EnableIT_TC(PORT);

    // Configure interrupts
    HAL_NVIC_SetPriority(IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(IRQn);

    // Configure GPIO
    __GPIOA_CLK_ENABLE();
    __GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {
        .Pin       = PIN,
        .Mode      = GPIO_MODE_AF_PP,
        .Pull      = GPIO_NOPULL,
        .Speed     = GPIO_SPEED_HIGH,
        .Alternate = ALTERNATE
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

    // Enable the transmission buffer empty interrupt, which will pick up the
    // data written by the FIFO using the above code and starts transmitting it.
    if (!LL_USART_IsEnabledIT_TXE(PORT)) {
        system_stop_lock |= SYSTEM_MODULE_USART;
        LL_USART_EnableIT_TXE(PORT);
    }

    reenable_irq(masked);
    return stored;
}


#if DEBUG_LOG == 1
void USART1_IRQHandler(void)
#elif DEBUG_LOG == 2
void USART2_IRQHandler(void)
#else
#error Unsupport DEBUG_LOG
#endif
{
    uint8_t c;

    if (LL_USART_IsEnabledIT_TXE(PORT) && LL_USART_IsActiveFlag_TXE(PORT)) {
        if (cbuf_get(&tx_fifo, &c, 1) != 0) {
            LL_USART_TransmitData8(PORT, c);
        } else {
            LL_USART_DisableIT_TXE(PORT);
        }
    }

    if (LL_USART_IsActiveFlag_TC(PORT)) {
        LL_USART_ClearFlag_TC(PORT);
        system_stop_lock &= ~SYSTEM_MODULE_USART;
    }
}

#endif
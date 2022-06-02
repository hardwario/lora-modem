#include "usart.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_usart.h>
#include "cbuf.h"
#include "irq.h"
#include "system.h"
#include "io.h"
#include "halt.h"


#ifndef USART_TX_BUFFER_SIZE
#define USART_TX_BUFFER_SIZE 256
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
        .Pin       = USART_TX_PIN,
        .Mode      = GPIO_MODE_AF_PP,
        .Pull      = GPIO_NOPULL,
        .Speed     = GPIO_SPEED_HIGH,
        .Alternate = USART_TX_AF
    };

    HAL_GPIO_Init(USART_TX_GPIO_PORT, &gpio);

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


void usart_write_blocking(const char *buffer, size_t length)
{
    uint32_t masked;
    size_t written;
    while (length) {
        written = usart_write(buffer, length);
        buffer += written;
        length -= written;

        if (written == 0) {
            while (tx_fifo.max_length == tx_fifo.length) {
                masked = disable_irq();

                // If we reached a full TX FIFO and were invoked with interrupts
                // masked, we have to abort here. We cannot wait for the TX FIFO
                // to have space since that generally requires working
                // interrupts. We will end up sending incomplete message in this
                // case.
                if (masked) return;

                // If the TX FIFO is at full capacity, we invoke system_sleep to
                // put the MCU to sleep until there is some space in the output
                // FIFO. System_sleep used below must not enter the Stop mode.
                // That is, however, guaranteed, since the function usart_write
                // above creates a stop mode wake lock which will still be in
                // place when the process gets here.
                if (tx_fifo.max_length == tx_fifo.length)
                    system_sleep();
                reenable_irq(masked);
            }
        }
    }
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

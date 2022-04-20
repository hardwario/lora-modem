#include "usart.h"
#include "cbuf.h"
#include "irq.h"
#include "system.h"
#include "io.h"


#ifndef USART_TX_BUFFER_SIZE
#define USART_TX_BUFFER_SIZE 256
#endif


static char tx_buffer[USART_TX_BUFFER_SIZE];
static cbuf_t tx_fifo;


void usart_init(void)
{
    cbuf_init(&tx_fifo, tx_buffer, sizeof(tx_buffer));
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    irq_disable();

    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB2ENR;

    USART1->CR3 |= USART_CR3_OVRDIS;
    USART1->CR1 |= USART_CR1_TE; // USART_CR1_RXNEIE | USART_CR1_RE;
    USART1->BRR = 0x116; // 115200
    USART1->CR1 |= USART_CR1_UE;

    GPIO_InitTypeDef GPIO_InitStruct = { 0 };

    // Enable GPIO TX/RX clock
    __GPIOA_CLK_ENABLE();
    __GPIOA_CLK_ENABLE();

    // UART TX GPIO pin configuration
    GPIO_InitStruct.Pin = USART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    GPIO_InitStruct.Alternate = USART_TX_AF;

    HAL_GPIO_Init(USART_TX_GPIO_PORT, &GPIO_InitStruct);

    irq_enable();
}


size_t usart_write(const char *buffer, size_t length)
{
    cbuf_view_t v;
    irq_disable();
    cbuf_tail(&tx_fifo, &v);
    irq_enable();

    size_t stored = cbuf_copy_in(&v, buffer, length);

    system_wait_hsi();

    irq_disable();
    cbuf_produce(&tx_fifo, stored);
    system_disallow_stop_mode(SYSTEM_MODULE_USART);
    USART1->CR1 |= USART_CR1_TXEIE;
    irq_enable();

    return stored;
}


void USART1_IRQHandler(void)
{
    static bool block = false;

    if ((USART1->CR1 & USART_CR1_TXEIE) && (USART1->ISR & USART_ISR_TXE)) {
        uint8_t c;

        if (cbuf_get(&tx_fifo, &c, 1) != 0) {
            if (!block) block = true;
            USART1->TDR = c;
        } else {
            USART1->CR1 &= ~USART_CR1_TXEIE;
            USART1->CR1 |= USART_CR1_TCIE;
        }
    }

    if ((USART1->CR1 & USART_CR1_TCIE) && (USART1->ISR & USART_ISR_TC)) {
        USART1->CR1 &= ~USART_CR1_TCIE;

        if (block) {
            block = false;
            system_allow_stop_mode(SYSTEM_MODULE_USART);
        }
    }
}

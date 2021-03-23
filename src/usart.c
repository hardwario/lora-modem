#include "usart.h"
#include "fifo.h"
#include "irq.h"
#include "system.h"
#include "io.h"

typedef struct
{
    fifo_t tx_fifo;
    fifo_t rx_fifo;
    uint8_t tx_buffer[USART_TX_BUFFER_SIZE];
    uint8_t rx_buffer[USART_RX_BUFFER_SIZE];

} usart_t;

static usart_t usart;

void usart_init(void)
{
    memset(&usart, 0, sizeof(usart));

    fifo_init(&usart.tx_fifo, usart.tx_buffer, sizeof(usart.tx_buffer));
    fifo_init(&usart.rx_fifo, usart.rx_buffer, sizeof(usart.rx_buffer));

    HAL_NVIC_EnableIRQ(USART1_IRQn);

    irq_disable();

    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB2ENR;

    USART1->CR3 |= USART_CR3_OVRDIS;
    USART1->CR1 |= USART_CR1_RXNEIE | USART_CR1_TE | USART_CR1_RE;
    USART1->BRR = 0x116; // 115200
    USART1->CR1 |= USART_CR1_UE;

    irq_enable();

    usart_io_init();
}

size_t usart_write(const char *buffer, size_t length)
{
    if (length > fifo_get_spaces(&usart.tx_fifo))
    {
        return 0;
    }

    size_t ret = fifo_write(&usart.tx_fifo, buffer, length);

    system_wait_hsi();

    irq_disable();
    USART1->CR1 |= USART_CR1_TXEIE;
    irq_enable();

    return ret;
}

size_t usart_read(char *buffer, size_t length)
{
    return fifo_read(&usart.rx_fifo, buffer, length);
}

void usart_io_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    /* Enable GPIO TX/RX clock */
    __GPIOA_CLK_ENABLE();
    __GPIOA_CLK_ENABLE();
    /* UART TX GPIO pin configuration  */
    GPIO_InitStruct.Pin = USART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    GPIO_InitStruct.Alternate = USART_TX_AF;

    HAL_GPIO_Init(USART_TX_GPIO_PORT, &GPIO_InitStruct);

    /* UART RX GPIO pin configuration  */
    GPIO_InitStruct.Pin = USART_RX_PIN;
    GPIO_InitStruct.Alternate = USART_RX_AF;

    HAL_GPIO_Init(USART_RX_GPIO_PORT, &GPIO_InitStruct);
}

void usart_io_deinit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    __GPIOA_CLK_ENABLE();

    GPIO_InitStructure.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStructure.Pull = GPIO_NOPULL;

    GPIO_InitStructure.Pin = USART_TX_PIN;
    HAL_GPIO_Init(USART_TX_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = USART_RX_PIN;
    HAL_GPIO_Init(USART_RX_GPIO_PORT, &GPIO_InitStructure);
}

void USART1_IRQHandler(void)
{
    static bool block = false;

    if ((USART1->CR1 & USART_CR1_RXNEIE) != 0 && (USART1->ISR & USART_ISR_RXNE) != 0)
    {
        uint8_t c = USART1->RDR;

        fifo_write(&usart.rx_fifo, &c, 1);
    }

    if ((USART1->CR1 & USART_CR1_TXEIE) != 0 && (USART1->ISR & USART_ISR_TXE) != 0)
    {
        uint8_t c;

        if (fifo_read(&usart.tx_fifo, &c, 1) != 0)
        {
            if (!block)
            {
                block = true;

                system_stop_mode_disable(SYSTEM_MASK_USART);
            }

            USART1->TDR = c;
        }
        else
        {
            USART1->CR1 &= ~USART_CR1_TXEIE;
            USART1->CR1 |= USART_CR1_TCIE;
        }
    }

    if ((USART1->CR1 & USART_CR1_TCIE) != 0 && (USART1->ISR & USART_ISR_TC) != 0)
    {
        USART1->CR1 &= ~USART_CR1_TCIE;

        if (block)
        {
            block = false;

            system_stop_mode_enable(SYSTEM_MASK_USART);
        }
    }
}

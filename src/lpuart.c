#include "lpuart.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_dma.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_lpuart.h>
#include <LoRaWAN/Utilities/utilities.h>
#include "io.h"
#include "halt.h"
#include "utils.h"
#include "cbuf.h"
#include "log.h"
#include "irq.h"
#include "system.h"


#ifndef LPUART_BUFFER_SIZE
#define LPUART_BUFFER_SIZE 512
#endif

#ifndef LPUART_DMA_BUFFER_SIZE
#define LPUART_DMA_BUFFER_SIZE 64
#endif


static UART_HandleTypeDef port;

static unsigned char tx_buffer[LPUART_BUFFER_SIZE];
static __IO ITStatus tx_idle;
static volatile size_t tx_len;
volatile cbuf_t lpuart_tx_fifo;

static unsigned char dma_buffer[LPUART_DMA_BUFFER_SIZE];
static unsigned char rx_buffer[LPUART_BUFFER_SIZE];
volatile cbuf_t lpuart_rx_fifo;


// This function is invoked from the IRQ handler context
static void enqueue(unsigned char *data, size_t len)
{
    size_t stored = cbuf_put(&lpuart_rx_fifo, data, len);
    if (stored != len)
        log_warning("lpuart: Read overrun, %d bytes discarded", len - stored);
}


// This function is invoked from the IRQ handler context
static void rx_callback(void)
{
    static size_t old_pos;
    size_t pos;

    pos = ARRAY_LEN(dma_buffer) - LL_DMA_GetDataLength(DMA1, LL_DMA_CHANNEL_6);
    if (pos == old_pos) return;

    if (pos > old_pos) {
        enqueue(&dma_buffer[old_pos], pos - old_pos);
    } else {
        enqueue(&dma_buffer[old_pos], ARRAY_LEN(dma_buffer) - old_pos);
        if (pos > 0) enqueue(&dma_buffer[0], pos);
    }
    old_pos = pos;
}


void lpuart_init(unsigned int baudrate)
{
    cbuf_init(&lpuart_tx_fifo, tx_buffer, sizeof(tx_buffer));
    cbuf_init(&lpuart_rx_fifo, rx_buffer, sizeof(rx_buffer));
    tx_idle = 1;

    port.Instance = LPUART1;
    port.Init.Mode = UART_MODE_TX_RX;
    port.Init.BaudRate = baudrate;
    port.Init.WordLength = UART_WORDLENGTH_8B;
    port.Init.StopBits = UART_STOPBITS_1;
    port.Init.Parity = UART_PARITY_NONE;
    port.Init.HwFlowCtl = UART_HWCONTROL_NONE;

    if (HAL_UART_Init(&port) != HAL_OK)
        halt("Error while initializing LPUART");

    LL_LPUART_EnableIT_IDLE(port.Instance);

    // Wake the MCU up from stop mode if we start receiving data over LPUART1
    UART_WakeUpTypeDef wake = { .WakeUpEvent = LL_LPUART_WAKEUP_ON_STARTBIT };
    HAL_UARTEx_StopModeWakeUpSourceConfig(&port, wake);

    if (HAL_UART_Receive_DMA(&port, dma_buffer, ARRAY_LEN(dma_buffer)) != HAL_OK)
        halt("Couldn't start DMA for LPUART1 rx path");

    HAL_UARTEx_EnableStopMode(&port);
}


static void init_gpio(void)
{
    GPIO_InitTypeDef gpio = {
        .Mode = GPIO_MODE_AF_PP,
        .Speed = GPIO_SPEED_HIGH
    };

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = LPUART_TX_PIN;
    gpio.Alternate = LPUART_TX_AF;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(LPUART_TX_GPIO_PORT, &gpio);

    gpio.Pin = LPUART_RX_PIN;
    gpio.Alternate = LPUART_RX_AF;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(LPUART_RX_GPIO_PORT, &gpio);
}


static void deinit_gpio(void)
{
    GPIO_InitTypeDef gpio = {
        .Mode = GPIO_MODE_ANALOG,
        .Pull = GPIO_NOPULL
    };

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = LPUART_TX_PIN;
    HAL_GPIO_Init(LPUART_TX_GPIO_PORT, &gpio);

    gpio.Pin = LPUART_RX_PIN;
    HAL_GPIO_Init(LPUART_RX_GPIO_PORT, &gpio);
}


void HAL_UART_MspInit(UART_HandleTypeDef *port)
{
    /* Enable peripherals and GPIO Clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Enable LPUART clock */
    __LPUART1_CLK_ENABLE();

    /* select LPUART clock source */
    RCC_PeriphCLKInitTypeDef clock = {
        .PeriphClockSelection = RCC_PERIPHCLK_LPUART1,
        .Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_HSI
    };
    HAL_RCCEx_PeriphCLKConfig(&clock);

    /* Enable DMA clock */
    __HAL_RCC_DMA1_CLK_ENABLE();

    static DMA_HandleTypeDef tx_dma = {
        .Instance = DMA1_Channel7,
        .Init = {
            .Direction           = DMA_MEMORY_TO_PERIPH,
            .Priority            = DMA_PRIORITY_LOW,
            .Mode                = DMA_NORMAL,
            .Request             = DMA_REQUEST_5,
            .PeriphDataAlignment = DMA_PDATAALIGN_BYTE,
            .MemDataAlignment    = DMA_MDATAALIGN_BYTE,
            .PeriphInc           = DMA_PINC_DISABLE,
            .MemInc              = DMA_MINC_ENABLE
        }
    };

    if (HAL_DMA_Init(&tx_dma) != HAL_OK)
        halt("Failed to initialize DMA for LPUART1 TX path");

    __HAL_LINKDMA(port, hdmatx, tx_dma);


    static DMA_HandleTypeDef rx_dma = {
        .Instance = DMA1_Channel6,
        .Init = {
            .Direction           = DMA_PERIPH_TO_MEMORY,
            .Priority            = DMA_PRIORITY_LOW,
            .Mode                = DMA_CIRCULAR,
            .Request             = DMA_REQUEST_5,
            .PeriphDataAlignment = DMA_PDATAALIGN_BYTE,
            .MemDataAlignment    = DMA_MDATAALIGN_BYTE,
            .PeriphInc           = DMA_PINC_DISABLE,
            .MemInc              = DMA_MINC_ENABLE
        }
    };

    if (HAL_DMA_Init(&rx_dma) != HAL_OK)
        halt("Failed to initialize DMA for LPUART1 RX path");

    /* Associate the initialized DMA handle with the LPUART handle */
    __HAL_LINKDMA(port, hdmarx, rx_dma);

    HAL_NVIC_SetPriority(DMA1_Channel4_5_6_7_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_5_6_7_IRQn);

    HAL_NVIC_SetPriority(RNG_LPUART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(RNG_LPUART1_IRQn);

    /* Configure the GPIO pins used by LPUART */
    init_gpio();
}


void HAL_UART_MspDeInit(UART_HandleTypeDef *port)
{
    deinit_gpio();

    /* Reset peripherals */
    __LPUART1_FORCE_RESET();
    __LPUART1_RELEASE_RESET();

    /* Disable DMA */
    if (port->hdmarx != 0) HAL_DMA_DeInit(port->hdmarx);
    if (port->hdmatx != 0) HAL_DMA_DeInit(port->hdmatx);

    HAL_NVIC_DisableIRQ(DMA1_Channel4_5_6_7_IRQn);
    HAL_NVIC_DisableIRQ(RNG_LPUART1_IRQn);
}


size_t lpuart_write(const char *buffer, size_t length)
{
    cbuf_view_t v;

    irq_disable();
    cbuf_tail(&lpuart_tx_fifo, &v);
    irq_enable();

    size_t written = cbuf_copy_in(&v, buffer, length);

    irq_disable();
    cbuf_produce(&lpuart_tx_fifo, written);

    if (tx_idle && lpuart_tx_fifo.length > 0) {
        tx_idle = 0;
        system_disallow_stop_mode(SYSTEM_MODULE_LPUART_TX);

        cbuf_head(&lpuart_tx_fifo, &v);
        if (v.len[0]) {
            HAL_UART_Transmit_DMA(&port, (unsigned char *)v.ptr[0], v.len[0]);
            tx_len = v.len[0];
        } else {
            HAL_UART_Transmit_DMA(&port, (unsigned char *)v.ptr[1], v.len[1]);
            tx_len = v.len[1];
        }
    }

    irq_enable();
    return written;
}


void lpuart_write_blocking(const char *buffer, size_t length)
{
    size_t written;
    while (length) {
        written = lpuart_write(buffer, length);
        buffer += written;
        length -= written;

        if (written == 0) {
            while (lpuart_tx_fifo.max_length == lpuart_tx_fifo.length) {
                CRITICAL_SECTION_BEGIN();
                if (lpuart_tx_fifo.max_length == lpuart_tx_fifo.length)
                    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
                CRITICAL_SECTION_END();
            }
        }
    }
}


void HAL_UART_TxCpltCallback(UART_HandleTypeDef *port)
{
    cbuf_view_t v;

    if (tx_len) cbuf_consume(&lpuart_tx_fifo, tx_len);

    if (lpuart_tx_fifo.length) {
        cbuf_head(&lpuart_tx_fifo, &v);
        if (v.len[0]) {
            HAL_UART_Transmit_DMA(port, (unsigned char *)v.ptr[0], v.len[0]);
            tx_len = v.len[0];
        } else {
            HAL_UART_Transmit_DMA(port, (unsigned char *)v.ptr[1], v.len[1]);
            tx_len = v.len[1];
        }
    } else {
        system_allow_stop_mode(SYSTEM_MODULE_LPUART_TX);
        tx_len = 0;
        tx_idle = 1;
    }
}


void RNG_LPUART1_IRQHandler(void)
{
    if (LL_LPUART_IsEnabledIT_IDLE(port.Instance) && LL_LPUART_IsActiveFlag_IDLE(port.Instance)) {
        LL_LPUART_ClearFlag_IDLE(port.Instance);
        rx_callback();
        system_allow_stop_mode(SYSTEM_MODULE_LPUART_RX);
        return;
    }

    if (LL_LPUART_IsEnabledIT_RXNE(port.Instance)) {
        LL_LPUART_DisableIT_RXNE(port.Instance);
        system_disallow_stop_mode(SYSTEM_MODULE_LPUART_RX);
        return;
    }

    // If the event wasn't handled by the code above, delegate to the HAL. But
    // before we do that, check and clear the error flags, otherwise the HAL
    // would abort the DMA transfer.

    if (LL_LPUART_IsActiveFlag_PE(port.Instance))
        LL_LPUART_ClearFlag_PE(port.Instance);

    if (LL_LPUART_IsActiveFlag_FE(port.Instance))
        LL_LPUART_ClearFlag_FE(port.Instance);

    if (LL_LPUART_IsActiveFlag_ORE(port.Instance))
        LL_LPUART_ClearFlag_ORE(port.Instance);

    if (LL_LPUART_IsActiveFlag_NE(port.Instance))
        LL_LPUART_ClearFlag_NE(port.Instance);

    HAL_UART_IRQHandler(&port);
}


void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *port)
{
    (void)port;
    rx_callback();
}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    (void)handle;
    rx_callback();
}


void DMA1_Channel4_5_6_7_IRQHandler(void)
{
    HAL_DMA_IRQHandler(port.hdmarx);
    HAL_DMA_IRQHandler(port.hdmatx);
}


void lpuart_disable_rx_dma(void)
{
    LL_LPUART_EnableIT_RXNE(port.Instance);
    HAL_UART_DMAPause(&port);
}


void lpuart_enable_rx_dma(void)
{
    HAL_UART_DMAResume(&port);
}


size_t lpuart_read(char *buffer, size_t length)
{
    cbuf_view_t v;

    irq_disable();
    cbuf_head(&lpuart_rx_fifo, &v);
    irq_enable();

    size_t rv = cbuf_copy_out(buffer, &v, length);

    irq_disable();
    cbuf_consume(&lpuart_rx_fifo, rv);
    irq_enable();

    return rv;
}


void lpuart_flush(void)
{
    while (!tx_idle) {
        CRITICAL_SECTION_BEGIN();
        if (!tx_idle)
            HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
        CRITICAL_SECTION_END();
    }
}

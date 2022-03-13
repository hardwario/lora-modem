#include "lpuart.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_dma.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_lpuart.h>
#include "io.h"
#include "halt.h"
#include "utils.h"
#include "log.h"
#include "system.h"


static uint8_t rx_buffer[64];


static UART_HandleTypeDef port;
static void (*on_tx_done)(void);

static lpuart_rx_callback_f rx_callback;


static void rx(void)
{
    static size_t old_pos;
    size_t pos;

    pos = ARRAY_LEN(rx_buffer) - LL_DMA_GetDataLength(DMA1, LL_DMA_CHANNEL_6);
    if (pos == old_pos) return;
    if (rx_callback == NULL) return;

    if (pos > old_pos) {
        rx_callback(&rx_buffer[old_pos], pos - old_pos);
    } else {
        rx_callback(&rx_buffer[old_pos], ARRAY_LEN(rx_buffer) - old_pos);
        if (pos > 0) rx_callback(&rx_buffer[0], pos);
    }
    old_pos = pos;
}


void lpuart_init(unsigned int baudrate, void (*tx)(void))
{
    on_tx_done = tx;

    port.Instance = LPUART1;
    port.Init.Mode = UART_MODE_TX_RX;
    port.Init.BaudRate = baudrate;
    port.Init.WordLength = UART_WORDLENGTH_8B;
    port.Init.StopBits = UART_STOPBITS_1;
    port.Init.Parity = UART_PARITY_NONE;
    port.Init.HwFlowCtl = UART_HWCONTROL_NONE;

    if (HAL_UART_Init(&port) != HAL_OK)
        halt("Error while initializing UART");

    LL_LPUART_EnableIT_IDLE(port.Instance);
}


void lpuart_deinit(void)
{
    HAL_UART_DeInit(&port);
}


void init_gpio(void)
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


void deinit_gpio(void)
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


void lpuart_async_write(uint8_t *buffer, size_t length)
{
    HAL_UART_Transmit_DMA(&port, buffer, length);
}


void HAL_UART_TxCpltCallback(UART_HandleTypeDef *port)
{
    (void)port;
    if (NULL != on_tx_done) on_tx_done();
}


void lpuart_set_rx_callback(lpuart_rx_callback_f cb)
{
    rx_callback = cb;

    // Wake the MCU up from stop mode if we start receiving data over LPUART1
    UART_WakeUpTypeDef wake = { .WakeUpEvent = LL_LPUART_WAKEUP_ON_STARTBIT };
    HAL_UARTEx_StopModeWakeUpSourceConfig(&port, wake);

    if (HAL_UART_Receive_DMA(&port, rx_buffer, ARRAY_LEN(rx_buffer)) != HAL_OK)
        halt("Couldn't start DMA for LPUART1 rx path");

    HAL_UARTEx_EnableStopMode(&port);
}


void RNG_LPUART1_IRQHandler(void)
{
    if (LL_LPUART_IsEnabledIT_IDLE(port.Instance) && LL_LPUART_IsActiveFlag_IDLE(port.Instance)) {
        LL_LPUART_ClearFlag_IDLE(port.Instance);
        rx();
        system_allow_stop_mode(SYSTEM_MODULE_LPUART_RX);
        return;
    }

    if (LL_LPUART_IsEnabledIT_RXNE(port.Instance)) {
        LL_LPUART_DisableIT_RXNE(port.Instance);
        system_disallow_stop_mode(SYSTEM_MODULE_LPUART_RX);
    }

    HAL_UART_IRQHandler(&port);
}


void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *port)
{
    (void)port;
    rx();
}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    (void)handle;
    rx();
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
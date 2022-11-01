#include "lpuart.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_dma.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_lpuart.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
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

    uint32_t masked = disable_irq();

    port.Instance = LPUART1;
    port.Init.Mode = UART_MODE_TX_RX;
    port.Init.BaudRate = baudrate;
    port.Init.WordLength = UART_WORDLENGTH_8B;
    port.Init.StopBits = UART_STOPBITS_1;
    port.Init.Parity = UART_PARITY_NONE;
    port.Init.HwFlowCtl = UART_HWCONTROL_NONE;

    if (HAL_UART_Init(&port) != HAL_OK) goto error;

    __HAL_UART_DISABLE(&port);

    // Do not disable DMA on parity, framing, or noise errors. This will
    // configure the LPUART peripheral to simply not raise RXNE which will NOT
    // assert DMA request and the errorneous data is skipped. The following byte
    // will be transferred again.
    //
    LL_LPUART_DisableDMADeactOnRxErr(LPUART1);

    // Disable overrun detection. If we are not fast enough at receiving data,
    // let the new byte ovewrite the previous one without setting the overrun
    // event. The application layer (ATCI) can deal with such errors.
    LL_LPUART_DisableOverrunDetect(LPUART1);

    __HAL_UART_ENABLE(&port);
    uint32_t tickstart = HAL_GetTick();
    if (UART_WaitOnFlagUntilTimeout(&port, USART_ISR_REACK, RESET, tickstart, HAL_UART_TIMEOUT_VALUE) != HAL_OK)
        goto error;

    // Wake the MCU up from Stop mode once a full frame has been received
    UART_WakeUpTypeDef wake = { .WakeUpEvent = LL_LPUART_WAKEUP_ON_RXNE };
    HAL_UARTEx_StopModeWakeUpSourceConfig(&port, wake);

    if (HAL_UART_Receive_DMA(&port, dma_buffer, ARRAY_LEN(dma_buffer)) != HAL_OK)
        goto error;

    HAL_UARTEx_EnableStopMode(&port);

    // Enable the idle line detection interrupt. We use the event to transmit
    // data from the DMA buffer to the input FIFO queue and to re-enable the
    // low-power Stop mode.
    LL_LPUART_EnableIT_IDLE(LPUART1);

    // Disable the receive buffer not empty interrupt. We use DMA to receive
    // data over LPUART1 so that the receive process works even when interrupts
    // don't, e.g., during heavy memory bus activity (writes to EEPROM).
    LL_LPUART_DisableIT_RXNE(LPUART1);

    // Disable framing, noise, and overrun interrupt generation. We don't want
    // those errors to stop DMA transfers. We simply ignore such errors and let
    // the ATCI recover at the application layer.
    LL_LPUART_DisableIT_ERROR(LPUART1);

    reenable_irq(masked);
    return;

error:
    reenable_irq(masked);
    halt("Error while initializing LPUART port");
}



static void init_gpio(void)
{
    GPIO_InitTypeDef gpio = {
        .Mode = GPIO_MODE_AF_PP,
        .Speed = GPIO_SPEED_HIGH
    };

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2;
    gpio.Alternate = GPIO_AF6_LPUART1;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_3;
    gpio.Alternate = GPIO_AF6_LPUART1;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}


static void deinit_gpio(void)
{
    GPIO_InitTypeDef gpio = {
        .Mode = GPIO_MODE_ANALOG,
        .Pull = GPIO_NOPULL
    };

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gpio);
}


void lpuart_disable(void) {
    lpuart_flush();
    HAL_UART_DMAPause(&port);
    deinit_gpio();
}

void lpuart_enable() {
    init_gpio();
    HAL_UART_DMAResume(&port);
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
    uint32_t masked = disable_irq();
    cbuf_view_t v;

    cbuf_tail(&lpuart_tx_fifo, &v);
    reenable_irq(masked);

    size_t written = cbuf_copy_in(&v, buffer, length);

    masked = disable_irq();
    cbuf_produce(&lpuart_tx_fifo, written);

    if (tx_idle && lpuart_tx_fifo.length > 0) {
        tx_idle = 0;
        system_stop_lock |= SYSTEM_MODULE_LPUART_TX;

        cbuf_head(&lpuart_tx_fifo, &v);
        if (v.len[0]) {
            HAL_UART_Transmit_DMA(&port, (unsigned char *)v.ptr[0], v.len[0]);
            tx_len = v.len[0];
        } else {
            HAL_UART_Transmit_DMA(&port, (unsigned char *)v.ptr[1], v.len[1]);
            tx_len = v.len[1];
        }
    }

    reenable_irq(masked);
    return written;
}


void lpuart_write_blocking(const char *buffer, size_t length)
{
    uint32_t masked;
    size_t written;
    while (length) {
        written = lpuart_write(buffer, length);
        buffer += written;
        length -= written;

        if (written == 0) {
            while (lpuart_tx_fifo.max_length == lpuart_tx_fifo.length) {
                masked = disable_irq();
                // If the TX FIFO is at full capacity, we invoke system_idle to
                // put the MCU to sleep until there is some space in the output
                // FIFO which will be signalled by the ISR when the DMA transfer
                // finishes. Since transmission happens via DMA, system_idle
                // used below must not enter the Stop mode. That is, however,
                // guaranteed, since the function luart_write above creates a
                // stop mode wake lock which will still be in place when the
                // process gets here.
                if (lpuart_tx_fifo.max_length == lpuart_tx_fifo.length)
                    system_idle();
                reenable_irq(masked);
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
        system_stop_lock &= ~SYSTEM_MODULE_LPUART_TX;
        tx_len = 0;
        tx_idle = 1;
    }
}


void RNG_LPUART1_IRQHandler(void)
{
    // If we were woken up by LPUART activity, prevent the MCU from entering the
    // Stop mode until we receive an idle frame. This generally indicates
    // incoming data over LPUART1 and we want to give the DMA controller a
    // chance to transfer that data into RAM. The modem will still enter the
    // sleep mode between received bytes.
    if (LL_LPUART_IsActiveFlag_WKUP(port.Instance)) {
        LL_LPUART_ClearFlag_WKUP(port.Instance);
        system_stop_lock |= SYSTEM_MODULE_LPUART_RX;
    }

    // Once an idle frame has been received, we assume that the client is done
    // transmitting and we re-enable the Stop mode again. While in the Stop
    // mode, the MCU will be woken up by WKUP interrupt once another frame is
    // received by LPUART.
    if (LL_LPUART_IsEnabledIT_IDLE(port.Instance) && LL_LPUART_IsActiveFlag_IDLE(port.Instance)) {
        LL_LPUART_ClearFlag_IDLE(port.Instance);
        rx_callback();
        system_stop_lock &= ~SYSTEM_MODULE_LPUART_RX;
    }

    // Delegate to the HAL. But before we do that, check and clear the error
    // flags, otherwise the HAL would abort the DMA transfer. These errors are
    // actually disabled in the init function, but better be safe than sorry.

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


void lpuart_before_stop(void)
{
    HAL_UART_DMAPause(&port);
    LL_LPUART_EnableIT_WKUP(port.Instance);
}


void lpuart_after_stop(void)
{
    LL_LPUART_DisableIT_WKUP(port.Instance);
    HAL_UART_DMAResume(&port);

    // Resuming DMA re-enables the LPUART1 error interrupt, so we need to
    // disable it here again. We ignore all errors on LPUART1 and let upper
    // layers (ATCI) deal with it.
    LL_LPUART_DisableIT_ERROR(port.Instance);
}


size_t lpuart_read(char *buffer, size_t length)
{
    uint32_t masked = disable_irq();
    cbuf_view_t v;

    cbuf_head(&lpuart_rx_fifo, &v);
    reenable_irq(masked);

    size_t rv = cbuf_copy_out(buffer, &v, length);

    masked = disable_irq();
    cbuf_consume(&lpuart_rx_fifo, rv);
    reenable_irq(masked);

    return rv;
}


// Block until all data from the output FIFO buffer has been transmitted. The
// transmit process signals that condition by setting the variable tx_idle to 1
// from within the IRQ context.
void lpuart_flush(void)
{
    uint32_t masked;
    while (!tx_idle) {
        masked = disable_irq();
        if (!tx_idle)
            HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
        reenable_irq(masked);
    }
}


void HAL_UART_ErrorCallback(UART_HandleTypeDef *port)
{
    (void)port;
    log_error("LPUART1 error: %ld", port->ErrorCode);
}
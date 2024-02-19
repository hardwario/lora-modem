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
#include "cmd.h"
#include "nvm.h"

#ifndef LPUART_BUFFER_SIZE
#define LPUART_BUFFER_SIZE 512
#endif

#ifndef LPUART_DMA_BUFFER_SIZE
#define LPUART_DMA_BUFFER_SIZE 64
#endif


static UART_HandleTypeDef port;


// The actual buffer holding the data to be transmitted
static unsigned char tx_buffer[LPUART_BUFFER_SIZE];
// The number of bytes currently being transmitted by DMA (<= tx_bytes_left)
static volatile size_t tx_bytes_transmitting; 
// The number of bytes left to transmit before DMA can be paused (<= lpuart_tx_fifo.length)
static volatile size_t tx_bytes_left;         
// True if LPUART transmissions are paused
bool volatile lpuart_tx_paused;
// A circular buffer implementation over tx_buffer    
volatile cbuf_t lpuart_tx_fifo;


// The following variables implement the RX direction (host to modem)
static unsigned char dma_buffer[LPUART_DMA_BUFFER_SIZE];
static unsigned char rx_buffer[LPUART_BUFFER_SIZE];
volatile cbuf_t lpuart_rx_fifo;


#if DETACHABLE_LPUART == 1
static bool volatile attached;
#endif // DETACHABLE_LPUART


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


static void init_tx(void)
{
    cbuf_init(&lpuart_tx_fifo, tx_buffer, sizeof(tx_buffer));
    tx_bytes_transmitting = 0;
    tx_bytes_left = 0;
    lpuart_tx_paused = sysconf.async_uart ? false : true;
#if DETACHABLE_LPUART == 1
    attached = true;
#endif
}


static void init_rx(void)
{
    cbuf_init(&lpuart_rx_fifo, rx_buffer, sizeof(rx_buffer));
}


void lpuart_init(unsigned int baudrate)
{
    init_tx();
    init_rx();

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
    LL_LPUART_DisableDMADeactOnRxErr(port.Instance);

    // Disable overrun detection. If we are not fast enough at receiving data,
    // let the new byte ovewrite the previous one without setting the overrun
    // event. The application layer (ATCI) can deal with such errors.
    LL_LPUART_DisableOverrunDetect(port.Instance);

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

    // Enable the idle line detection intexrrupt. We use the event to transmit
    // data from the DMA buffer to the input FIFO queue and to re-enable the
    // low-power Stop mode.
    LL_LPUART_EnableIT_IDLE(port.Instance);

    // Disable the receive buffer not empty interrupt. We use DMA to receive
    // data over LPUART1 so that the receive process works even when interrupts
    // don't, e.g., during heavy memory bus activity (writes to EEPROM).
    LL_LPUART_DisableIT_RXNE(port.Instance);

    // Disable framing, noise, and overrun interrupt generation. We don't want
    // those errors to stop DMA transfers. We simply ignore such errors and let
    // the ATCI recover at the application layer.
    LL_LPUART_DisableIT_ERROR(port.Instance);

    // Disable parity error interrupts. Although we do not enable parity on the
    // LPUART port, we call this function anyway to be sure.
    LL_LPUART_DisableIT_PE(port.Instance);

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
        .Alternate = GPIO_AF6_LPUART1,
        .Speed = GPIO_SPEED_HIGH
    };

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_3;
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
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gpio);
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


// When called from the main thread, this function must be called with
// interrupts disabled because it is also called from the DMA transmission
// completition callback.
//
static inline void start_dma_transmission(void)
{
    cbuf_view_t v;

    // If we have an ongoing DMA transfer, do nothing. Another transmission will
    // be started from the completion callback if necessary.
    if (tx_bytes_transmitting) return;

    // If there is nothing to transmit, return. No need to check
    // lpuart_tx_fifo.length here because tx_bytes_left <= lpuart_tx_fifo.length
    if (!tx_bytes_left) {
        system_stop_lock &= ~SYSTEM_MODULE_LPUART_TX;
        return;
    }

    cbuf_head(&lpuart_tx_fifo, &v);

    int i = v.len[0] != 0 ? 0 : 1;
    tx_bytes_transmitting = tx_bytes_left < v.len[i] ? tx_bytes_left : v.len[i];
 
    if (tx_bytes_transmitting) {
        HAL_UART_Transmit_DMA(&port, (unsigned char *)v.ptr[i], tx_bytes_transmitting);
        tx_bytes_left -= tx_bytes_transmitting;
        system_stop_lock |= SYSTEM_MODULE_LPUART_TX;
    }
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

    // If we are not paused, mark the newly added data as to be transmitted
    // right away
    if (!lpuart_tx_paused) {
        tx_bytes_left += written;
        start_dma_transmission();
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
                // finishes. Since the transmission happens via DMA, system_idle
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
    (void)port;

    // Remove the just transmitted data from the circular buffer
    if (tx_bytes_transmitting) {
        cbuf_consume(&lpuart_tx_fifo, tx_bytes_transmitting);
        tx_bytes_transmitting = 0;
    }

    start_dma_transmission();
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


static inline void pause_rx_dma(void)
{
    if (HAL_IS_BIT_SET(port.Instance->CR3, USART_CR3_DMAR)
        && (port.RxState == HAL_UART_STATE_BUSY_RX)) {
        CLEAR_BIT(port.Instance->CR3, USART_CR3_DMAR);
    }
}


static inline void pause_tx_dma(void)
{
    if (HAL_IS_BIT_SET(port.Instance->CR3, USART_CR3_DMAT)
        && (port.gState == HAL_UART_STATE_BUSY_TX)) {
        CLEAR_BIT(port.Instance->CR3, USART_CR3_DMAT);
    }
}


static inline void pause_dma(void)
{
    pause_rx_dma();
    pause_tx_dma();
}


void lpuart_before_stop(void)
{
    pause_dma();
#if DETACHABLE_LPUART == 1
    if (attached)
#endif
        LL_LPUART_EnableIT_WKUP(port.Instance);
}


static inline void resume_rx_dma(void)
{
    if (port.RxState == HAL_UART_STATE_BUSY_RX) {
        /* Clear the Overrun flag before resuming the Rx transfer */
        __HAL_UART_CLEAR_FLAG(&port, UART_CLEAR_OREF);

        /* Enable the UART DMA Rx request */
        SET_BIT(port.Instance->CR3, USART_CR3_DMAR);
    }
}


static inline void resume_tx_dma(void)
{
    if (port.gState == HAL_UART_STATE_BUSY_TX) {
        SET_BIT(port.Instance->CR3, USART_CR3_DMAT);
    }
}


void lpuart_after_stop(void)
{
    LL_LPUART_DisableIT_WKUP(port.Instance);

    // We cannot use HAL_UART_DMAResume provided by the STM HAL here. That
    // function resumes both RX and TX DMA transfers and additionally re-enables
    // error interrupts. We need to resume the TX DMA here if and only if the
    // port is attached. Resuming a TX DMA while the port is detached from GPIO
    // would result in lost data.
    resume_rx_dma();
    resume_tx_dma();
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
    while (tx_bytes_transmitting) {
        masked = disable_irq();
        if (tx_bytes_transmitting)
            HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
        reenable_irq(masked);
    }
}


void HAL_UART_ErrorCallback(UART_HandleTypeDef *port)
{
    (void)port;
    log_error("LPUART1 error: %ld", port->ErrorCode);
}


void lpuart_resume_tx(void)
{
    lpuart_tx_paused = false;
    uint32_t masked = disable_irq();
    tx_bytes_left = lpuart_tx_fifo.length - tx_bytes_transmitting;
    start_dma_transmission();
    reenable_irq(masked);
}


void lpuart_pause_tx(void)
{
    lpuart_tx_paused = true;
}


#if DETACHABLE_LPUART == 1

void lpuart_detach(void)
{
    if (!attached) return;
    lpuart_pause_tx();
    deinit_gpio();
    attached = false;
}


void lpuart_attach(void)
{

    if (attached) return;
    init_gpio();
    lpuart_resume_tx();
    attached = true;
}

#endif // DETACHABLE_LPUART

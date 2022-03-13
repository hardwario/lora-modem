#include "console.h"
#include "system.h"
#include "fifo.h"
#include "lpuart.h"
#include "irq.h"

static void _console_vcom_tx_callback(void);
static void _console_vcom_rx_callback(const uint8_t *data, size_t len);

typedef struct
{
    fifo_t tx_fifo;
    fifo_t rx_fifo;
    char tx_buffer[1048];
    char rx_buffer[1024];
    uint8_t dma_buffer[32];
    __IO ITStatus vcom_ready;

} console_t;

static console_t _console;

void console_init(unsigned int baudrate)
{
    memset(&_console, 0, sizeof(_console));

    _console.vcom_ready = SET;

    fifo_init(&_console.tx_fifo, _console.tx_buffer, sizeof(_console.tx_buffer));
    fifo_init(&_console.rx_fifo, _console.rx_buffer, sizeof(_console.rx_buffer));

    lpuart_init(baudrate, _console_vcom_tx_callback);
    lpuart_set_rx_callback(_console_vcom_rx_callback);
}

size_t console_write(const char *buffer, size_t length)
{
    if (length > fifo_get_spaces(&_console.tx_fifo))
    {
        return 0;
    }

    size_t write_length = fifo_write(&_console.tx_fifo, buffer, length);

    irq_disable();

    if ((write_length > 0) && (_console.vcom_ready == SET))
    {
        uint16_t length = fifo_read(&_console.tx_fifo, _console.dma_buffer, sizeof(_console.dma_buffer));

        _console.vcom_ready = RESET;

        system_disallow_stop_mode(SYSTEM_MODULE_LPUART_TX);

        lpuart_async_write(_console.dma_buffer, length);
    }

    irq_enable();

    return write_length;
}

size_t console_read(char *buffer, size_t length)
{
    size_t rv;

    irq_disable();
    rv = fifo_read(&_console.rx_fifo, buffer, length);
    irq_enable();
    return rv;
}

void console_flush(void)
{
    while (_console.vcom_ready != SET)
    {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }
}

static void _console_vcom_tx_callback(void)
{
    uint16_t length = fifo_read(&_console.tx_fifo, _console.dma_buffer, sizeof(_console.dma_buffer));

    if (length > 0)
    {
        lpuart_async_write(_console.dma_buffer, length);
    }
    else
    {
        system_allow_stop_mode(SYSTEM_MODULE_LPUART_TX);
        _console.vcom_ready = SET;
    }
}

static void _console_vcom_rx_callback(const uint8_t *data, size_t len)
{
    fifo_write(&_console.rx_fifo, data, len);
}

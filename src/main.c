#include <LoRaWAN/Utilities/timeServer.h>
#include <loramac-node/src/radio/radio.h>
#include "atci.h"
#include "cmd.h"
#include "adc.h"
#include "sx1276io.h"
#include "config.h"
#include "io.h"
#include "lrw.h"
#include "system.h"
#include "log.h"
#include "lpuart.h"
#include "spi.h"
#include "gpio.h"
#include "rtc.h"
#include "usart.h"
#include "irq.h"
#include "part.h"
#include "eeprom.h"
#include "halt.h"
#include "nvm.h"


int main(void)
{
    system_init();
    log_init(LOG_LEVEL_DUMP, LOG_TIMESTAMP_ABS);

#ifdef DEBUG
    // If we are in debugging mode, delay initialization a bit so that we won't
    // miss any debugging or warning messages sent to the UART interface by the
    // initialization code that follow.
    rtc_delay_ms(1000);
    log_info("LoRa Module %s [LoRaMac %s] built on %s", VERSION, LIB_VERSION, BUILD_DATE);
#endif

    adc_init();
    spi_init(10000000);
    sx1276io_init();

    nvm_init();
    lrw_init(&nvm);
    cmd_init(sysconf.uart_baudrate);

    log_debug("LoRaMac: Starting");
    LoRaMacStart();
    cmd_event(CMD_EVENT_MODULE, CMD_MODULE_BOOT);

    while (1) {
        cmd_process();
        lrw_process();
        sysconf_process();

        CRITICAL_SECTION_BEGIN();
        if (lrw_irq) {
            lrw_irq = false;
        } else {
#ifndef LOW_POWER_DISABLE
            system_low_power();
#endif
        }
        CRITICAL_SECTION_END();
    }
}


void system_on_enter_stop_mode(void)
{
    spi_io_deinit();
    sx1276io_deinit();
    adc_deinit();
    // usart_io_deinit();
}


void system_on_exit_stop_mode(void)
{
    spi_io_init();
    sx1276io_init();
    // lpuart_io_init();
    // usart_io_init();
}

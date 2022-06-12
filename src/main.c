#include <LoRaWAN/Utilities/timeServer.h>
#include <loramac-node/src/radio/radio.h>
#include "atci.h"
#include "cmd.h"
#include "adc.h"
#include "sx1276io.h"
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

#ifdef DEBUG
    log_init(LOG_LEVEL_DUMP, LOG_TIMESTAMP_ABS);
#else
    log_init(LOG_LEVEL_OFF, LOG_TIMESTAMP_ABS);
#endif
    log_info("LoRa Module %s [LoRaMac %s] built on %s", VERSION, LIB_VERSION, BUILD_DATE);

    nvm_init();
    cmd_init(sysconf.uart_baudrate);

    adc_init();
    spi_init(10000000);
    sx1276io_init();

    lrw_init();
    log_debug("LoRaMac: Starting");
    LoRaMacStart();
    cmd_event(CMD_EVENT_MODULE, CMD_MODULE_BOOT);

    while (1) {
        cmd_process();
        lrw_process();
        sysconf_process();

        disable_irq();

        // If the application has scheduled a system reset, postpone it until
        // there are no more pending tasks. We don't really have the notion of
        // tasks in the modem software, however, we can tell that nothing is
        // going on once both low-power modes (sleep and stop) are not prevented
        // by any of the subsystems. The low-power sleep mode is typically
        // prevented if a subsystem requests that the application iteratures
        // through its main loop as quickly as possible, e.g., to handle an ISR
        // from the main thread. The stop mode can be prevented by hardware
        // peripherals such as LPUART1, RTC, or SX1276 while they need to finish
        // some background work.
        if (schedule_reset && !system_sleep_lock && !system_stop_lock) {
            NVIC_SystemReset();
        } else {
            system_idle();
        }

        enable_irq();

        // Invoke lrw_process as the first thing after waking up to give the MAC
        // a chance to timestamp incoming downlink as quickly as possible.
        lrw_process();
    }
}


void system_before_stop(void)
{
    spi_io_deinit();
    sx1276io_deinit();
    adc_deinit();
    lpuart_before_stop();
}


void system_after_stop(void)
{
    lpuart_after_stop();
    spi_io_init();
    sx1276io_init();
}

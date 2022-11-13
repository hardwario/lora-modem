#include <LoRaWAN/Utilities/timeServer.h>
#include <loramac-node/src/radio/radio.h>
#include "atci.h"
#include "cmd.h"
#include "adc.h"
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
#include "sx1276-board.h"


int main(void)
{
    int busy;
    system_init();

#ifdef DEBUG
    log_init(LOG_LEVEL_DUMP, LOG_TIMESTAMP_ABS);
#else
    log_init(LOG_LEVEL_OFF, LOG_TIMESTAMP_ABS);
#endif
    log_info("Open LoRaWAN modem %s [LoRaMac %s] built on %s", VERSION, LIB_VERSION, BUILD_DATE);

    nvm_init();
    cmd_init(sysconf.uart_baudrate);

    adc_init();

    SX1276.DIO0.port = GPIOB;
    SX1276.DIO0.pinIndex = GPIO_PIN_4;
    SX1276.DIO1.port = GPIOB;
    SX1276.DIO1.pinIndex = GPIO_PIN_1;
    SX1276.DIO2.port = GPIOB;
    SX1276.DIO2.pinIndex = GPIO_PIN_0;
    SX1276.DIO3.port = GPIOC;
    SX1276.DIO3.pinIndex = GPIO_PIN_13;
    SX1276.DIO4.port = GPIOA;
    SX1276.DIO4.pinIndex = GPIO_PIN_5;
    SX1276.DIO5.port = GPIOA;
    SX1276.DIO5.pinIndex = GPIO_PIN_4;
    SX1276.Reset.port = GPIOC;
    SX1276.Reset.pinIndex = GPIO_PIN_0;

    spi_init(&SX1276.Spi, 10000000);
    SX1276IoInit();

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
        // some background work. We specifically ignore the RADIO subsystem in
        // the code below and instead rely on LoRaMacIsBusy to tell us whether
        // the MAC subsystem (which owns the radio) is busy. This will allow a
        // reboot in class C where the radio is continuously listening. This
        // heuristic to determine when to perform the reset is a bit hackish,
        // but it's the best we can do in the absence of better activity
        // tracking mechanism.

        busy = system_sleep_lock | (system_stop_lock & ~SYSTEM_MODULE_RADIO) | LoRaMacIsBusy();
        if (schedule_reset && !busy) {
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
    SX1276IoDeInit();
    spi_io_deinit(&SX1276.Spi);
    adc_before_stop();
    lpuart_before_stop();
}


void system_after_stop(void)
{
    lpuart_after_stop();
    adc_after_stop();
    spi_io_init(&SX1276.Spi);
    SX1276IoInit();
}

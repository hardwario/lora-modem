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


static lrw_config_t config;


void gpio_dump(char name, GPIO_TypeDef *port)
{
    // log_debug("GPIO%c->MODER: 0x%08lX", name, port->MODER);
    // log_debug("GPIO%c->OTYPER: 0x%08lX", name, port->OTYPER);
    // log_debug("GPIO%c->OSPEEDR: 0x%08lX", name, port->OSPEEDR);
    // log_debug("GPIO%c->PUPDR: 0x%08lX", name, port->PUPDR);
    // log_debug("GPIO%c->ODR: 0x%08lX", name, port->ODR);
    // log_debug("GPIO%c->AFRL: 0x%08lX", name, port->AFR[0]);
    // log_debug("GPIO%c->AFRH: 0x%08lX", name, port->AFR[1]);

    log_debug("%c 0x%08lX 0x%08lX 0x%08lX 0x%08lX 0x%08lX 0x%08lX 0x%08lX",
              name,
              port->MODER,
              port->OTYPER,
              port->OSPEEDR,
              port->PUPDR,
              port->ODR,
              port->AFR[0],
              port->AFR[1]
    );
}

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

    // Initialize the random number generator (used by LoRaMac) early. The
    // generator is seeded from the unique number of the MCU board. This will
    // yield pseudo-random DevAddr (when one is randomly generated) derived from
    // the unique ID of the MCU.
    srand1(system_get_random_seed());

    // Load configuration from EEPROM. If no configuation is found, initialize
    // the configuration from defaults.
    //config_init(&config, sizeof(config), NULL);

    adc_init();

    spi_init(10000000);

    sx1276io_init();

    lrw_init(&config, 8);

    log_debug("LoRaMac: Starting");
    LoRaMacStart();

    cmd_init();
    cmd_event(CMD_EVENT_MODULE, CMD_MODULE_BOOT);

    while (1) {
        cmd_process();
        lrw_process();

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

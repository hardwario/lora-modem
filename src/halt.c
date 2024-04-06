#include "halt.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_exti.h>
#include "lpuart.h"
#include "log.h"
#include "system.h"
#include "cmd.h"
#include "irq.h"


__attribute__((noreturn)) void halt(const char *msg)
{
#if DEBUG_LOG != 0
    const char prefix[] = "Halted";
#endif
    cmd_event(CMD_EVENT_MODULE, CMD_MODULE_HALT);

    if (msg == NULL) {
        log_error(prefix);
    } else {
        log_error("%s: %s\r\n", prefix, msg);
    }

    lpuart_flush();

    disable_irq();

    // Make sure we can enter the low-power Stop mode
    system_sleep_lock = 0;
    system_stop_lock = 0;

    // Mask all EXTI interrupts and events to ensure we're not woken up. The
    // only way of recovering from a halt should be via the external reset pin.
    // This is to ensure that the LoRa modem doesn't drain the device's battery
    // while being halted due to an irrecoverable error.
    EXTI->IMR = LL_EXTI_LINE_NONE;
    EXTI->EMR = LL_EXTI_LINE_NONE;

    // Hopefully, we can enter the low-power Stop mode now. Note that if there
    // are any pending interrupts, the MCU will not enter Sleep or Stop modes
    // and the loop below will keep spinning. We have tried preventing that by
    // masking all EXTI interrupts and events above.
    for(;;) system_idle();
}

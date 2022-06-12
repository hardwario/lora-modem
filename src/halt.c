#include "halt.h"
#include "lpuart.h"
#include "log.h"
#include "system.h"
#include "cmd.h"
#include "irq.h"


void halt(const char *msg)
{
#if defined(DEBUG)
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
    // Note that if there are any pending interrupts, the MCU will not enter
    // Sleep or Stop modes and the loop below will keep spinning.

    for(;;) {
        system_idle();
    }
}

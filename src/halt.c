#include "halt.h"
#include "lpuart.h"
#include "log.h"
#include "system.h"
#include "cmd.h"
#include "irq.h"


void halt(const char *msg)
{
    const char prefix[] = "Halted";

    cmd_event(CMD_EVENT_MODULE, CMD_MODULE_HALT);

    if (msg == NULL) {
        log_error(prefix);
    } else {
        log_error("%s: %s\r\n", prefix, msg);
    }

    lpuart_flush();
    irq_disable();
    while (1) {
        system_low_power();
    }
}

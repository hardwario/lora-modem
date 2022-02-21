#include "halt.h"
#include "console.h"
#include "log.h"
#include "system.h"


void halt(const char *msg)
{
    const char prefix[] = "Halted";

    console_write(prefix, sizeof(prefix) - 1);
    if (msg == NULL) {
        log_error(prefix);
    } else {
        console_write(": ", 2);
        console_write(msg, strlen(msg));
        log_error("%s: %s\r\n", prefix, msg);
    }
    console_write("\r\n", 2);

    while (1) {
        system_low_power();
    }
}

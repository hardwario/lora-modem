#include "error.h"
#include "console.h"
#include "system.h"

void error_handler()
{
    const char *text = "error_handler\r\n";
    console_write(text, sizeof(text));

    while (1)
    {
        system_low_power();
    }
}

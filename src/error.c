#include "error.h"
#include "low_power_manager.h"
#include "console.h"

void error_handler()
{
    const char *text = "error_handler\r\n";
    console_write(text, sizeof(text));

    while (1)
    {
        LPM_EnterLowPower();
    }
}

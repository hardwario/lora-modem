#ifndef _DELAY_H
#define _DELAY_H

#include "rtc.h"

//! @brief  Blocking delay of "s" seconds

#define Delay(s) rtc_delay_ms(ms * 1000);

//! @brief Blocking delay of "ms" milliseconds

#ifndef DelayMs
#define DelayMs(ms) rtc_delay_ms(ms)
#endif

#endif // _DELAY_H
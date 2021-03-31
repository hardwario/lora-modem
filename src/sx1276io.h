#ifndef _SX1276IO_H
#define _SX1276IO_H

#include "common.h"

// for TCXO
#define BOARD_WAKEUP_TIME  5
#define RF_MID_BAND_THRESH 525000000

//! @brief Initializes the radio I/Os pins interface

void sx1276io_init(void);

//! @brief De-initializes the radio I/Os pins interface. (For MCU lowpower modes)

void sx1276io_deinit(void);

#endif // _SX1276IO_H

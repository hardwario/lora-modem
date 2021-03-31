#ifndef _SYSTEM_H
#define _SYSTEM_H

#include "common.h"

//! @brief System init

void system_init(void);

//! @brief Reset MCU

void system_reset(void);

//! @brief Get pseudo random seed generated using the MCU Unique ID

uint32_t system_get_random_seed(void);

//! @brief This function return a unique ID
//! @param[out] id Pointer to destination buffer, size 8 bytes

void system_get_unique_id(uint8_t *id);

typedef enum
{
    SYSTEM_LP_RTC = (1 << 0),
    SYSTEM_LP_UART = (1 << 1),
} system_mask_t;

void system_stop_mode_enable(system_mask_t mask);

void system_stop_mode_disable(system_mask_t mask);

bool system_is_stop_mode(void);

//! @brief Go to low power, sleep mode or stop mode

void system_low_power();

//! @brief This function call on enter to stop mode (weak)

void system_on_enter_stop_mode(void);

//! @brief This function call on exit from stop mode (weak)

void system_on_exit_stop_mode(void);

#endif

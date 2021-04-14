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

//! @brief Aait on HSI

void system_wait_hsi(void);

//! @brief Stop mode mask subsystem
typedef enum
{
    SYSTEM_MASK_RTC = (1 << 0),
    SYSTEM_MASK_LPUART = (1 << 1),
    SYSTEM_MASK_USART = (1 << 2),
    SYSTEM_MASK_RADIO = (1 << 3),
} system_mask_t;

//! @brief Enable stop mode
//! @param[in] mask Mask subsystem

void system_stop_mode_enable(system_mask_t mask);

//! @brief Disable stop mode
//! @param[in] mask Mask subsystem

void system_stop_mode_disable(system_mask_t mask);

bool system_is_stop_mode(void);

system_mask_t system_get_stop_mode_mask(void);

//! @brief Go to low power, sleep mode or stop mode

void system_low_power(void);

//! @brief This function call on enter to stop mode (weak)

void system_on_enter_stop_mode(void);

//! @brief This function call on exit from stop mode (weak)

void system_on_exit_stop_mode(void);

#endif

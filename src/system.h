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

//! @brief Sleep lock and Stop mode mask
typedef enum
{
    SYSTEM_MODULE_RTC       = (1 << 0),
    SYSTEM_MODULE_LPUART_RX = (1 << 1),
    SYSTEM_MODULE_LPUART_TX = (1 << 2),
    SYSTEM_MODULE_USART     = (1 << 3),
    SYSTEM_MODULE_RADIO     = (1 << 5),
    SYSTEM_MODULE_ATCI      = (1 << 6),
    SYSTEM_MODULE_NVM       = (1 << 7)
} system_module_t;


void system_allow_stop_mode(system_module_t module);

void system_disallow_stop_mode(system_module_t module);

bool system_is_stop_mode_allowed(void);

unsigned system_get_stop_mode_mask(void);

int system_is_sleep_allowed(void);

void system_allow_sleep(system_module_t module);

void system_disallow_sleep(system_module_t module);

unsigned system_get_sleep_mask(void);

//! @brief Go to low power, sleep mode or stop mode. The function must be
//! invoked with interrupts disabled.

void system_sleep(void);

//! @brief This function call on enter to stop mode (weak)

void system_on_enter_stop_mode(void);

//! @brief This function call on exit from stop mode (weak)

void system_on_exit_stop_mode(void);

#endif

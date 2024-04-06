#ifndef _SYSTEM_H
#define _SYSTEM_H

#include <stdint.h>

extern volatile unsigned system_stop_lock;
extern volatile unsigned system_sleep_lock;

//! @brief System init

void system_init(void);

//! @brief Get a pseudo-random seed generated using the MCU Unique ID

uint32_t system_get_random_seed(void);

//! @brief This function returns a unique ID
//! @param[out] id Pointer to the destination buffer, size 8 bytes

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
    SYSTEM_MODULE_RADIO     = (1 << 4),
    SYSTEM_MODULE_ATCI      = (1 << 5),
    SYSTEM_MODULE_NVM       = (1 << 6),
    SYSTEM_MODULE_LORA      = (1 << 7)
} system_module_t;


//! @brief Go to low power, sleep mode, or stop mode. The function must be
//! invoked with interrupts disabled.

void system_idle(void);

//! @brief This function is called on an enter to the stop mode (weak)

void system_before_stop(void);

//! @brief This function is called on an exit from the stop mode (weak)

void system_after_stop(void);

#endif

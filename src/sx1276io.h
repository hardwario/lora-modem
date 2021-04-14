#ifndef _SX1276IO_H
#define _SX1276IO_H

#include "common.h"
#include "sx1276.h"

//! @brief Initializes the radio I/Os pins interface

void sx1276io_init(void);

//! @brief De-initializes the radio I/Os pins interface. (For MCU lowpower modes)

void sx1276io_deinit(void);

//! @brief Initializes DIO IRQ handlers
//! @param[in] irqHandlers Array containing the IRQ callback functions

void sx1276io_irq_init(DioIrqHandler **irqHandlers);

//! @brief Resets the radio

void sx1276io_reset(void);

//! @brief Enables/disables the TCXO if available on board design.
//! @param[in] state TCXO enabled when true and disabled when false.

void sx1276io_set_tcxo(bool state);

//! @brief Sets the radio output power.
//! @param[in] power Sets the RF output power

void sx1276io_rf_tx_set_power(int8_t power);

//! @brief Set the RF Switch I/Os pins in Low Power mode
//! @param[in] status enable or disable

void sx1276io_ant_set_sw_low_power(bool status);

//! @brief Controls the antenna switch if necessary.
//! @note see errata note
//! param[in] op_mode Current radio operating mode

void sx1276io_ant_set_sw(uint8_t op_mode);

//! @brief Gets the Defines the time required for the TCXO to wakeup [ms].
//! @retval time Board TCXO wakeup time in ms.

uint32_t sx1276io_get_tcxo_wakeup_time(void);

//! @brief Gets current state of DIO1 pin state (FifoLevel).
//! @retval state DIO1 pin current state.

uint32_t sx1276io_get_dio1_state(void);

#endif // _SX1276IO_H

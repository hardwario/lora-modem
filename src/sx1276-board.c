#include "sx1276-board.h"
#include <loramac-node/src/radio/radio.h>
#include <loramac-node/src/radio/sx1276/sx1276.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include "gpio.h"
#include "rtc.h"
#include "system.h"
#include "log.h"
#include "radio.h"
#include "irq.h"

#define TCXO_VCC_PORT            GPIOA
#define TCXO_VCC_PIN             GPIO_PIN_12

#define ANT_SWITCH_PORT_RX       GPIOA //CRF1
#define ANT_SWITCH_PIN_RX        GPIO_PIN_1

#define ANT_SWITCH_PORT_TX_BOOST GPIOC //CRF3
#define ANT_SWITCH_PIN_TX_BOOST  GPIO_PIN_1

#define ANT_SWITCH_PORT_TX_RFO   GPIOC //CRF2
#define ANT_SWITCH_PIN_TX_RFO    GPIO_PIN_2


#define IRQ_PRIORITY  0
#define TCXO_WAKEUP_TIME 5


static bool radio_is_active = false;


void SX1276IoInit(void)
{
    GPIO_InitTypeDef cfg = {
        .Mode = GPIO_MODE_IT_RISING_FALLING,
        .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_HIGH
    };

    gpio_init(SX1276.DIO1.port, SX1276.DIO1.pinIndex, &cfg);

    cfg.Mode = GPIO_MODE_IT_RISING;
    gpio_init(SX1276.DIO0.port, SX1276.DIO0.pinIndex, &cfg);
    gpio_init(SX1276.DIO2.port, SX1276.DIO2.pinIndex, &cfg);
    gpio_init(SX1276.DIO3.port, SX1276.DIO3.pinIndex, &cfg);
    gpio_init(SX1276.DIO4.port, SX1276.DIO4.pinIndex, &cfg);

    // RADIO_TCXO_POWER
    static bool initialized = false;
    if (!initialized) {
        cfg.Mode = GPIO_MODE_OUTPUT_PP;
        cfg.Pull = GPIO_NOPULL;
        gpio_write(TCXO_VCC_PORT, TCXO_VCC_PIN, 0);
        gpio_init(TCXO_VCC_PORT, TCXO_VCC_PIN, &cfg);
        initialized = true;
    }
}


void SX1276IoDeInit(void)
{
    GPIO_InitTypeDef cfg = {
        .Mode = GPIO_MODE_ANALOG,
        .Pull = GPIO_NOPULL
    };

    gpio_init(SX1276.DIO0.port, SX1276.DIO0.pinIndex, &cfg);
    gpio_init(SX1276.DIO1.port, SX1276.DIO1.pinIndex, &cfg);
    gpio_init(SX1276.DIO2.port, SX1276.DIO2.pinIndex, &cfg);
    gpio_init(SX1276.DIO3.port, SX1276.DIO3.pinIndex, &cfg);
    gpio_init(SX1276.DIO4.port, SX1276.DIO4.pinIndex, &cfg);
    gpio_init(SX1276.DIO5.port, SX1276.DIO5.pinIndex, &cfg);
}


void SX1276IoIrqInit(DioIrqHandler **irq)
{
    gpio_set_irq(SX1276.DIO0.port, SX1276.DIO0.pinIndex, IRQ_PRIORITY, irq[0]);
    gpio_set_irq(SX1276.DIO1.port, SX1276.DIO1.pinIndex, IRQ_PRIORITY, irq[1]);
    gpio_set_irq(SX1276.DIO2.port, SX1276.DIO2.pinIndex, IRQ_PRIORITY, irq[2]);
    gpio_set_irq(SX1276.DIO3.port, SX1276.DIO3.pinIndex, IRQ_PRIORITY, irq[3]);
    gpio_set_irq(SX1276.DIO4.port, SX1276.DIO4.pinIndex, IRQ_PRIORITY, irq[4]);
}


void SX1276Reset(void)
{
    // Enables the TCXO if available on the board design
    SX1276SetBoardTcxo(true);

    // Set RESET pin to 0
    GPIO_InitTypeDef cfg = {
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_HIGH
    };

    gpio_write(SX1276.Reset.port, SX1276.Reset.pinIndex, 0);
    gpio_init(SX1276.Reset.port, SX1276.Reset.pinIndex, &cfg);

    rtc_delay_ms(1);

    // Configure RESET as input
    cfg.Mode = GPIO_MODE_ANALOG;
    gpio_init(SX1276.Reset.port, SX1276.Reset.pinIndex, &cfg);

    rtc_delay_ms(6);
}


void SX1276SetBoardTcxo(uint8_t state)
{
    if (state) {
        // if TCXO OFF power it up.
        if (gpio_read(TCXO_VCC_PORT, TCXO_VCC_PIN) == 0) {
            // Power ON the TCXO
            log_debug("SX1276SetBoardTcxo: %d", state);
            gpio_write(TCXO_VCC_PORT, TCXO_VCC_PIN, 1);
            DelayMs(TCXO_WAKEUP_TIME);
        }
    } else {
        // Power OFF the TCXO
        log_debug("SX1276SetBoardTcxo: %d", state);
        gpio_write(TCXO_VCC_PORT, TCXO_VCC_PIN, 0);
    }
}


void SX1276SetRfTxPower(int8_t power)
{
    uint8_t paconfig = SX1276Read(REG_PACONFIG);
    uint8_t paDac = SX1276Read(REG_PADAC);

    uint8_t paselect = power > 14 ? RF_PACONFIG_PASELECT_PABOOST : RF_PACONFIG_PASELECT_RFO;

    paconfig = (paconfig & RF_PACONFIG_PASELECT_MASK) | paselect;

    if ((paconfig & RF_PACONFIG_PASELECT_PABOOST) == RF_PACONFIG_PASELECT_PABOOST) {
        if (power > 17) {
            paDac = (paDac & RF_PADAC_20DBM_MASK) | RF_PADAC_20DBM_ON;
        } else {
            paDac = (paDac & RF_PADAC_20DBM_MASK) | RF_PADAC_20DBM_OFF;
        }
        if ((paDac & RF_PADAC_20DBM_ON) == RF_PADAC_20DBM_ON ) {
            if (power < 5) power = 5;
            if (power > 20) power = 20;
            paconfig = (paconfig & RF_PACONFIG_OUTPUTPOWER_MASK ) | (uint8_t)((uint16_t)(power - 5) & 0x0F);
        } else {
            if (power < 2) power = 2;
            if (power > 17) power = 17;
            paconfig = (paconfig & RF_PACONFIG_OUTPUTPOWER_MASK) | (uint8_t)((uint16_t)(power - 2) & 0x0F);
        }
    } else {
        if (power > 0) {
            if (power > 15) power = 15;
            paconfig = (paconfig & RF_PACONFIG_MAX_POWER_MASK & RF_PACONFIG_OUTPUTPOWER_MASK) | (7 << 4) | (power);
        } else {
            if (power < -4) power = -4;
            paconfig = (paconfig & RF_PACONFIG_MAX_POWER_MASK & RF_PACONFIG_OUTPUTPOWER_MASK) | (0 << 4) | (power + 4);
        }
    }
    SX1276Write(REG_PACONFIG, paconfig);
    SX1276Write(REG_PADAC, paDac);
}


void SX1276SetAntSwLowPower(bool status)
{
    uint32_t mask;
    if (radio_is_active == status) return;

    log_debug("SX1276SetAntSwLowPower: %d", status);
    radio_is_active = status;

    GPIO_InitTypeDef cfg = {
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_HIGH
    };

    if (status == false) {
        cfg.Mode = GPIO_MODE_OUTPUT_PP;

        gpio_write(ANT_SWITCH_PORT_RX, ANT_SWITCH_PIN_RX, 0);
        gpio_init(ANT_SWITCH_PORT_RX, ANT_SWITCH_PIN_RX, &cfg);

        gpio_write(ANT_SWITCH_PORT_TX_BOOST, ANT_SWITCH_PIN_TX_BOOST, 0);
        gpio_init(ANT_SWITCH_PORT_TX_BOOST, ANT_SWITCH_PIN_TX_BOOST, &cfg);

        gpio_write(ANT_SWITCH_PORT_TX_RFO, ANT_SWITCH_PIN_TX_RFO, 0);
        gpio_init(ANT_SWITCH_PORT_TX_RFO, ANT_SWITCH_PIN_TX_RFO, &cfg);

        mask = disable_irq();
        system_stop_lock |= SYSTEM_MODULE_RADIO;
        reenable_irq(mask);
    } else {
        mask = disable_irq();
        system_stop_lock &= ~SYSTEM_MODULE_RADIO;
        reenable_irq(mask);

        cfg.Mode = GPIO_MODE_ANALOG;

        gpio_write(ANT_SWITCH_PORT_RX, ANT_SWITCH_PIN_RX, 0);
        gpio_init(ANT_SWITCH_PORT_RX, ANT_SWITCH_PIN_RX, &cfg);

        gpio_write(ANT_SWITCH_PORT_TX_BOOST, ANT_SWITCH_PIN_TX_BOOST, 0);
        gpio_init(ANT_SWITCH_PORT_TX_BOOST, ANT_SWITCH_PIN_TX_BOOST, &cfg);

        gpio_write(ANT_SWITCH_PORT_TX_RFO, ANT_SWITCH_PIN_TX_RFO, 0);
        gpio_init(ANT_SWITCH_PORT_TX_RFO, ANT_SWITCH_PIN_TX_RFO, &cfg);
    }
}


void SX1276SetAntSw(uint8_t op_mode)
{
    uint8_t paconfig = SX1276Read(REG_PACONFIG);
    switch(op_mode) {
    case RFLR_OPMODE_TRANSMITTER:
        if ((paconfig & RF_PACONFIG_PASELECT_PABOOST) == RF_PACONFIG_PASELECT_PABOOST) {
            // GpioWrite( &AntSwitchTxBoost, 1 );
            gpio_write(ANT_SWITCH_PORT_TX_BOOST, ANT_SWITCH_PIN_TX_BOOST, 1);
        } else {
            // GpioWrite( &AntSwitchTxRfo, 1 );
            gpio_write(ANT_SWITCH_PORT_TX_RFO, ANT_SWITCH_PIN_TX_RFO, 1);
        }
        break;

    case RFLR_OPMODE_RECEIVER:
    case RFLR_OPMODE_RECEIVER_SINGLE:
    case RFLR_OPMODE_CAD:
    default:
        // GpioWrite( &AntSwitchRx, 1 );
        gpio_write(ANT_SWITCH_PORT_RX, ANT_SWITCH_PIN_RX, 1);
        break;
    }
}


uint32_t SX1276GetBoardTcxoWakeupTime(void)
{
    return TCXO_WAKEUP_TIME;
}


uint32_t SX1276GetDio1PinState(void)
{
    return gpio_read(SX1276.DIO1.port, SX1276.DIO1.pinIndex);
}

#include "sx1276io.h"
#include <loramac-node/src/radio/radio.h>
#include <loramac-node/src/radio/sx1276/sx1276.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include "gpio.h"
#include "rtc.h"
#include "system.h"
#include "log.h"
#include "radio.h"
#include "irq.h"

#define SX1276IO_IRQ_PRIORITY  0
#define SX1276IO_TCXO_WAKEUP_TIME 5


static bool sx1276io_radio_is_active = false;

void sx1276io_init(void)
{
    GPIO_InitTypeDef cfg = {
        .Mode = GPIO_MODE_IT_RISING_FALLING,
        .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_HIGH
    };

    gpio_init(RADIO_DIO_1_PORT, RADIO_DIO_1_PIN, &cfg);

    cfg.Mode = GPIO_MODE_IT_RISING;
    gpio_init(RADIO_DIO_0_PORT, RADIO_DIO_0_PIN, &cfg);
    gpio_init(RADIO_DIO_2_PORT, RADIO_DIO_2_PIN, &cfg);
    gpio_init(RADIO_DIO_3_PORT, RADIO_DIO_3_PIN, &cfg);
    gpio_init(RADIO_DIO_4_PORT, RADIO_DIO_4_PIN, &cfg);

    // RADIO_TCXO_POWER
    static bool initialized = false;
    if (!initialized) {
        cfg.Mode = GPIO_MODE_OUTPUT_PP;
        cfg.Pull = GPIO_NOPULL;
        gpio_write(RADIO_TCXO_VCC_PORT, RADIO_TCXO_VCC_PIN, 0);
        gpio_init(RADIO_TCXO_VCC_PORT, RADIO_TCXO_VCC_PIN, &cfg);
        initialized = true;
    }
}

void sx1276io_deinit(void)
{
    GPIO_InitTypeDef cfg = {
        .Mode = GPIO_MODE_ANALOG,
        .Pull = GPIO_NOPULL
    };

    gpio_init(RADIO_DIO_0_PORT, RADIO_DIO_0_PIN, &cfg);
    gpio_init(RADIO_DIO_1_PORT, RADIO_DIO_1_PIN, &cfg);
    gpio_init(RADIO_DIO_2_PORT, RADIO_DIO_2_PIN, &cfg);
    gpio_init(RADIO_DIO_3_PORT, RADIO_DIO_3_PIN, &cfg);
    gpio_init(RADIO_DIO_4_PORT, RADIO_DIO_4_PIN, &cfg);
    gpio_init(RADIO_DIO_5_PORT, RADIO_DIO_5_PIN, &cfg);
}

void sx1276io_irq_init(DioIrqHandler **irqHandlers)
{
    gpio_set_irq(RADIO_DIO_0_PORT, RADIO_DIO_0_PIN, SX1276IO_IRQ_PRIORITY, irqHandlers[0]);
    gpio_set_irq(RADIO_DIO_1_PORT, RADIO_DIO_1_PIN, SX1276IO_IRQ_PRIORITY, irqHandlers[1]);
    gpio_set_irq(RADIO_DIO_2_PORT, RADIO_DIO_2_PIN, SX1276IO_IRQ_PRIORITY, irqHandlers[2]);
    gpio_set_irq(RADIO_DIO_3_PORT, RADIO_DIO_3_PIN, SX1276IO_IRQ_PRIORITY, irqHandlers[3]);
    gpio_set_irq(RADIO_DIO_4_PORT, RADIO_DIO_4_PIN, SX1276IO_IRQ_PRIORITY, irqHandlers[4]);
}

void sx1276io_reset( void )
{
    // Enables the TCXO if available on the board design
    sx1276io_set_tcxo(true);

    // Set RESET pin to 0
    GPIO_InitTypeDef initStruct = { 0 };

    initStruct.Mode =GPIO_MODE_OUTPUT_PP;
    initStruct.Pull = GPIO_NOPULL;
    initStruct.Speed = GPIO_SPEED_HIGH;
    gpio_write(RADIO_RESET_PORT, RADIO_RESET_PIN, 0 );
    gpio_init(RADIO_RESET_PORT, RADIO_RESET_PIN, &initStruct );

    rtc_delay_ms( 1 );

    // Configure RESET as input
    initStruct.Mode = GPIO_MODE_ANALOG;
    gpio_init(RADIO_RESET_PORT, RADIO_RESET_PIN, &initStruct );

    rtc_delay_ms( 6 );
}

void sx1276io_set_tcxo(bool state)
{
    log_debug("sx1276io_set_tcxo: %d", state);
    if(state)
    {
        // if TCXO OFF power it up.
        if (gpio_read(RADIO_TCXO_VCC_PORT, RADIO_TCXO_VCC_PIN) == 0)
        {
            // Power ON the TCXO
            gpio_write(RADIO_TCXO_VCC_PORT, RADIO_TCXO_VCC_PIN, 1);
            DelayMs( SX1276IO_TCXO_WAKEUP_TIME );
        }
    }
    else
    {
        // Power OFF the TCXO
        gpio_write(RADIO_TCXO_VCC_PORT, RADIO_TCXO_VCC_PIN, 0);
    }
}

void sx1276io_rf_tx_set_power(int8_t power)
{
    uint8_t paconfig = 0;
    uint8_t paDac = 0;

    paconfig = SX1276Read( REG_PACONFIG );
    paDac = SX1276Read( REG_PADAC );

    uint8_t paselect = power > 14 ? RF_PACONFIG_PASELECT_PABOOST : RF_PACONFIG_PASELECT_RFO;

    paconfig = ( paconfig & RF_PACONFIG_PASELECT_MASK ) | paselect;

    if( ( paconfig & RF_PACONFIG_PASELECT_PABOOST ) == RF_PACONFIG_PASELECT_PABOOST )
    {
        if( power > 17 )
        {
            paDac = ( paDac & RF_PADAC_20DBM_MASK ) | RF_PADAC_20DBM_ON;
        }
        else
        {
            paDac = ( paDac & RF_PADAC_20DBM_MASK ) | RF_PADAC_20DBM_OFF;
        }
        if( ( paDac & RF_PADAC_20DBM_ON ) == RF_PADAC_20DBM_ON )
        {
            if( power < 5 )
            {
                power = 5;
            }
            if( power > 20 )
            {
                power = 20;
            }
            paconfig = ( paconfig & RF_PACONFIG_OUTPUTPOWER_MASK ) | ( uint8_t )( ( uint16_t )( power - 5 ) & 0x0F );
        }
        else
        {
            if( power < 2 )
            {
                power = 2;
            }
            if( power > 17 )
            {
                power = 17;
            }
            paconfig = ( paconfig & RF_PACONFIG_OUTPUTPOWER_MASK ) | ( uint8_t )( ( uint16_t )( power - 2 ) & 0x0F );
        }
    }
    else
    {
        if( power > 0 )
        {
            if( power > 15 )
            {
                power = 15;
            }
            paconfig = ( paconfig & RF_PACONFIG_MAX_POWER_MASK & RF_PACONFIG_OUTPUTPOWER_MASK ) | ( 7 << 4 ) | ( power );
        }
        else
        {
            if( power < -4 )
            {
                power = -4;
            }
            paconfig = ( paconfig & RF_PACONFIG_MAX_POWER_MASK & RF_PACONFIG_OUTPUTPOWER_MASK ) | ( 0 << 4 ) | ( power + 4 );
        }
    }
    SX1276Write( REG_PACONFIG, paconfig );
    SX1276Write( REG_PADAC, paDac );
}

void sx1276io_ant_set_sw_low_power(bool status)
{
    uint32_t mask;
    log_debug("sx1276io_ant_set_sw_low_power: %d", status);

    if( sx1276io_radio_is_active != status )
    {
        sx1276io_radio_is_active = status;

        GPIO_InitTypeDef initStruct = {0};

        if( status == false )
        {

            initStruct.Mode = GPIO_MODE_OUTPUT_PP;
            initStruct.Pull = GPIO_NOPULL;
            initStruct.Speed = GPIO_SPEED_HIGH;

            gpio_write(RADIO_ANT_SWITCH_PORT_RX, RADIO_ANT_SWITCH_PIN_RX, 0);
            gpio_init(RADIO_ANT_SWITCH_PORT_RX, RADIO_ANT_SWITCH_PIN_RX, &initStruct);

            gpio_write(RADIO_ANT_SWITCH_PORT_TX_BOOST, RADIO_ANT_SWITCH_PIN_TX_BOOST, 0);
            gpio_init(RADIO_ANT_SWITCH_PORT_TX_BOOST, RADIO_ANT_SWITCH_PIN_TX_BOOST, &initStruct);

            gpio_write(RADIO_ANT_SWITCH_PORT_TX_RFO, RADIO_ANT_SWITCH_PIN_TX_RFO, 0);
            gpio_init(RADIO_ANT_SWITCH_PORT_TX_RFO, RADIO_ANT_SWITCH_PIN_TX_RFO, &initStruct);

            mask = disable_irq();
            system_stop_lock |= SYSTEM_MODULE_RADIO;
            reenable_irq(mask);
        }
        else
        {
            mask = disable_irq();
            system_stop_lock &= ~SYSTEM_MODULE_RADIO;
            reenable_irq(mask);

            initStruct.Mode = GPIO_MODE_ANALOG;
            initStruct.Pull = GPIO_NOPULL;
            initStruct.Speed = GPIO_SPEED_HIGH;

            gpio_write(RADIO_ANT_SWITCH_PORT_RX, RADIO_ANT_SWITCH_PIN_RX, 0);
            gpio_init(RADIO_ANT_SWITCH_PORT_RX, RADIO_ANT_SWITCH_PIN_RX, &initStruct);

            gpio_write(RADIO_ANT_SWITCH_PORT_TX_BOOST, RADIO_ANT_SWITCH_PIN_TX_BOOST, 0);
            gpio_init(RADIO_ANT_SWITCH_PORT_TX_BOOST, RADIO_ANT_SWITCH_PIN_TX_BOOST, &initStruct);

            gpio_write(RADIO_ANT_SWITCH_PORT_TX_RFO, RADIO_ANT_SWITCH_PIN_TX_RFO, 0);
            gpio_init(RADIO_ANT_SWITCH_PORT_TX_RFO, RADIO_ANT_SWITCH_PIN_TX_RFO, &initStruct);
        }
    }
}

void sx1276io_ant_set_sw(uint8_t op_mode)
{
    uint8_t paconfig = SX1276Read(REG_PACONFIG);
    switch(op_mode)
    {
    case RFLR_OPMODE_TRANSMITTER:
        if( ( paconfig & RF_PACONFIG_PASELECT_PABOOST ) == RF_PACONFIG_PASELECT_PABOOST )
        {
            // GpioWrite( &AntSwitchTxBoost, 1 );
            gpio_write(RADIO_ANT_SWITCH_PORT_TX_BOOST, RADIO_ANT_SWITCH_PIN_TX_BOOST, 1);
        }
        else
        {
            // GpioWrite( &AntSwitchTxRfo, 1 );
            gpio_write(RADIO_ANT_SWITCH_PORT_TX_RFO, RADIO_ANT_SWITCH_PIN_TX_RFO, 1);
        }
        break;
    case RFLR_OPMODE_RECEIVER:
    case RFLR_OPMODE_RECEIVER_SINGLE:
    case RFLR_OPMODE_CAD:
    default:
        // GpioWrite( &AntSwitchRx, 1 );
        gpio_write(RADIO_ANT_SWITCH_PORT_RX, RADIO_ANT_SWITCH_PIN_RX, 1);
        break;
    }
}

uint32_t sx1276io_get_tcxo_wakeup_time(void)
{
    return SX1276IO_TCXO_WAKEUP_TIME;
}

uint32_t sx1276io_get_dio1_state(void)
{
    return gpio_read(RADIO_DIO_1_PORT, RADIO_DIO_1_PIN);
}

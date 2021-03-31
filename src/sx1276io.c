#include "sx1276io.h"
#include "radio.h"
#include "sx1276.h"
#include "gpio.h"
#include "io.h"
#include "rtc.h"

#define _SX1276IO_IRQ_PRIORITY  0

static void sx1276io_set_xo(uint8_t state);
static uint32_t sx1276io_get_wake_time(void);
static void sx1276io_irq_init(DioIrqHandler **irqHandlers);
static void sx1276io_rf_tx_set_power(int8_t power);
static void sx1276io_ant_set_sw_low_power(bool status);
static void sx1276io_ant_set_sw(uint8_t opMode);

void sx1276io_init(void)
{
    GPIO_InitTypeDef initStruct = {0};

    static LoRaBoardCallback_t callbacks = {sx1276io_set_xo,
                                            sx1276io_get_wake_time,
                                            sx1276io_irq_init,
                                            sx1276io_rf_tx_set_power,
                                            sx1276io_ant_set_sw_low_power,
                                            sx1276io_ant_set_sw};

    SX1276BoardInit(&callbacks);

    initStruct.Mode = GPIO_MODE_IT_RISING;
    initStruct.Pull = GPIO_PULLDOWN;
    initStruct.Speed = GPIO_SPEED_HIGH;

    gpio_init(RADIO_DIO_0_PORT, RADIO_DIO_0_PIN, &initStruct);
    gpio_init(RADIO_DIO_1_PORT, RADIO_DIO_1_PIN, &initStruct);
    gpio_init(RADIO_DIO_2_PORT, RADIO_DIO_2_PIN, &initStruct);
    gpio_init(RADIO_DIO_3_PORT, RADIO_DIO_3_PIN, &initStruct);
#ifdef RADIO_DIO_4
    gpio_init(RADIO_DIO_4_PORT, RADIO_DIO_4_PIN, &initStruct);
#endif
#ifdef RADIO_DIO_5
    gpio_init(RADIO_DIO_5_PORT, RADIO_DIO_5_PIN, &initStruct);
#endif
    initStruct.Mode = GPIO_MODE_OUTPUT_PP;
    initStruct.Pull = GPIO_NOPULL;
    gpio_init(RADIO_TCXO_VCC_PORT, RADIO_TCXO_VCC_PIN, &initStruct);
}

void sx1276io_deinit(void)
{
    GPIO_InitTypeDef initStruct = {0};

    initStruct.Mode = GPIO_MODE_IT_RISING; //GPIO_MODE_ANALOG;
    initStruct.Pull = GPIO_PULLDOWN;

    gpio_init(RADIO_DIO_0_PORT, RADIO_DIO_0_PIN, &initStruct);
    gpio_init(RADIO_DIO_1_PORT, RADIO_DIO_1_PIN, &initStruct);
    gpio_init(RADIO_DIO_2_PORT, RADIO_DIO_2_PIN, &initStruct);
    gpio_init(RADIO_DIO_3_PORT, RADIO_DIO_3_PIN, &initStruct);

#ifdef RADIO_DIO_4
    gpio_init(RADIO_DIO_4_PORT, RADIO_DIO_4_PIN, &initStruct);
#endif
#ifdef RADIO_DIO_5
    gpio_init(RADIO_DIO_5_PORT, RADIO_DIO_5_PIN, &initStruct);
#endif
}

static void sx1276io_set_xo(uint8_t state)
{
    if (state == SET)
    {
        gpio_write(RADIO_TCXO_VCC_PORT, RADIO_TCXO_VCC_PIN, 1);
        rtc_delay_ms(BOARD_WAKEUP_TIME); //start up time of TCXO
    }
    else
    {
        gpio_write(RADIO_TCXO_VCC_PORT, RADIO_TCXO_VCC_PIN, 0);
    }
}

static uint32_t sx1276io_get_wake_time(void)
{
    return BOARD_WAKEUP_TIME;
}

static void sx1276io_irq_init(DioIrqHandler **irqHandlers)
{
    gpio_set_irq(RADIO_DIO_0_PORT, RADIO_DIO_0_PIN, _SX1276IO_IRQ_PRIORITY, irqHandlers[0]);
    gpio_set_irq(RADIO_DIO_1_PORT, RADIO_DIO_1_PIN, _SX1276IO_IRQ_PRIORITY, irqHandlers[1]);
    gpio_set_irq(RADIO_DIO_2_PORT, RADIO_DIO_2_PIN, _SX1276IO_IRQ_PRIORITY, irqHandlers[2]);
    gpio_set_irq(RADIO_DIO_3_PORT, RADIO_DIO_3_PIN, _SX1276IO_IRQ_PRIORITY, irqHandlers[3]);
}

static void sx1276io_rf_tx_set_power(int8_t power)
{
    uint8_t paconfig = 0;
    uint8_t padac = 0;

    paconfig = SX1276Read(REG_PACONFIG);
    padac = SX1276Read(REG_PADAC);

    uint8_t paselect = power > 14 ? RF_PACONFIG_PASELECT_PABOOST : RF_PACONFIG_PASELECT_RFO;

    paconfig = (paconfig & RF_PACONFIG_PASELECT_MASK) | paselect;

    if ((paconfig & RF_PACONFIG_PASELECT_PABOOST) == RF_PACONFIG_PASELECT_PABOOST)
    {
        if (power > 17)
        {
            padac = (padac & RF_PADAC_20DBM_MASK) | RF_PADAC_20DBM_ON;
        }
        else
        {
            padac = (padac & RF_PADAC_20DBM_MASK) | RF_PADAC_20DBM_OFF;
        }
        if ((padac & RF_PADAC_20DBM_ON) == RF_PADAC_20DBM_ON)
        {
            if (power < 5)
            {
                power = 5;
            }
            if (power > 20)
            {
                power = 20;
            }
            paconfig = (paconfig & RF_PACONFIG_OUTPUTPOWER_MASK) | (uint8_t)((uint16_t)(power - 5) & 0x0F);
        }
        else
        {
            if (power < 2)
            {
                power = 2;
            }
            if (power > 17)
            {
                power = 17;
            }
            paconfig = (paconfig & RF_PACONFIG_OUTPUTPOWER_MASK) | (uint8_t)((uint16_t)(power - 2) & 0x0F);
        }
    }
    else
    {
        if (power > 0)
        {
            if (power > 15)
            {
                power = 15;
            }
            paconfig = (paconfig & RF_PACONFIG_MAX_POWER_MASK & RF_PACONFIG_OUTPUTPOWER_MASK) | (7 << 4) | (power);
        }
        else
        {
            if (power < -4)
            {
                power = -4;
            }
            paconfig = (paconfig & RF_PACONFIG_MAX_POWER_MASK & RF_PACONFIG_OUTPUTPOWER_MASK) | (0 << 4) | (power + 4);
        }
    }
    SX1276Write(REG_PACONFIG, paconfig);
    SX1276Write(REG_PADAC, padac);
}

static void sx1276io_ant_set_sw_low_power(bool status)
{
    GPIO_InitTypeDef initStruct = {0};

    if (status == false)
    {
        initStruct.Mode = GPIO_MODE_OUTPUT_PP;
        initStruct.Pull = GPIO_NOPULL;
        initStruct.Speed = GPIO_SPEED_HIGH;

        gpio_init(RADIO_ANT_SWITCH_PORT_RX, RADIO_ANT_SWITCH_PIN_RX, &initStruct);
        gpio_write(RADIO_ANT_SWITCH_PORT_RX, RADIO_ANT_SWITCH_PIN_RX, 0);

        gpio_init(RADIO_ANT_SWITCH_PORT_TX_BOOST, RADIO_ANT_SWITCH_PIN_TX_BOOST, &initStruct);
        gpio_write(RADIO_ANT_SWITCH_PORT_TX_BOOST, RADIO_ANT_SWITCH_PIN_TX_BOOST, 0);

        gpio_init(RADIO_ANT_SWITCH_PORT_TX_RFO, RADIO_ANT_SWITCH_PIN_TX_RFO, &initStruct);
        gpio_write(RADIO_ANT_SWITCH_PORT_TX_RFO, RADIO_ANT_SWITCH_PIN_TX_RFO, 0);
    }
    else
    {
        initStruct.Mode = GPIO_MODE_ANALOG;
        initStruct.Pull = GPIO_NOPULL;
        initStruct.Speed = GPIO_SPEED_HIGH;

        gpio_init(RADIO_ANT_SWITCH_PORT_RX, RADIO_ANT_SWITCH_PIN_RX, &initStruct);
        gpio_write(RADIO_ANT_SWITCH_PORT_RX, RADIO_ANT_SWITCH_PIN_RX, 0);

        gpio_init(RADIO_ANT_SWITCH_PORT_TX_BOOST, RADIO_ANT_SWITCH_PIN_TX_BOOST, &initStruct);
        gpio_write(RADIO_ANT_SWITCH_PORT_TX_BOOST, RADIO_ANT_SWITCH_PIN_TX_BOOST, 0);

        gpio_init(RADIO_ANT_SWITCH_PORT_TX_RFO, RADIO_ANT_SWITCH_PIN_TX_RFO, &initStruct);
        gpio_write(RADIO_ANT_SWITCH_PORT_TX_RFO, RADIO_ANT_SWITCH_PIN_TX_RFO, 0);
    }
}

static void sx1276io_ant_set_sw(uint8_t opMode)
{
    uint8_t paconfig = SX1276Read(REG_PACONFIG);
    switch (opMode)
    {
    case RFLR_OPMODE_TRANSMITTER:
        if ((paconfig & RF_PACONFIG_PASELECT_PABOOST) == RF_PACONFIG_PASELECT_PABOOST)
        {
            gpio_write(RADIO_ANT_SWITCH_PORT_TX_BOOST, RADIO_ANT_SWITCH_PIN_TX_BOOST, 1);
        }
        else
        {
            gpio_write(RADIO_ANT_SWITCH_PORT_TX_RFO, RADIO_ANT_SWITCH_PIN_TX_RFO, 1);
        }
        SX1276.RxTx = 1;
        break;
    case RFLR_OPMODE_RECEIVER:
    case RFLR_OPMODE_RECEIVER_SINGLE:
    case RFLR_OPMODE_CAD:
    default:
        SX1276.RxTx = 0;
        gpio_write(RADIO_ANT_SWITCH_PORT_RX, RADIO_ANT_SWITCH_PIN_RX, 1);
        break;
    }
}



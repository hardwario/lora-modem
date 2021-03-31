#include "atci.h"
#include "cmd.h"
#include "config.h"
#include "io.h"
#include "lora.h"
#include "system.h"
#include "timeServer.h"

#define LORA_MAX_BAT 254
#define LORA_ADR_ON 1
#define ENABLE_FAST_WAKEUP

static void lora_rx_data(lora_AppData_t *AppData);
static void lora_join_status(bool status);
static void confirm_class(DeviceClass_t Class);
static void tx_needed(void);
static uint8_t get_battery_level(void);
static uint16_t get_temperature_level(void);
static void lora_mac_process_notify(void);
static void send_data_confirm(void);

static lora_callback_t LoRaMainCallbacks = {
    .config_save = config_save,
    get_battery_level,
    get_temperature_level,
    system_get_unique_id,
    system_get_random_seed,
    lora_rx_data,
    lora_join_status,
    confirm_class,
    tx_needed,
    lora_mac_process_notify,
    send_data_confirm
};
LoraFlagStatus LoraMacProcessRequest = LORA_RESET;

#pragma pack(push, 1)
typedef struct
{
    lora_configuration_t lora;
} configuration_t;
#pragma pack(pop)

static const configuration_t configuration_default = {
    .lora = {
        .otaa = LORA_ENABLE,
        .duty_cycle = LORA_DUTY_CYCLE,
        .class = LORA_CLASS,
        .devaddr = LORA_DEVICE_ADDRESS,
        .deveui = LORA_DEVICE_EUI,
        .appeui = LORA_JOIN_EUI,
        .appkey = LORA_APP_KEY,
        .nwkkey = LORA_NWK_KEY,
        .nwksenckey = LORA_NWK_S_ENC_KEY,
        .appskey = LORA_APP_S_KEY,
        .fnwksIntkey = LORA_F_NWK_S_INT_KEY,
        .snwksintkey = LORA_S_NWK_S_INT_KEY,
        .tx_datarate = DR_0,
        .adr = LORA_ADR_ON,
        .public_network = LORA_PUBLIC_NETWORK,
    }
};

configuration_t configuration;

int main(void)
{
    system_init();

    config_init(&configuration, sizeof(lora_configuration_t), &configuration_default);

    adc_init();

    spi_init(10000000);

    sx1276io_init();

    lora_init(&configuration.lora, &LoRaMainCallbacks);

    cmd_init();

    cmd_event(0, 0);

    while (1)
    {
        cmd_process();

        if (LoraMacProcessRequest == LORA_SET)
        {
            /*reset notification flag*/
            LoraMacProcessRequest = LORA_RESET;
            LoRaMacProcess();
        }

        // low power section
        DISABLE_IRQ();
        if (LoraMacProcessRequest != LORA_SET)
        {
#ifndef LOW_POWER_DISABLE
            system_low_power();
#endif
        }
        ENABLE_IRQ();
    }
}

static void lora_rx_data(lora_AppData_t *AppData)
{
    // call back when LoRa has received a frame
    // set_at_receive(AppData->Port, AppData->Buff, AppData->BuffSize);
}

void lora_mac_process_notify(void)
{
    LoraMacProcessRequest = LORA_SET;
}

static void lora_join_status(bool status)
{
    cmd_event(1, status);
}

static void confirm_class(DeviceClass_t Class)
{
    // call back when LoRa endNode has just switch the class
    // PRINTF("switch to class %c done\n\r", "ABC"[Class]);
}

static void tx_needed(void)
{
    // call back when server needs endNode to send a frame
    // PRINTF("Network Server is asking for an uplink transmission\n\r");
}

uint8_t get_battery_level(void)
{
    // callback to get the battery level in % of full charge (254 full charge, 0 no charge)
    return 254;
}

static uint16_t get_temperature_level(void)
{
    return 0;
}

static void send_data_confirm(void)
{
    cmd_print("+ACK\r\n\r\n");
}

void system_on_enter_stop_mode(void)
{
    /*  spi_io_deinit( );*/
    GPIO_InitTypeDef initStruct = {0};

    initStruct.Mode = GPIO_MODE_ANALOG;
    initStruct.Pull = GPIO_NOPULL;
    gpio_init(RADIO_MOSI_PORT, RADIO_MOSI_PIN, &initStruct);
    gpio_init(RADIO_MISO_PORT, RADIO_MISO_PIN, &initStruct);
    gpio_init(RADIO_SCLK_PORT, RADIO_SCLK_PIN, &initStruct);
    gpio_init(RADIO_NSS_PORT, RADIO_NSS_PIN, &initStruct);

    sx1276io_deinit();
    adc_deinit();
}

void system_on_exit_stop_mode(void)
{
    spi_io_init();
    sx1276io_init();
    vcom_io_init();
}

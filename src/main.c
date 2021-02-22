#include "system.h"
#include "hw.h"
#include "low_power_manager.h"
#include "timeServer.h"
#include "version.h"
#include "lora.h"
#include "atci.h"
#include "config.h"
#include "cmd.h"
#include "common.h"

#define LORAWAN_MAX_BAT 254
#define LORAWAN_ADR_ON 1
#define ENABLE_FAST_WAKEUP

static void LoraRxData(lora_AppData_t *AppData);
static void lora_join_status(bool status);
static void LORA_ConfirmClass(DeviceClass_t Class);
static void LORA_TxNeeded(void);
static uint8_t LORA_GetBatteryLevel(void);
static void LoraMacProcessNotify(void);
static void LORA_McpsDataConfirm(void);

static LoRaMainCallback_t LoRaMainCallbacks = {
    .config_save = config_save,
                                               LORA_GetBatteryLevel,
                                               HW_GetTemperatureLevel,
                                               HW_GetUniqueId,
                                               HW_GetRandomSeed,
                                               LoraRxData,
                                               lora_join_status,
                                               LORA_ConfirmClass,
                                               LORA_TxNeeded,
                                               LoraMacProcessNotify,
                                               LORA_McpsDataConfirm};
LoraFlagStatus LoraMacProcessRequest = LORA_RESET;

#pragma pack(push, 1)
typedef struct
{
    lora_configuration_t lora;
    struct
    {
        uint16_t baudrate;
        uint8_t data_bit;
        uint8_t stop_bit;
        uint8_t parity;
    } uart;

} configuration_t;
#pragma pack(pop)

static const configuration_t configuration_default = {
    .lora = {
        .otaa = LORA_ENABLE,
        .duty_cycle = LORAWAN_DUTY_CYCLE,
        .class = LORAWAN_CLASS,
        .devaddr = LORAWAN_DEVICE_ADDRESS,
        .deveui = LORAWAN_DEVICE_EUI,
        .appeui = LORAWAN_JOIN_EUI,
        .appkey = LORAWAN_APP_KEY,
        .nwkkey = LORAWAN_NWK_KEY,
        .nwksenckey = LORAWAN_NWK_S_ENC_KEY,
        .appskey = LORAWAN_APP_S_KEY,
        .fnwksIntkey = LORAWAN_F_NWK_S_INT_KEY,
        .snwksintkey = LORAWAN_S_NWK_S_INT_KEY,
        .tx_datarate = DR_0,
        .adr = LORAWAN_ADR_ON,
        .public_network = LORAWAN_PUBLIC_NETWORK,
    },
    .uart = {
        .baudrate = 19200,
        .data_bit = 8,
        .stop_bit = 0,
        .parity = 1
    }
};

configuration_t configuration;

int main(void)
{
    system_init();

    config_init(&configuration, sizeof(lora_configuration_t), &configuration_default);

    HW_Init();

    LPM_SetOffMode(LPM_APPLI_Id, LPM_Disable);

    LORA_Init(&configuration.lora, &LoRaMainCallbacks);

    cmd_init();

    cmd_event(0, 0);

    while (1)
    {
        atci_process();

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
            LPM_EnterLowPower();
#endif
        }
        ENABLE_IRQ();
    }
}

static void LoraRxData(lora_AppData_t *AppData)
{
    // call back when LoRa has received a frame
    // set_at_receive(AppData->Port, AppData->Buff, AppData->BuffSize);
}

void LoraMacProcessNotify(void)
{
    LoraMacProcessRequest = LORA_SET;
}

static void lora_join_status(bool status)
{
    cmd_event(1, status);
}

static void LORA_ConfirmClass(DeviceClass_t Class)
{
    // call back when LoRa endNode has just switch the class
    // PRINTF("switch to class %c done\n\r", "ABC"[Class]);
}

static void LORA_TxNeeded(void)
{
    // call back when server needs endNode to send a frame
    // PRINTF("Network Server is asking for an uplink transmission\n\r");
}

uint8_t LORA_GetBatteryLevel(void)
{
    // callback to get the battery level in % of full charge (254 full charge, 0 no charge)
    return 254;
}

static void LORA_McpsDataConfirm(void)
{
    PRINTF("+ACK\r\n\r\n");
}

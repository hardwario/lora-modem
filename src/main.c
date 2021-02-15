#include "system.h"
#include "hw.h"
#include "low_power_manager.h"
#include "timeServer.h"
#include "version.h"
#include "lora.h"
#include "atci.h"
#include "cmd.h"

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

static LoRaMainCallback_t LoRaMainCallbacks = {LORA_GetBatteryLevel,
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

int main(void)
{
    system_init();
    HW_Init();

    LPM_SetOffMode(LPM_APPLI_Id, LPM_Disable);

    LORA_Init(&LoRaMainCallbacks);

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
    PRINTF("Network Server \"ack\" an uplink data confirmed message transmission\n\r");
}

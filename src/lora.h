#ifndef _LORA_H
#define _LORA_H

#include "LoRaMac.h"
#include "region/Region.h"

#define LORA_DEVICE_EUI      { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LORA_CLASS           CLASS_A
#define LORA_PUBLIC_NETWORK  true
#define LORA_JOIN_EUI        { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 }
#define LORA_APP_KEY         { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }
#define LORA_GEN_APP_KEY     { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F }
#define LORA_NWK_KEY         { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }
#define LORA_NETWORK_ID      ( uint32_t )0
#define LORA_DEVICE_ADDRESS  ( uint32_t )0x00000000
#define LORA_F_NWK_S_INT_KEY { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }
#define LORA_S_NWK_S_INT_KEY { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }
#define LORA_NWK_S_ENC_KEY   { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }
#define LORA_APP_S_KEY       { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }

#if defined(REGION_EU868) || defined(REGION_RU864) || defined(REGION_CN779) || defined(REGION_EU433)
    #define LORA_DUTY_CYCLE LORA_ENABLE
#else
    #define LORA_DUTY_CYCLE LORA_DISABLE
#endif

#ifndef LORA_MAC_VERSION
#define LORA_MAC_VERSION 0x01000300
#endif

#define LORA_ADR_ON                              1
#define LORA_ADR_OFF                             0

//! @brief Application Data structure

typedef struct
{
    // point to the LoRa App data buffer
    uint8_t *Buff;
    // LoRa App data buffer size
    uint8_t BuffSize;
    // Port on which the LoRa App is data is sent or received
    uint8_t Port;

} lora_AppData_t;

typedef enum
{
    LORA_RESET = 0,
    LORA_SET = !LORA_RESET
} LoraFlagStatus;

typedef enum
{
    LORA_DISABLE = 0,
    LORA_ENABLE = !LORA_DISABLE
} LoraState_t;

typedef enum
{
    LORA_ERROR = -1,
    LORA_SUCCESS = 0
} LoraErrorStatus;

typedef enum
{
    LORA_UNCONFIRMED_MSG = 0,
    LORA_CONFIRMED_MSG = !LORA_UNCONFIRMED_MSG
} LoraConfirm_t;

//! @brief Lora configuration structure
#pragma pack(push, 1)
typedef struct
{
    bool public_network;      // if public network
    bool otaa;                // if over the air activation
    bool duty_cycle;          // if duty cyle
    uint8_t class;            // Class mode
    uint32_t devaddr;         // Device address
    uint8_t deveui[8];        // Device EUI
    uint8_t appeui[8];        // AppEUI x Join Eui
    uint8_t appkey[16];       // App Key
    uint8_t nwkkey[16];       // Application Key
    uint8_t nwksenckey[16];   // Network Session Key
    uint8_t appskey[16];      // Application Session Key
    uint8_t fnwksIntkey[16];  // Application Session Key
    uint8_t snwksintkey[16];  // Application Session Key
    uint8_t application_port; // Application port we will receive to
    uint8_t tx_datarate;      // TX datarate
    bool adr;                 // if aptive data rate
} lora_configuration_t;
#pragma pack(pop)

//! @brief Lora callback structure
typedef struct
{
    //! @brief Call for lora_save_config
    bool (*config_save)(void);

    //! @brief Get the current battery level
    //! @retval value battery level(0 very low, 254 fully charged)
    uint8_t (*get_battery_level)(void);

    //! @brief Get the current temperature in degree Celcius( q7.8 )
    uint16_t (*get_temperature_level)(void);

    //! @brief Gets the board 64 bits unique ID
    //! @param [IN] id Pointer to an buffer that will contain the Unique ID, size 8 bytes
    void (*get_unique_Id)(uint8_t *id);

    //! @brief Returns a pseudo random seed generated using the MCU Unique ID
    uint32_t (*get_random_seed)(void);

    //! @brief Process Rx Data received from Lora network
    //! @param [IN] AppData Pointer to structure
    void (*rx_data)(lora_AppData_t *AppData);

    //! @brief Callback for result lora_join
    //! @param [IN] success
    void (*join_status)(bool success);

    //! @brief Confirms the class change
    void (*confirm_class)(DeviceClass_t Class);

    //! @brief Callback indicating an uplink transmission is needed to allow a pending downlink transmission
    void (*tx_needed)(void);

    //! @brief Will be called each time a Radio IRQ is handled by the MAC layer.
    //! @warning Runs in a IRQ context. Should only change variables state.
    void (*mac_process_notify)(void);

    //! @brief callback indicating a downlink transmission providing an acknowledgment for an uplink confirmed data message transmission
    void (*send_data_confirm)(void);

} lora_callback_t;

//! @brief Lora Initialisation
//! @param [IN] config Pointer to configuration structure
//! @param [IN] callbacks Pointer to callback structure

void lora_init(lora_configuration_t *config, lora_callback_t *callbacks);

//! @brief run Lora send data
// TODO: remove app data

LoraErrorStatus lora_send(lora_AppData_t *AppData, LoraConfirm_t IsTxConfirmed);

//! @brief Join a Lora Network only for OTTA
//! @note  if the device is ABP, this is a pass through functon

LoraErrorStatus lora_join(void);

//! @brief Check whether the Device is joined to the network

LoraFlagStatus lora_is_join(void);

//! @brief Change Lora Class
//! @note callback confirm_class informs upper layer that the change has occured
//! @note Only switch from class A to class B/C OR from  class B/C to class A is allowed
//! @attention can be calld only in LORA_ClassSwitchSlot or rx_data callbacks
//! @param [IN] DeviceClass_t NewClass

LoraErrorStatus lora_class_change(DeviceClass_t newClass);

//! @brief get the current Lora Class

uint8_t lora_class_get(void);

//! @brief  Set join activation process: OTAA vs ABP
//! @param  otaa The Air Activation status to set: enable or disable

void lora_otaa_set(LoraState_t otaa);

//! @brief  Get join activation process: OTAA vs ABP
//! @retval ENABLE if OTAA is used, DISABLE if ABP is used

LoraState_t lora_otaa_get(void);

//! @brief  Set duty cycle: ENABLE or DISABLE
//! @param  duty_cycle to set: enable or disable

void lora_duty_cycle_set(LoraState_t duty_cycle);

//! @brief  Get Duty cycle: OTAA vs ABP
//! @retval ENABLE / DISABLE

LoraState_t lora_duty_cycle_get(void);

//! @brief  Get Device EUI
//! @retval Point to the buffer, size 8 bytes

uint8_t *lora_deveui_get(void);

//! @brief  Set Device EUI
//! @param  deveui Point to the buffer, size 8 bytes

void lora_deveui_set(uint8_t deveui[8]);

//! @brief  Get Join Eui
//! @retval Point to the buffer, size 8 bytes

uint8_t *lora_appeui_get(void);

//! @brief  Set Join Eui
//! @param  joineui Point to the buffer, size 8 bytes

void lora_appeui_set(uint8_t joineui[8]);

//! @brief  Get Device address

uint32_t lora_devaddr_get(void);

//! @brief  Set Device address
//! @param  devaddr

void lora_devaddr_set(uint32_t devaddr);

//! @brief  Get Application Key
//! @retval Point to the buffer, size 16 bytes

uint8_t *lora_appkey_get(void);

//! @brief  Set Application Key
//! @param  appkey Point to the buffer, size 16 bytes

void lora_appkey_set(uint8_t appkey[16]);

//! @brief  Get is public network enabled

bool lora_public_network_get(void);

//! @brief  Set enable public network or not
//! @param  enable

void lora_public_network_set(bool enable);

//!  @brief  Get the SNR of the last received data

int8_t lora_snr_get(void);

//! @brief  Get the RSSI of the last received data

int16_t lora_rssi_get(void);

//! @brief  Get whether or not the last sent data were acknowledged
//! @retval ENABLE if so, DISABLE otherwise

LoraState_t lora_isack_get(void);

//! @brief  get tx datarate

int8_t lora_tx_datarate_get(void);

//! @brief  Set tx datarate

void lora_tx_datarate_set(int8_t TxDataRate);

//! @brief  get LoRaMac region

LoRaMacRegion_t lora_region_get(void);

//! @brief Save config

void lora_save_config(void);

#endif // _LORA_H

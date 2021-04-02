#ifndef _LORA_H
#define _LORA_H

#include "LoRaMac.h"
#include "region/Region.h"

#define LORA_DEVICE_EUI      { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LORA_CLASS           CLASS_A
#define LORA_PUBLIC_NETWORK  true
#define LORA_JOIN_EUI        { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 }
#define LORA_DEFAULT_KEY     { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }
#define LORA_NETWORK_ID      ( uint32_t )0
#define LORA_DEVICE_ADDRESS  ( uint32_t )0x00000000

#ifndef LORA_MAC_VERSION
#define LORA_MAC_VERSION 0x01000300
#endif

#define LORA_MAX_BAT 254
#define LORA_ADR_ON                              1
#define LORA_ADR_OFF                             0
#define LORA_UNCONFIRMED_MSG false
#define LORA_CONFIRMED_MSG true
#define LORA_CHMASK_LENGTH 6 

//! @brief Application Data structure

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

//! @brief Lora configuration structure
#pragma pack(push, 2)
typedef struct
{
    uint8_t region;
    bool public_network;      // if public network
    bool otaa;                // if over the air activation
    bool duty_cycle;          // if duty cyle
    bool adr;                 // if aptive data rate
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
    uint16_t chmask[LORA_CHMASK_LENGTH]; 
    uint8_t tx_datarate;      // TX datarate
} lora_configuration_t;
#pragma pack(pop)

//! @brief Lora TX notification structure

typedef struct
{
    uint8_t is_mcps_confirm;
    LoRaMacEventInfoStatus_t status;
    int8_t datarate;
    uint32_t uplink_counter;
    int8_t power;
    uint8_t channel;
    uint8_t ack_received;

    bool confirmed;
    uint8_t port;
    uint8_t length;
    uint8_t *buffer;
} lora_tx_params_t;

//! @brief Lora RX notification structure

typedef struct
{
    uint8_t is_mcps_indication;
    LoRaMacEventInfoStatus_t status;
    int8_t datarate;
    int8_t rssi;
    int8_t snr;
    uint32_t downlink_counter;
    int8_t slot;
} lora_rx_params_t;

typedef struct
{
    ChannelParams_t *channels;
    uint8_t length;
    uint16_t* chmask;
    uint8_t chmask_length;
    uint16_t* chmask_default;

} lora_channel_list_t;

//! @brief Lora callback structure
typedef struct
{
    //! @brief Call for lora_save_config
    bool (*config_save)(void);

    //! @brief Get the current battery level
    //! @retval value battery level(0 very low, 254 fully charged)
    uint8_t (*get_battery_level)(void);

    //! @brief Get the current temperature in degree Celcius
    float (*get_temperature_level)(void);

    //! @brief Gets the board 64 bits unique ID
    //! @param[in] id Pointer to an buffer that will contain the Unique ID, size 8 bytes
    void (*get_unique_id)(uint8_t *id);

    //! @brief Returns a pseudo random seed generated using the MCU Unique ID
    uint32_t (*get_random_seed)(void);

    //! @brief Callback for result lora_join
    //! @param[in] success
    void (*join_status)(bool success);

    //! @brief Callback indicating an uplink transmission is needed to allow a pending downlink transmission
    void (*tx_needed)(void);

    //! @brief Will be called each time a Radio IRQ is handled by the MAC layer.
    //! @warning Runs in a IRQ context. Should only change variables state.
    void (*mac_process_notify)(void);

    //! @brief Process Rx Data received from Lora network
    //! @param[in] port Port
    //! @param[in] buffer Pointer to data
    //! @param[in] length Data length
    //! @param[in] params RX params
    void (*on_rx_data)(uint8_t port, uint8_t *buffer, uint8_t length, lora_rx_params_t *params);

    //! @brief Notifies that a frame has been transmitted
    //! @param[in] params TX params
    void (*on_tx_data)(lora_tx_params_t *params);

} lora_callback_t;

//! @brief Lora Initialisation
//! @param[in] config Pointer to configuration structure
//! @param[in] callbacks Pointer to callback structure

void lora_init(lora_configuration_t *config, lora_callback_t *callbacks);

//! @brief Lora process

void lora_process(void);

//! @brief Indicates if the lora is busy
 
bool lora_is_busy(void);

//! @brief Join a Lora Network only for OTTA
//! @note  if the device is ABP, this is a pass through functon

bool lora_join(void);

//! @brief Check whether the Device is joined to the network

bool lora_is_join(void);

//! @brief run Lora send data
//! @param[in] port Port
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be send
//! @param[in] confirmed set true for confirmed message

bool lora_send(uint8_t port, void *buffer, uint8_t length, bool confirmed);

//! @brief Change Lora Class
//! @note callback confirm_class informs upper layer that the change has occured
//! @note Only switch from class A to class B/C OR from  class B/C to class A is allowed
//! @attention can be calld only in LORA_ClassSwitchSlot or rx_data callbacks
//! @param[in] DeviceClass_t NewClass

bool lora_class_change(DeviceClass_t newClass);

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

void lora_duty_cycle_set(bool duty_cycle);

//! @brief  Get Duty cycle: OTAA vs ABP
//! @retval ENABLE / DISABLE

bool lora_duty_cycle_get(void);

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

//! @brief Get LoRaMac region

LoRaMacRegion_t lora_region_get(void);

//! @brief Set LoRaMac region
//! @note  Change default duty cycle for region
//! @param[in] region 

bool lora_region_set(LoRaMacRegion_t region);

//! @brief Get channel list

lora_channel_list_t lora_get_channel_list(void);

bool lora_chmask_set(uint16_t chmask[LORA_CHMASK_LENGTH]);

//! @brief Save config

void lora_save_config(void);

#endif // _LORA_H

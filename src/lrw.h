#ifndef _LRW_H
#define _LRW_H

#include <loramac-node/src/mac/LoRaMac.h>
#include <loramac-node/src/mac/region/Region.h>

#define LRW_DEVICE_EUI      { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LRW_CLASS           CLASS_A
#define LRW_PUBLIC_NETWORK  true
#define LRW_JOIN_EUI        { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 }
#define LRW_DEFAULT_KEY     { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }
#define LRW_NETWORK_ID      ( uint32_t )0
#define LRW_DEVICE_ADDRESS  ( uint32_t )0x00000000

#ifndef LRW_MAC_VERSION
#define LRW_MAC_VERSION 0x01000300
#endif

#define LRW_MAX_BAT 254
#define LRW_ADR_ON                              1
#define LRW_ADR_OFF                             0
#define LRW_UNCONFIRMED_MSG false
#define LRW_CONFIRMED_MSG true
#define LRW_CHMASK_LENGTH 6

//! @brief Application Data structure

typedef enum
{
    LRW_RESET = 0,
    LRW_SET = !LRW_RESET
} LoraFlagStatus;

typedef enum
{
    LRW_DISABLE = 0,
    LRW_ENABLE = !LRW_DISABLE
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
    uint16_t chmask[LRW_CHMASK_LENGTH];
    uint8_t tx_datarate;      // TX datarate
    uint8_t tx_repeats;       // unconfirmed messages only
} lrw_configuration_t;
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
} lrw_tx_params_t;

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
} lrw_rx_params_t;

typedef struct
{
    ChannelParams_t *channels;
    uint8_t length;
    uint16_t* chmask;
    uint8_t chmask_length;
    uint16_t* chmask_default;

} lrw_channel_list_t;

//! @brief Lora callback structure
typedef struct
{
    //! @brief Call for lrw_save_config
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

    //! @brief Callback for result lrw_join
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
    void (*on_rx_data)(uint8_t port, uint8_t *buffer, uint8_t length, lrw_rx_params_t *params);

    //! @brief Notifies that a frame has been transmitted
    //! @param[in] params TX params
    void (*on_tx_data)(lrw_tx_params_t *params);

} lrw_callback_t;

//! @brief LoRaWAN Initialisation
//! @param[in] config Pointer to configuration structure
//! @param[in] callbacks Pointer to callback structure

void lrw_init(lrw_configuration_t *config, lrw_callback_t *callbacks);

//! @brief Lora process

void lrw_process(void);

//! @brief Indicates if the lora is busy

bool lrw_is_busy(void);

//! @brief Join a Lora Network only for OTTA
//! @note  if the device is ABP, this is a pass through functon

bool lrw_join(void);

//! @brief Check whether the Device is joined to the network

bool lrw_is_join(void);

//! @brief run Lora send data
//! @param[in] port Port
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be send
//! @param[in] confirmed set true for confirmed message

bool lrw_send(uint8_t port, void *buffer, uint8_t length, bool confirmed);

//! @brief Change Lora Class
//! @note callback confirm_class informs upper layer that the change has occured
//! @note Only switch from class A to class B/C OR from  class B/C to class A is allowed
//! @attention can be calld only in LRW_ClassSwitchSlot or rx_data callbacks
//! @param[in] DeviceClass_t NewClass

bool lrw_class_change(DeviceClass_t newClass);

//! @brief get the current Lora Class

uint8_t lrw_class_get(void);

//! @brief  Set join activation process: OTAA vs ABP
//! @param  otaa The Air Activation status to set: enable or disable

void lrw_otaa_set(LoraState_t otaa);

//! @brief  Get join activation process: OTAA vs ABP
//! @retval ENABLE if OTAA is used, DISABLE if ABP is used

LoraState_t lrw_otaa_get(void);

//! @brief  Set duty cycle: ENABLE or DISABLE
//! @param  duty_cycle to set: enable or disable

void lrw_duty_cycle_set(bool duty_cycle);

//! @brief  Get Duty cycle: OTAA vs ABP
//! @retval ENABLE / DISABLE

bool lrw_duty_cycle_get(void);

//! @brief  Get Device EUI
//! @retval Point to the buffer, size 8 bytes

uint8_t *lrw_deveui_get(void);

//! @brief  Set Device EUI
//! @param  deveui Point to the buffer, size 8 bytes

void lrw_deveui_set(uint8_t deveui[8]);

//! @brief  Get Join Eui
//! @retval Point to the buffer, size 8 bytes

uint8_t *lrw_appeui_get(void);

//! @brief  Set Join Eui
//! @param  joineui Point to the buffer, size 8 bytes

void lrw_appeui_set(uint8_t joineui[8]);

//! @brief  Get Device address

uint32_t lrw_devaddr_get(void);

//! @brief  Set Device address
//! @param  devaddr

void lrw_devaddr_set(uint32_t devaddr);

//! @brief  Get Application Key
//! @retval Point to the buffer, size 16 bytes

uint8_t *lrw_appkey_get(void);

//! @brief  Set Application Key
//! @param  appkey Point to the buffer, size 16 bytes

void lrw_appkey_set(uint8_t appkey[16]);

//! @brief  Get is public network enabled

bool lrw_public_network_get(void);

//! @brief  Set enable public network or not
//! @param  enable

void lrw_public_network_set(bool enable);

//!  @brief  Get the SNR of the last received data

int8_t lrw_snr_get(void);

//! @brief  Get the RSSI of the last received data

int16_t lrw_rssi_get(void);

//! @brief  Get whether or not the last sent data were acknowledged
//! @retval ENABLE if so, DISABLE otherwise

LoraState_t lrw_isack_get(void);

//! @brief Get tx datarate

int8_t lrw_tx_datarate_get(void);

//! @brief  Set tx datarate

void lrw_tx_datarate_set(int8_t TxDataRate);

//! @brief Get LoRaMac region

LoRaMacRegion_t lrw_region_get(void);

//! @brief Set LoRaMac region
//! @note  Change default duty cycle for region
//! @param[in] region

int lrw_region_set(LoRaMacRegion_t region);

//! @brief Get channel list

lrw_channel_list_t lrw_get_channel_list(void);

bool lrw_chmask_set(uint16_t chmask[LRW_CHMASK_LENGTH]);

//! @brief Set Number of uplink unconfirmed messages repeats
//! @param[in] repeats Number in the range 1 to 15

bool lrw_unconfirmed_message_repeats_set(uint8_t repeats);

//! @brief Get Number of uplink unconfirmed messages repeats

uint8_t lrw_unconfirmed_message_repeats_get(void);

//! @brief Save config

void lrw_save_config(void);

#endif // _LRW_H

#ifndef __LORA_MAIN_H__
#define __LORA_MAIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "Commissioning.h"
#include "LoRaMac.h"
#include "region/Region.h"

#define LORAWAN_DEVICE_EUI      { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LORAWAN_CLASS           CLASS_A
#if defined(REGION_EU868) || defined(REGION_RU864) || defined(REGION_CN779) || defined(REGION_EU433)
    #define LORAWAN_DUTY_CYCLE LORA_ENABLE
#else
    #define LORAWAN_DUTY_CYCLE LORA_DISABLE
#endif

#define LORAWAN_ADR_ON                              1
#define LORAWAN_ADR_OFF                             0

/*!
 * Application Data structure
 */
typedef struct
{
  /*point to the LoRa App data buffer*/
  uint8_t *Buff;
  /*LoRa App data buffer size*/
  uint8_t BuffSize;
  /*Port on which the LoRa App is data is sent/ received*/
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
  LORAWAN_UNCONFIRMED_MSG = 0,
  LORAWAN_CONFIRMED_MSG = !LORAWAN_UNCONFIRMED_MSG
} LoraConfirm_t;

typedef enum
{
  LORA_TRUE = 0,
  LORA_FALSE = !LORA_TRUE
} LoraBool_t;

/*!
 * LoRa State Machine states
 */
typedef enum eTxEventType
{
  /*!
   * @brief AppdataTransmition issue based on timer every TxDutyCycleTime
   */
  TX_ON_TIMER,
  /*!
   * @brief AppdataTransmition external event plugged on OnSendEvent( )
   */
  TX_ON_EVENT
} TxEventType_t;

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

/* Lora Main callbacks*/
typedef struct sLoRaMainCallback
{
    bool (*config_save)(void);
  /*!
   * @brief Get the current battery level
   *
   * @retval value  battery level ( 0: very low, 254: fully charged )
   */
  uint8_t (*BoardGetBatteryLevel)(void);
  /*!
   * \brief Get the current temperature
   *
   * \retval value  temperature in degreeCelcius( q7.8 )
   */
  uint16_t (*BoardGetTemperatureLevel)(void);
  /*!
   * @brief Gets the board 64 bits unique ID
   *
   * @param [IN] id Pointer to an array that will contain the Unique ID
   */
  void (*BoardGetUniqueId)(uint8_t *id);
  /*!
  * Returns a pseudo random seed generated using the MCU Unique ID
  *
  * @retval seed Generated pseudo random seed
  */
  uint32_t (*BoardGetRandomSeed)(void);
  /*!
   * @brief Process Rx Data received from Lora network
   *
   * @param [IN] AppData structure
   *
   */
  void (*LORA_RxData)(lora_AppData_t *AppData);

  /*!
   * @brief callback indicating EndNode has joined
   *
   * @param [IN] None
   */
  void (*join_status)(bool success);
  /*!
   * @brief Confirms the class change
   *
   * @param [IN] AppData is a buffer to process
   *
   * @param [IN] port is a Application port on wicth Appdata will be sent
   *
   * @param [IN] length is the number of recieved bytes
   */
  void (*LORA_ConfirmClass)(DeviceClass_t Class);
  /*!
   * @brief callback indicating an uplink transmission is needed to allow
   *        a pending downlink transmission
   *
   * @param [IN] None
   */
  void (*LORA_TxNeeded)(void);
  /*!
   *\brief    Will be called each time a Radio IRQ is handled by the MAC
   *          layer.
   *
   *\warning  Runs in a IRQ context. Should only change variables state.
   */
  void (*MacProcessNotify)(void);
  /*!
  * @brief callback indicating a downlink transmission providing
  *        an acknowledgment for an uplink confirmed data message transmission
  *
  * @param [IN] None
  */
  void (*LORA_McpsDataConfirm)(void);
} LoRaMainCallback_t;



/* External variables --------------------------------------------------------*/
/* Exported macros -----------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
/**
 * @brief Lora Initialisation
 * @param [IN] LoRaMainCallback_t
 * @param [IN] application parmaters
 * @retval none
 */
void LORA_Init(lora_configuration_t *config, LoRaMainCallback_t *callbacks);

/**
 * @brief run Lora classA state Machine
 * @param [IN] none
 * @retval none
 */
LoraErrorStatus LORA_send(lora_AppData_t *AppData, LoraConfirm_t IsTxConfirmed);

/**
 * @brief Join a Lora Network in classA
 * @Note if the device is ABP, this is a pass through functon
 * @param [IN] none
 * @retval none
 */
LoraErrorStatus LORA_Join(void);

/**
 * @brief Check whether the Device is joined to the network
 * @param [IN] none
 * @retval returns LORA_SET if joined
 */
LoraFlagStatus LORA_JoinStatus(void);

/**
 * @brief change Lora Class
 * @Note callback LORA_ConfirmClass informs upper layer that the change has occured
 * @Note Only switch from class A to class B/C OR from  class B/C to class A is allowed
 * @Attention can be calld only in LORA_ClassSwitchSlot or LORA_RxData callbacks
 * @param [IN] DeviceClass_t NewClass
 * @retval LoraErrorStatus
 */
LoraErrorStatus LORA_RequestClass(DeviceClass_t newClass);

/**
 * @brief get the current Lora Class
 * @retval class
 */
uint8_t lora_class_get(void);

/**
  * @brief  Set join activation process: OTAA vs ABP
  * @param  Over The Air Activation status to set: enable or disable
  * @retval None
  */
void lora_otaa_set(LoraState_t otaa);

/**
  * @brief  Get join activation process: OTAA vs ABP
  * @param  None
  * @retval ENABLE if OTAA is used, DISABLE if ABP is used
  */
LoraState_t lora_otaa_get(void);

/**
  * @brief  Set duty cycle: ENABLE or DISABLE
  * @param  Duty cycle to set: enable or disable
  * @retval None
  */
void lora_duty_cycle_set(LoraState_t duty_cycle);

/**
  * @brief  Get Duty cycle: OTAA vs ABP
  * @param  None
  * @retval ENABLE / DISABLE
  */
LoraState_t lora_duty_cycle_get(void);

/**
  * @brief  Get Device EUI
  * @param  None
  * @retval DevEUI
  */
uint8_t *lora_deveui_get(void);

/**
  * @brief  Set Device EUI
  * @param  deveui
  * @retval Nonoe
  */
void lora_deveui_set(uint8_t deveui[8]);

/**
  * @brief  Get Join Eui
  * @param  None
  * @retval JoinEUI
  */
uint8_t *lora_appeui_get(void);

/**
  * @brief  Set Join Eui
  * @param  JoinEUI
  * @retval Nonoe
  */
void lora_appeui_set(uint8_t joineui[8]);

/**
  * @brief  Get Device address
  * @param  None
  * @retval JoinEUI
  */
uint32_t lora_devaddr_get(void);

/**
  * @brief  Set Device address
  * @param  devaddr
  * @retval Nonoe
  */
void lora_devaddr_set(uint32_t devaddr);

/**
  * @brief  Get Application Key
  * @param  None
  * @retval AppKey
  */
uint8_t *lora_appkey_get(void);

/**
  * @brief  Set Application Key
  * @param  AppKey
  * @retval None
  */
void lora_appkey_set(uint8_t appkey[16]);

/**
  * @brief  Get is public network enabled
  * @param  None
  * @retval enabled
  */
bool lora_public_network_get(void);

/**
  * @brief  Set enable public network or not
  * @param  enable
  * @retval None
  */
void lora_public_network_set(bool enable);

/**
 * @brief  Get the SNR of the last received data
 * @param  None
 * @retval SNR
 */
int8_t lora_snr_get(void);

/**
 * @brief  Get the RSSI of the last received data
 * @param  None
 * @retval RSSI
 */
int16_t lora_rssi_get(void);

/**
 * @brief  Get whether or not the last sent data were acknowledged
 * @param  None
 * @retval ENABLE if so, DISABLE otherwise
 */
LoraState_t lora_isack_get(void);

/**
 * @brief  Launch LoraWan certification tests
 * @param  None
 * @retval The application port
 */
void lora_wan_certif(void);

/**
 * @brief  set tx datarate
 * @param  None
 * @retval The application port
 */
void lora_tx_datarate_set(int8_t TxDataRate);

/**
 * @brief  get tx datarate
 * @param  None
 * @retval tx datarate
 */
int8_t lora_tx_datarate_get(void);

/**
 * @brief  get LoRaMac region
 * @param  None
 * @retval LoRaMac region
 */
LoRaMacRegion_t lora_region_get(void);

void lora_save_config(void);

#ifdef __cplusplus
}
#endif

#endif /*__LORA_MAIN_H__*/

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

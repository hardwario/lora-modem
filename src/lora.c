#include "lora.h"
#include "timeServer.h"
#include "utilities.h"
#include "LoRaMac.h"

static struct
{
    lora_configuration_t *config;
    McpsConfirm_t *McpsConfirm; // confirm structure
    int16_t Rssi;               // Rssi of the received packet
    int8_t Snr;                 // Snr of the received packet
    lora_callback_t *callbacks;

} _lora;

/*!
 *  Select either Device_Time_req or Beacon_Time_Req following LoRaWAN version
 *  - Device_Time_req   Available for V1.0.3 or later
 *  - Beacon_time_Req   Available for V1.0.2 and before
 */
#define USE_DEVICE_TIMING

/*!
 * Join requests trials duty cycle.
 */
#define OVER_THE_AIR_ACTIVATION_DUTYCYCLE 10000 // 10 [s] value in ms

#ifdef LORAMAC_CLASSB_ENABLED
/*!
 * Default ping slots periodicity
 *
 * \remark periodicity is equal to 2^LORA_DEFAULT_PING_SLOT_PERIODICITY seconds
 *         example: 2^3 = 8 seconds. The end-device will open an Rx slot every 8 seconds.
 */
#define LORA_DEFAULT_PING_SLOT_PERIODICITY 0
uint8_t DefaultPingSlotPeriodicity;

#endif
#define HEX16(X) X[0], X[1], X[2], X[3], X[4], X[5], X[6], X[7], X[8], X[9], X[10], X[11], X[12], X[13], X[14], X[15]
#define HEX8(X) X[0], X[1], X[2], X[3], X[4], X[5], X[6], X[7]

static MlmeReqJoin_t JoinParameters;

#ifdef LORAMAC_CLASSB_ENABLED
static LoraErrorStatus LORA_BeaconReq(void);
static LoraErrorStatus LORA_PingSlotReq(void);

#if defined(USE_DEVICE_TIMING)
static LoraErrorStatus LORA_DeviceTimeReq(void);
#else
static LoraErrorStatus LORA_BeaconTimeReq(void);
#endif /* USE_DEVICE_TIMING */
#endif /* LORAMAC_CLASSB_ENABLED */

/*!
 * Defines the LoRa parameters at Init
 */
static LoRaMacPrimitives_t LoRaMacPrimitives;
static LoRaMacCallback_t LoRaMacCallbacks;
static MibRequestConfirm_t mibReq;
static LoRaMacRegion_t LoRaRegion;

/*!
 * \brief   MCPS-Confirm event function
 *
 * \param   [IN] McpsConfirm - Pointer to the confirm structure,
 *               containing confirm attributes.
 */
static void McpsConfirm(McpsConfirm_t *mcpsConfirm)
{
    _lora.McpsConfirm = mcpsConfirm;
    if (mcpsConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK)
    {
        switch (mcpsConfirm->McpsRequest)
        {
        case MCPS_UNCONFIRMED:
        {
            // Check Datarate
            // Check TxPower
            break;
        }
        case MCPS_CONFIRMED:
        {
            // Check Datarate
            // Check TxPower
            // Check AckReceived
            if (mcpsConfirm->AckReceived)
            {
                _lora.callbacks->send_data_confirm();
            }
            // Check NbTrials
            break;
        }
        case MCPS_PROPRIETARY:
        {
            break;
        }
        default:
            break;
        }
    }
}

/*!
 * \brief   MCPS-Indication event function
 *
 * \param   [IN] mcpsIndication - Pointer to the indication structure,
 *               containing indication attributes.
 */
static void McpsIndication(McpsIndication_t *mcpsIndication)
{
    lora_AppData_t AppData;

    if (mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK)
    {
        return;
    }

    switch (mcpsIndication->McpsIndication)
    {
    case MCPS_UNCONFIRMED:
    {
        break;
    }
    case MCPS_CONFIRMED:
    {
        break;
    }
    case MCPS_PROPRIETARY:
    {
        break;
    }
    case MCPS_MULTICAST:
    {
        break;
    }
    default:
        break;
    }

    // Check Multicast
    // Check Port
    // Check Datarate
    // Check FramePending
    if (mcpsIndication->FramePending == true)
    {
        // The server signals that it has pending data to be sent.
        // We schedule an uplink as soon as possible to flush the server.
        _lora.callbacks->tx_needed();
    }
    // Check Buffer
    // Check BufferSize
    // Check Rssi
    // Check Snr
    // Check RxSlot

    if (mcpsIndication->RxData == true)
    {
            AppData.Port = mcpsIndication->Port;
            AppData.BuffSize = mcpsIndication->BufferSize;
            AppData.Buff = mcpsIndication->Buffer;
            _lora.Rssi = mcpsIndication->Rssi;
            _lora.Snr = mcpsIndication->Snr;
            _lora.callbacks->rx_data(&AppData);
    }
}

/*!
 * \brief   MLME-Confirm event function
 *
 * \param   [IN] MlmeConfirm - Pointer to the confirm structure,
 *               containing confirm attributes.
 */
static void MlmeConfirm(MlmeConfirm_t *mlmeConfirm)
{
#ifdef LORAMAC_CLASSB_ENABLED
    MibRequestConfirm_t mibReq;
#endif /* LORAMAC_CLASSB_ENABLED */

    switch (mlmeConfirm->MlmeRequest)
    {
    case MLME_JOIN:
    {
        if (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK)
        {
            // Status is OK, node has joined the network
            _lora.callbacks->join_status(true);

            MibRequestConfirm_t mibReq = {.Type = MIB_DEV_ADDR};
            LoRaMacMibGetRequestConfirm(&mibReq);
            _lora.config->devaddr = mibReq.Param.DevAddr;

#ifdef LORAMAC_CLASSB_ENABLED
#if defined(USE_DEVICE_TIMING)
            LORA_DeviceTimeReq();
#else
            LORA_BeaconTimeReq();
#endif /* USE_DEVICE_TIMING */
#endif /* LORAMAC_CLASSB_ENABLED */
        }
        else
        {
            _lora.callbacks->join_status(false);
        }
        break;
    }
    case MLME_LINK_CHECK:
    {
        if (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK)
        {
            // Check DemodMargin
            // Check NbGateways
        }
        break;
    }
#ifdef LORAMAC_CLASSB_ENABLED
    case MLME_BEACON_ACQUISITION:
    {
        if (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK)
        {
            /* Beacon has been acquired */
            /* REquest Server for Ping Slot */
            LORA_PingSlotReq();
        }
        else
        {
            /* Beacon not acquired */
            /* Search again */
            /* we can check if the MAC has received a time reference for the beacon*/
            /* in this case do either a Device_Time_Req  or a Beacon_Timing_req*/
            LORA_BeaconReq();
        }
        break;
    }
    case MLME_PING_SLOT_INFO:
    {
        if (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK)
        {
            /* class B is now ativated*/
            mibReq.Type = MIB_DEVICE_CLASS;
            mibReq.Param.Class = CLASS_B;
            LoRaMacMibSetRequestConfirm(&mibReq);

#if defined(REGION_AU915) || defined(REGION_US915)
            mibReq.Type = MIB_PING_SLOT_DATARATE;
            mibReq.Param.PingSlotDatarate = DR_8;
            LoRaMacMibSetRequestConfirm(&mibReq);
#endif

            /*notify upper layer*/
            _lora.callbacks->confirm_class(CLASS_B);
        }
        else
        {
            LORA_PingSlotReq();
        }
        break;
    }
#if defined(USE_DEVICE_TIMING)
    case MLME_DEVICE_TIME:
    {
        if (mlmeConfirm->Status != LORAMAC_EVENT_INFO_STATUS_OK)
        {
            LORA_DeviceTimeReq();
        }
    }
#endif /* USE_DEVICE_TIMING */
#endif /* LORAMAC_CLASSB_ENABLED */
    default:
        break;
    }
}

/*!
 * \brief   MLME-Indication event function
 *
 * \param   [IN] MlmeIndication - Pointer to the indication structure.
 */
static void MlmeIndication(MlmeIndication_t *MlmeIndication)
{
#ifdef LORAMAC_CLASSB_ENABLED
    MibRequestConfirm_t mibReq;
#endif /* LORAMAC_CLASSB_ENABLED */

    switch (MlmeIndication->MlmeIndication)
    {
    case MLME_SCHEDULE_UPLINK:
    {
        // The MAC signals that we shall provide an uplink as soon as possible
        _lora.callbacks->tx_needed();
        break;
    }
#ifdef LORAMAC_CLASSB_ENABLED
    case MLME_BEACON_LOST:
    {
        // Switch to class A again
        mibReq.Type = MIB_DEVICE_CLASS;
        mibReq.Param.Class = CLASS_A;
        LoRaMacMibSetRequestConfirm(&mibReq);

        LORA_BeaconReq();
        break;
    }
    case MLME_BEACON:
    {
        if (MlmeIndication->Status == LORAMAC_EVENT_INFO_STATUS_BEACON_LOCKED)
        {
            TVL2(PRINTF("BEACON RECEIVED\n\r");)
        }
        else
        {
            TVL2(PRINTF("BEACON NOT RECEIVED\n\r");)
        }
        break;
    }
#endif /* LORAMAC_CLASSB_ENABLED */
    default:
        break;
    }
}
/**
 *  lora Init
 */
void lora_init(lora_configuration_t *config, lora_callback_t *callbacks)
{
    memset(&_lora, 0, sizeof(_lora));

    _lora.config = config;
    _lora.callbacks = callbacks;

    uint8_t empty_deveui[8] = LORA_DEVICE_EUI;

    // fill deveui if not set in eeprom
    if (memcmp(_lora.config->deveui, empty_deveui, 8) == 0)
    {
        _lora.callbacks->get_unique_Id(_lora.config->deveui);
    }
    // fill devaddr if not set in eeprom
    if (_lora.config->devaddr == 0)
    {
        srand1(_lora.callbacks->get_random_seed());
        _lora.config->devaddr = randr(0, 0x01FFFFFF);
    }

    LoRaMacPrimitives.MacMcpsConfirm = McpsConfirm;
    LoRaMacPrimitives.MacMcpsIndication = McpsIndication;
    LoRaMacPrimitives.MacMlmeConfirm = MlmeConfirm;
    LoRaMacPrimitives.MacMlmeIndication = MlmeIndication;
    LoRaMacCallbacks.GetBatteryLevel = _lora.callbacks->get_battery_level;
    LoRaMacCallbacks.GetTemperatureLevel = _lora.callbacks->get_temperature_level;
    LoRaMacCallbacks.MacProcessNotify = _lora.callbacks->mac_process_notify;
#if defined(REGION_AS923)
    LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_AS923);
    LoRaRegion = LORAMAC_REGION_AS923;
#elif defined(REGION_AU915)
    LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_AU915);
    LoRaRegion = LORAMAC_REGION_AU915;
#elif defined(REGION_CN470)
    LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_CN470);
    LoRaRegion = LORAMAC_REGION_CN470;
#elif defined(REGION_CN779)
    LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_CN779);
    LoRaRegion = LORAMAC_REGION_CN779;
#elif defined(REGION_EU433)
    LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_EU433);
    LoRaRegion = LORAMAC_REGION_EU433;
#elif defined(REGION_IN865)
    LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_IN865);
    LoRaRegion = LORAMAC_REGION_IN865;
#elif defined(REGION_EU868)
    LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_EU868);
    LoRaRegion = LORAMAC_REGION_EU868;
#elif defined(REGION_KR920)
    LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_KR920);
    LoRaRegion = LORAMAC_REGION_KR920;
#elif defined(REGION_US915)
    LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_US915);
    LoRaRegion = LORAMAC_REGION_US915;
#elif defined(REGION_RU864)
    LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_RU864);
    LoRaRegion = LORAMAC_REGION_RU864;
#else
#error "Please define a region in the compiler options."
#endif

#if defined(HYBRID)
#if defined(REGION_US915) || defined(REGION_AU915)
    uint16_t channelMask[] = {0x00FF, 0x0000, 0x0000, 0x0000, 0x0001, 0x0000};
    mibReq.Type = MIB_CHANNELS_MASK;
    mibReq.Param.ChannelsMask = channelMask;
    LoRaMacMibSetRequestConfirm(&mibReq);
    mibReq.Type = MIB_CHANNELS_DEFAULT_MASK;
    mibReq.Param.ChannelsDefaultMask = channelMask;
    LoRaMacMibSetRequestConfirm(&mibReq);
#endif
#if defined(REGION_CN470)
    uint16_t channelMask[] = {0x00FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};
    mibReq.Type = MIB_CHANNELS_MASK;
    mibReq.Param.ChannelsMask = channelMask;
    LoRaMacMibSetRequestConfirm(&mibReq);
    mibReq.Type = MIB_CHANNELS_DEFAULT_MASK;
    mibReq.Param.ChannelsDefaultMask = channelMask;
    LoRaMacMibSetRequestConfirm(&mibReq);
#endif
#endif

    lora_otaa_set(LORA_ENABLE);

    mibReq.Type = MIB_DEV_EUI;
    mibReq.Param.DevEui = _lora.config->deveui;
    LoRaMacMibSetRequestConfirm(&mibReq);

    mibReq.Type = MIB_JOIN_EUI;
    mibReq.Param.JoinEui = _lora.config->appeui;
    LoRaMacMibSetRequestConfirm(&mibReq);

    mibReq.Type = MIB_ADR;
    mibReq.Param.AdrEnable = _lora.config->adr;
    LoRaMacMibSetRequestConfirm(&mibReq);

    mibReq.Type = MIB_PUBLIC_NETWORK;
    mibReq.Param.EnablePublicNetwork = _lora.config->public_network;
    LoRaMacMibSetRequestConfirm(&mibReq);

    mibReq.Type = MIB_APP_KEY;
    mibReq.Param.AppKey = _lora.config->appkey;
    LoRaMacMibSetRequestConfirm(&mibReq);

    mibReq.Type = MIB_NWK_KEY;
    mibReq.Param.NwkKey = _lora.config->nwkkey;
    LoRaMacMibSetRequestConfirm(&mibReq);

    mibReq.Type = MIB_DEVICE_CLASS;
    mibReq.Param.Class = CLASS_A;
    LoRaMacMibSetRequestConfirm(&mibReq);

    LoRaMacTestSetDutyCycleOn(_lora.config->duty_cycle);

    mibReq.Type = MIB_SYSTEM_MAX_RX_ERROR;
    mibReq.Param.SystemMaxRxError = 20;
    LoRaMacMibSetRequestConfirm(&mibReq);

#ifdef LORAMAC_CLASSB_ENABLED
    DefaultPingSlotPeriodicity = LORA_DEFAULT_PING_SLOT_PERIODICITY;
#endif /* LORAMAC_CLASSB_ENABLED */

    /*set Mac statein Idle*/
    LoRaMacStart();
}

LoraErrorStatus lora_join(void)
{
    LoraErrorStatus status;
    if (_lora.config->otaa == LORA_ENABLE)
    {
        MlmeReq_t mlmeReq;

        mlmeReq.Type = MLME_JOIN;
        mlmeReq.Req.Join.Datarate = DR_0; // LoRaParamInit->tx_datarate;

        JoinParameters = mlmeReq.Req.Join;

        LoRaMacStatus_t s = LoRaMacMlmeRequest(&mlmeReq);

        // PRINTF("status %d %d", s, mlmeReq.ReqReturn.DutyCycleWaitTime);

        status = s == LORAMAC_STATUS_OK ? LORA_SUCCESS : LORA_ERROR;
    }
    else
    {
        /*no Join in abp*/
        status = LORA_ERROR;
    }

    return status;
}

LoraFlagStatus lora_is_join(void)
{
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_NETWORK_ACTIVATION;

    LoRaMacMibGetRequestConfirm(&mibReq);

    if (mibReq.Param.NetworkActivation == ACTIVATION_TYPE_NONE)
    {
        return LORA_RESET;
    }
    else
    {
        return LORA_SET;
    }
}

LoraErrorStatus lora_send(lora_AppData_t *AppData, LoraConfirm_t IsTxConfirmed)
{
    McpsReq_t mcpsReq;
    LoRaMacTxInfo_t txInfo;

    if (LoRaMacQueryTxPossible(AppData->BuffSize, &txInfo) != LORAMAC_STATUS_OK)
    {
        // Send empty frame in order to flush MAC commands
        mcpsReq.Type = MCPS_UNCONFIRMED;
        mcpsReq.Req.Unconfirmed.fBuffer = NULL;
        mcpsReq.Req.Unconfirmed.fBufferSize = 0;
        mcpsReq.Req.Unconfirmed.Datarate = lora_tx_datarate_get();
    }
    else
    {
        if (IsTxConfirmed == LORA_UNCONFIRMED_MSG)
        {
            mcpsReq.Type = MCPS_UNCONFIRMED;
            mcpsReq.Req.Unconfirmed.fPort = AppData->Port;
            mcpsReq.Req.Unconfirmed.fBufferSize = AppData->BuffSize;
            mcpsReq.Req.Unconfirmed.fBuffer = AppData->Buff;
            mcpsReq.Req.Unconfirmed.Datarate = lora_tx_datarate_get();
        }
        else
        {
            mcpsReq.Type = MCPS_CONFIRMED;
            mcpsReq.Req.Confirmed.fPort = AppData->Port;
            mcpsReq.Req.Confirmed.fBufferSize = AppData->BuffSize;
            mcpsReq.Req.Confirmed.fBuffer = AppData->Buff;
            mcpsReq.Req.Confirmed.NbTrials = 8;
            mcpsReq.Req.Confirmed.Datarate = lora_tx_datarate_get();
        }
    }
    if (LoRaMacMcpsRequest(&mcpsReq) == LORAMAC_STATUS_OK)
    {
        return LORA_SUCCESS;
    }
    return LORA_ERROR;
}

#ifdef LORAMAC_CLASSB_ENABLED
#if defined(USE_DEVICE_TIMING)
static LoraErrorStatus LORA_DeviceTimeReq(void)
{
    MlmeReq_t mlmeReq;

    mlmeReq.Type = MLME_DEVICE_TIME;

    if (LoRaMacMlmeRequest(&mlmeReq) == LORAMAC_STATUS_OK)
    {
        return LORA_SUCCESS;
    }
    else
    {
        return LORA_ERROR;
    }
}
#else
static LoraErrorStatus LORA_BeaconTimeReq(void)
{
    MlmeReq_t mlmeReq;

    mlmeReq.Type = MLME_BEACON_TIMING;

    if (LoRaMacMlmeRequest(&mlmeReq) == LORAMAC_STATUS_OK)
    {
        return LORA_SUCCESS;
    }
    else
    {
        return LORA_ERROR;
    }
}
#endif

static LoraErrorStatus LORA_BeaconReq(void)
{
    MlmeReq_t mlmeReq;

    mlmeReq.Type = MLME_BEACON_ACQUISITION;

    if (LoRaMacMlmeRequest(&mlmeReq) == LORAMAC_STATUS_OK)
    {
        return LORA_SUCCESS;
    }
    else
    {
        return LORA_ERROR;
    }
}

static LoraErrorStatus LORA_PingSlotReq(void)
{

    MlmeReq_t mlmeReq;

    mlmeReq.Type = MLME_LINK_CHECK;
    LoRaMacMlmeRequest(&mlmeReq);

    mlmeReq.Type = MLME_PING_SLOT_INFO;
    mlmeReq.Req.PingSlotInfo.PingSlot.Fields.Periodicity = DefaultPingSlotPeriodicity;
    mlmeReq.Req.PingSlotInfo.PingSlot.Fields.RFU = 0;

    if (LoRaMacMlmeRequest(&mlmeReq) == LORAMAC_STATUS_OK)
    {
        return LORA_SUCCESS;
    }
    else
    {
        return LORA_ERROR;
    }
}
#endif /* LORAMAC_CLASSB_ENABLED */

LoraErrorStatus lora_class_change(DeviceClass_t newClass)
{
    LoraErrorStatus Errorstatus = LORA_SUCCESS;
    MibRequestConfirm_t mibReq;
    DeviceClass_t currentClass;

    mibReq.Type = MIB_DEVICE_CLASS;
    LoRaMacMibGetRequestConfirm(&mibReq);

    currentClass = mibReq.Param.Class;
    /*attempt to swicth only if class update*/
    if (currentClass != newClass)
    {
        switch (newClass)
        {
        case CLASS_A:
        {
            mibReq.Param.Class = CLASS_A;
            if (LoRaMacMibSetRequestConfirm(&mibReq) == LORAMAC_STATUS_OK)
            {
                /*switch is instantanuous*/
                _lora.callbacks->confirm_class(CLASS_A);
            }
            else
            {
                Errorstatus = LORA_ERROR;
            }
            break;
        }
        case CLASS_B:
        {
#ifdef LORAMAC_CLASSB_ENABLED
            if (currentClass != CLASS_A)
            {
                Errorstatus = LORA_ERROR;
            }
            /*switch is not instantanuous*/
            Errorstatus = LORA_BeaconReq();
#else
            PRINTF("warning: LORAMAC_CLASSB_ENABLED has not been defined at compilation\n\r");
#endif /* LORAMAC_CLASSB_ENABLED */
            break;
        }
        case CLASS_C:
        {
            if (currentClass != CLASS_A)
            {
                Errorstatus = LORA_ERROR;
            }
            /*switch is instantanuous*/
            mibReq.Param.Class = CLASS_C;
            if (LoRaMacMibSetRequestConfirm(&mibReq) == LORAMAC_STATUS_OK)
            {
                _lora.callbacks->confirm_class(CLASS_C);
            }
            else
            {
                Errorstatus = LORA_ERROR;
            }
            break;
        }
        default:
            break;
        }
    }
    return Errorstatus;
}

uint8_t lora_class_get(void)
{
    MibRequestConfirm_t mibReq;

    mibReq.Type = MIB_DEVICE_CLASS;
    LoRaMacMibGetRequestConfirm(&mibReq);

    return mibReq.Param.Class;
}

void lora_otaa_set(LoraState_t otaa)
{
    _lora.config->otaa = otaa;

    if (_lora.config->otaa == LORA_ENABLE)
    {
        // PPRINTF("OTAA Mode enabled\n\r");
        // PPRINTF("deveui= %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n\r", HEX8(_lora.config->deveui));
        // PPRINTF("appeui= %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n\r", HEX8(_lora.config->appeui));
        // PPRINTF("appkey= %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n\r", HEX16(_lora.config->appkey));

        mibReq.Type = MIB_NETWORK_ACTIVATION;
        mibReq.Param.NetworkActivation = ACTIVATION_TYPE_NONE;
        LoRaMacMibSetRequestConfirm(&mibReq);
    }
    else
    {
        // PPRINTF("ABP Mode enabled\n\r");
        // PPRINTF("deveui= %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n\r", HEX8(_lora.config->deveui));
        // PPRINTF("DevAdd=  %08X\n\r", DevAddr);
        // PPRINTF("NwkSKey= %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n\r", HEX16(_lora.config->nwksenckey));
        // PPRINTF("appskey= %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n\r", HEX16(_lora.config->appskey));

        mibReq.Type = MIB_NET_ID;
        mibReq.Param.NetID = LORA_NETWORK_ID;
        LoRaMacMibSetRequestConfirm(&mibReq);

        mibReq.Type = MIB_DEV_ADDR;
        mibReq.Param.DevAddr = _lora.config->devaddr;
        LoRaMacMibSetRequestConfirm(&mibReq);

        mibReq.Type = MIB_F_NWK_S_INT_KEY;
        mibReq.Param.FNwkSIntKey = _lora.config->fnwksIntkey;
        LoRaMacMibSetRequestConfirm(&mibReq);

        mibReq.Type = MIB_S_NWK_S_INT_KEY;
        mibReq.Param.SNwkSIntKey = _lora.config->snwksintkey;
        LoRaMacMibSetRequestConfirm(&mibReq);

        mibReq.Type = MIB_NWK_S_ENC_KEY;
        mibReq.Param.NwkSEncKey = _lora.config->nwksenckey;
        LoRaMacMibSetRequestConfirm(&mibReq);

        mibReq.Type = MIB_APP_S_KEY;
        mibReq.Param.AppSKey = _lora.config->appskey;
        LoRaMacMibSetRequestConfirm(&mibReq);

        mibReq.Type = MIB_NETWORK_ACTIVATION;
        mibReq.Param.NetworkActivation = ACTIVATION_TYPE_ABP;
        LoRaMacMibSetRequestConfirm(&mibReq);

        // Enable legacy mode to operate according to LoRaWAN Spec. 1.0.3
        Version_t abpLrWanVersion;

        abpLrWanVersion.Value = LORA_MAC_VERSION;

        mibReq.Type = MIB_ABP_LORAWAN_VERSION;
        mibReq.Param.AbpLrWanVersion = abpLrWanVersion;
        LoRaMacMibSetRequestConfirm(&mibReq);
    }
}

LoraState_t lora_otaa_get(void)
{
    return _lora.config->otaa;
}

void lora_duty_cycle_set(LoraState_t duty_cycle)
{
    _lora.config->duty_cycle = duty_cycle;
    LoRaMacTestSetDutyCycleOn((duty_cycle == LORA_ENABLE) ? 1 : 0);
}

LoraState_t lora_duty_cycle_get(void)
{
    return _lora.config->duty_cycle;
}

uint8_t *lora_deveui_get(void)
{
    return _lora.config->deveui;
}

void lora_deveui_set(uint8_t deveui[8])
{
    memcpy1(_lora.config->deveui, deveui, sizeof(_lora.config->deveui));

    mibReq.Type = MIB_DEV_EUI;
    mibReq.Param.DevEui = _lora.config->deveui;
    LoRaMacMibSetRequestConfirm(&mibReq);
}

uint8_t *lora_appeui_get(void)
{
    return _lora.config->appeui;
}

void lora_appeui_set(uint8_t appeui[8])
{
    memcpy1(_lora.config->appeui, appeui, sizeof(_lora.config->appeui));

    mibReq.Type = MIB_JOIN_EUI;
    mibReq.Param.JoinEui = _lora.config->appeui;
    LoRaMacMibSetRequestConfirm(&mibReq);
}

uint32_t lora_devaddr_get(void)
{
    return _lora.config->devaddr;
}

void lora_devaddr_set(uint32_t devaddr)
{
    _lora.config->devaddr = devaddr;
    mibReq.Type = MIB_DEV_ADDR;
    mibReq.Param.DevAddr = _lora.config->devaddr;
    LoRaMacMibSetRequestConfirm(&mibReq);
}

uint8_t *lora_appkey_get(void)
{
    return _lora.config->appkey;
}

void lora_appkey_set(uint8_t appkey[16])
{
    memcpy1(_lora.config->appkey, appkey, sizeof(_lora.config->appkey));
    memcpy1(_lora.config->nwkkey, appkey, sizeof(_lora.config->nwkkey));

    mibReq.Type = MIB_APP_KEY;
    mibReq.Param.AppKey = _lora.config->appkey;
    LoRaMacMibSetRequestConfirm(&mibReq);

    mibReq.Type = MIB_NWK_KEY;
    mibReq.Param.NwkKey = _lora.config->nwkkey;
    LoRaMacMibSetRequestConfirm(&mibReq);
}

bool lora_public_network_get(void)
{
    return _lora.config->public_network;
}

void lora_public_network_set(bool enable)
{
    _lora.config->public_network = enable;
    mibReq.Type = MIB_PUBLIC_NETWORK;
    mibReq.Param.EnablePublicNetwork = _lora.config->public_network;
    LoRaMacMibSetRequestConfirm(&mibReq);
}

int8_t lora_snr_get(void)
{
    return _lora.Snr;
}

int16_t lora_rssi_get(void)
{
    return _lora.Rssi;
}

void lora_tx_datarate_set(int8_t tx_datarate)
{
    _lora.config->tx_datarate = tx_datarate;
}

int8_t lora_tx_datarate_get(void)
{
    return _lora.config->tx_datarate;
}

LoraState_t lora_isack_get(void)
{
    if (_lora.McpsConfirm == NULL)
    {
        return LORA_DISABLE;
    }
    else
    {
        return (_lora.McpsConfirm->AckReceived ? LORA_ENABLE : LORA_DISABLE);
    }
}

LoRaMacRegion_t lora_region_get(void)
{
    return LoRaRegion;
}

void lora_save_config(void)
{
    _lora.callbacks->config_save();
}


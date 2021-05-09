#include "lrw.h"
#include "timeServer.h"
#include "utilities.h"
#include "LoRaMac.h"
#include "LoRaMacTest.h"
#include "log.h"

static struct
{
    lrw_configuration_t *config;
    lrw_callback_t *callbacks;
    lrw_tx_params_t tx_params;
    lrw_rx_params_t rx_params;
    LoRaMacPrimitives_t mac_primitives;
    LoRaMacCallback_t mac_callbacks;
    MibRequestConfirm_t mib_req;
} lora;

static void mcps_confirm(McpsConfirm_t *param)
{
    log_debug("mcps_confirm: McpsRequest: %d, Channel: %ld AckReceived: %d", param->McpsRequest, param->Channel, param->AckReceived);

    lora.tx_params.is_mcps_confirm = 1;
    lora.tx_params.status = param->Status;
    lora.tx_params.datarate = param->Datarate;
    lora.tx_params.uplink_counter = param->UpLinkCounter;
    lora.tx_params.power = param->TxPower;
    lora.tx_params.channel = param->Channel;
    lora.tx_params.ack_received = param->AckReceived;

    if (lora.callbacks->on_tx_data != NULL)
        lora.callbacks->on_tx_data(&lora.tx_params);
}

static void mcps_indication(McpsIndication_t *param)
{
    log_debug("mcps_indication: status: %d rssi: %d", param->Status, param->Rssi);

    lora.rx_params.is_mcps_indication = 1;
    lora.rx_params.status = param->Status;

    if (lora.rx_params.status != LORAMAC_EVENT_INFO_STATUS_OK)
    {
        return;
    }

    if (param->RxData)
    {
        lora.rx_params.datarate = param->RxDatarate;
        lora.rx_params.rssi = param->Rssi;
        lora.rx_params.snr = param->Snr;
        lora.rx_params.downlink_counter = param->DownLinkCounter;
        lora.rx_params.slot = param->RxSlot;

        if (lora.callbacks->on_rx_data != NULL)
        {
            lora.callbacks->on_rx_data(param->Port, param->Buffer, param->BufferSize, &lora.rx_params);
        }
    }

    if (param->FramePending == true)
    {
        if (lora.callbacks->tx_needed != NULL)
            lora.callbacks->tx_needed();
    }
}

static void mlme_confirm(MlmeConfirm_t *param)
{
    log_debug("mlme_confirm: MlmeRequest: %d Status: %d", param->MlmeRequest, param->Status);

    lora.tx_params.is_mcps_confirm = 0;
    lora.tx_params.status = param->Status;

    if (param->MlmeRequest == MLME_JOIN)
    {
        lora.mib_req.Type = MIB_DEV_ADDR;
        LoRaMacMibGetRequestConfirm(&lora.mib_req);
        lora.config->devaddr = lora.mib_req.Param.DevAddr;

        if (param->Status == LORAMAC_EVENT_INFO_STATUS_OK)
        {
            lrw_class_change(lora.config->class);

            // overwrites the channel mask of the obtained connection to confirm
            lora.mib_req.Type = MIB_CHANNELS_MASK;
            lora.mib_req.Param.ChannelsMask = lora.config->chmask;
            LoRaMacMibSetRequestConfirm(&lora.mib_req);

            lora.callbacks->join_status(true);
        }
        else
        {
            lora.callbacks->join_status(false);
        }
    }

    // case MLME_LINK_CHECK:
    // {
    //     // Check DemodMargin
    //     // Check NbGateways
    //     break;
    // }
    // default:
    //     break;
    // }
}

static void mlme_indication(MlmeIndication_t *param)
{
    log_debug("MlmeIndication: MlmeIndication: %d Status: %d", param->MlmeIndication, param->Status);

    lora.rx_params.is_mcps_indication = 0;
    lora.rx_params.status = param->Status;

    if (param->MlmeIndication == MLME_SCHEDULE_UPLINK)
    {
        if (lora.callbacks->tx_needed != NULL)
        {
            lora.callbacks->tx_needed();
        }
    }
}

void lrw_init(lrw_configuration_t *config, lrw_callback_t *callbacks)
{
    memset(&lora, 0, sizeof(lora));

    lora.config = config;
    lora.callbacks = callbacks;

    uint8_t empty_deveui[8] = LRW_DEVICE_EUI;

    // fill deveui if not set in eeprom
    if (memcmp(lora.config->deveui, empty_deveui, 8) == 0)
    {
        lora.callbacks->get_unique_id(lora.config->deveui);
    }
    // fill devaddr if not set in eeprom
    if (lora.config->devaddr == 0)
    {
        srand1(lora.callbacks->get_random_seed());
        lora.config->devaddr = randr(0, 0x01FFFFFF);
    }

    lora.mac_primitives.MacMcpsConfirm = mcps_confirm;
    lora.mac_primitives.MacMcpsIndication = mcps_indication;
    lora.mac_primitives.MacMlmeConfirm = mlme_confirm;
    lora.mac_primitives.MacMlmeIndication = mlme_indication;
    lora.mac_callbacks.GetBatteryLevel = lora.callbacks->get_battery_level;
    lora.mac_callbacks.GetTemperatureLevel = lora.callbacks->get_temperature_level;
    lora.mac_callbacks.MacProcessNotify = lora.callbacks->mac_process_notify;

    LoRaMacInitialization(&lora.mac_primitives, &lora.mac_callbacks, (LoRaMacRegion_t)lora.config->region);

#if defined(HYBRID)
#if defined(REGION_US915) || defined(REGION_AU915)
    uint16_t channelMask[] = {0x00FF, 0x0000, 0x0000, 0x0000, 0x0001, 0x0000};
    lora.mib_req.Type = MIB_CHANNELS_MASK;
    lora.mib_req.Param.ChannelsMask = channelMask;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);
    lora.mib_req.Type = MIB_CHANNELS_DEFAULT_MASK;
    lora.mib_req.Param.ChannelsDefaultMask = channelMask;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);
#endif
#if defined(REGION_CN470)
    uint16_t channelMask[] = {0x00FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};
    lora.mib_req.Type = MIB_CHANNELS_MASK;
    lora.mib_req.Param.ChannelsMask = channelMask;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);
    lora.mib_req.Type = MIB_CHANNELS_DEFAULT_MASK;
    lora.mib_req.Param.ChannelsDefaultMask = channelMask;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);
#endif
#endif

    // Restore chennel mask from eeprom, need change both !!!
    // log_dump(lora.config->chmask, 12, "config.chmask");
    lora.mib_req.Type = MIB_CHANNELS_MASK;
    lora.mib_req.Param.ChannelsMask = lora.config->chmask;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);
    lora.mib_req.Type = MIB_CHANNELS_DEFAULT_MASK;
    lora.mib_req.Param.ChannelsDefaultMask = lora.config->chmask;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    lora.mib_req.Type = MIB_CHANNELS_NB_TRANS;
    lora.mib_req.Param.ChannelsNbTrans = lora.config->tx_repeats;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    lrw_otaa_set(LRW_ENABLE);

    lora.mib_req.Type = MIB_DEV_EUI;
    lora.mib_req.Param.DevEui = lora.config->deveui;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    lora.mib_req.Type = MIB_JOIN_EUI;
    lora.mib_req.Param.JoinEui = lora.config->appeui;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    lora.mib_req.Type = MIB_ADR;
    lora.mib_req.Param.AdrEnable = lora.config->adr;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    lora.mib_req.Type = MIB_PUBLIC_NETWORK;
    lora.mib_req.Param.EnablePublicNetwork = lora.config->public_network;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    lora.mib_req.Type = MIB_APP_KEY;
    lora.mib_req.Param.AppKey = lora.config->appkey;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    lora.mib_req.Type = MIB_NWK_KEY;
    lora.mib_req.Param.NwkKey = lora.config->nwkkey;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    lora.mib_req.Type = MIB_DEVICE_CLASS;
    lora.mib_req.Param.Class = CLASS_A;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    LoRaMacTestSetDutyCycleOn(lora.config->duty_cycle);

    lora.mib_req.Type = MIB_SYSTEM_MAX_RX_ERROR;
    lora.mib_req.Param.SystemMaxRxError = 20;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    LoRaMacStart();
}

void lrw_process(void)
{
    LoRaMacProcess();
}

bool lrw_is_busy(void)
{
    if (LoRaMacIsBusy())
        return true;

    // if (!lrw_is_join())
    // {
    //     return true;
    // }

    return false;
}

bool lrw_join(void)
{
    if (LoRaMacIsBusy())
        return false;

    if (!lrw_otaa_get())
        return false;

    MlmeReq_t mlmeReq;
    mlmeReq.Type = MLME_JOIN;
    mlmeReq.Req.Join.Datarate = DR_0; // LoRaParamInit->tx_datarate;
    LoRaMacStatus_t status = LoRaMacMlmeRequest(&mlmeReq);
    // log_debug("status %d %d", status, mlmeReq.ReqReturn.DutyCycleWaitTime);
    return status == LORAMAC_STATUS_OK;
}

bool lrw_is_join(void)
{
    lora.mib_req.Type = MIB_NETWORK_ACTIVATION;
    if (LoRaMacMibGetRequestConfirm(&lora.mib_req) != LORAMAC_STATUS_OK)
        return false;
    return lora.mib_req.Param.NetworkActivation != ACTIVATION_TYPE_NONE;
}

bool lrw_send(uint8_t port, void *buffer, uint8_t length, bool confirmed)
{
    McpsReq_t mcpsReq;
    LoRaMacTxInfo_t txInfo;

    if (LoRaMacQueryTxPossible(length, &txInfo) != LORAMAC_STATUS_OK)
    {
        // Send empty frame in order to flush MAC commands
        mcpsReq.Type = MCPS_UNCONFIRMED;
        mcpsReq.Req.Unconfirmed.fBuffer = NULL;
        mcpsReq.Req.Unconfirmed.fBufferSize = 0;
        mcpsReq.Req.Unconfirmed.Datarate = lrw_tx_datarate_get();
    }
    else
    {
        if (confirmed == LRW_UNCONFIRMED_MSG)
        {
            mcpsReq.Type = MCPS_UNCONFIRMED;
            mcpsReq.Req.Unconfirmed.fPort = port;
            mcpsReq.Req.Unconfirmed.fBufferSize = length;
            mcpsReq.Req.Unconfirmed.fBuffer = buffer;
            mcpsReq.Req.Unconfirmed.Datarate = lrw_tx_datarate_get();
        }
        else
        {
            mcpsReq.Type = MCPS_CONFIRMED;
            mcpsReq.Req.Confirmed.fPort = port;
            mcpsReq.Req.Confirmed.fBufferSize = length;
            mcpsReq.Req.Confirmed.fBuffer = buffer;
            mcpsReq.Req.Confirmed.NbTrials = 8;
            mcpsReq.Req.Confirmed.Datarate = lrw_tx_datarate_get();
        }
    }
    if (LoRaMacMcpsRequest(&mcpsReq) == LORAMAC_STATUS_OK)
    {
        return true;
    }
    return false;
}

bool lrw_class_change(DeviceClass_t new_class)
{
    lora.mib_req.Type = MIB_DEVICE_CLASS;
    LoRaMacMibGetRequestConfirm(&lora.mib_req);

    if (new_class == lora.mib_req.Param.Class)
        return true;

    lora.mib_req.Param.Class = new_class;
    return LoRaMacMibSetRequestConfirm(&lora.mib_req) == LORAMAC_STATUS_OK;
}

uint8_t lrw_class_get(void)
{
    lora.mib_req.Type = MIB_DEVICE_CLASS;
    LoRaMacMibGetRequestConfirm(&lora.mib_req);

    return lora.mib_req.Param.Class;
}

void lrw_otaa_set(LoraState_t otaa)
{
    lora.config->otaa = otaa;

    if (lora.config->otaa == LRW_ENABLE)
    {
        lora.mib_req.Type = MIB_NETWORK_ACTIVATION;
        lora.mib_req.Param.NetworkActivation = ACTIVATION_TYPE_NONE;
        LoRaMacMibSetRequestConfirm(&lora.mib_req);
    }
    else
    {
        lora.mib_req.Type = MIB_NET_ID;
        lora.mib_req.Param.NetID = LRW_NETWORK_ID;
        LoRaMacMibSetRequestConfirm(&lora.mib_req);

        lora.mib_req.Type = MIB_DEV_ADDR;
        lora.mib_req.Param.DevAddr = lora.config->devaddr;
        LoRaMacMibSetRequestConfirm(&lora.mib_req);

        lora.mib_req.Type = MIB_F_NWK_S_INT_KEY;
        lora.mib_req.Param.FNwkSIntKey = lora.config->fnwksIntkey;
        LoRaMacMibSetRequestConfirm(&lora.mib_req);

        lora.mib_req.Type = MIB_S_NWK_S_INT_KEY;
        lora.mib_req.Param.SNwkSIntKey = lora.config->snwksintkey;
        LoRaMacMibSetRequestConfirm(&lora.mib_req);

        lora.mib_req.Type = MIB_NWK_S_ENC_KEY;
        lora.mib_req.Param.NwkSEncKey = lora.config->nwksenckey;
        LoRaMacMibSetRequestConfirm(&lora.mib_req);

        lora.mib_req.Type = MIB_APP_S_KEY;
        lora.mib_req.Param.AppSKey = lora.config->appskey;
        LoRaMacMibSetRequestConfirm(&lora.mib_req);

        lora.mib_req.Type = MIB_NETWORK_ACTIVATION;
        lora.mib_req.Param.NetworkActivation = ACTIVATION_TYPE_ABP;
        LoRaMacMibSetRequestConfirm(&lora.mib_req);

        // Enable legacy mode to operate according to LoRaWAN Spec. 1.0.3
        Version_t abpLrWanVersion;
        abpLrWanVersion.Value = LRW_MAC_VERSION;
        lora.mib_req.Type = MIB_ABP_LORAWAN_VERSION;
        lora.mib_req.Param.AbpLrWanVersion = abpLrWanVersion;
        LoRaMacMibSetRequestConfirm(&lora.mib_req);
    }
}

LoraState_t lrw_otaa_get(void)
{
    return lora.config->otaa;
}

void lrw_duty_cycle_set(bool duty_cycle)
{
    lora.config->duty_cycle = duty_cycle;
    LoRaMacTestSetDutyCycleOn((duty_cycle == LRW_ENABLE) ? 1 : 0);
}

bool lrw_duty_cycle_get(void)
{
    return lora.config->duty_cycle;
}

uint8_t *lrw_deveui_get(void)
{
    return lora.config->deveui;
}

void lrw_deveui_set(uint8_t deveui[8])
{
    memcpy1(lora.config->deveui, deveui, sizeof(lora.config->deveui));

    lora.mib_req.Type = MIB_DEV_EUI;
    lora.mib_req.Param.DevEui = lora.config->deveui;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);
}

uint8_t *lrw_appeui_get(void)
{
    return lora.config->appeui;
}

void lrw_appeui_set(uint8_t appeui[8])
{
    memcpy1(lora.config->appeui, appeui, sizeof(lora.config->appeui));

    lora.mib_req.Type = MIB_JOIN_EUI;
    lora.mib_req.Param.JoinEui = lora.config->appeui;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);
}

uint32_t lrw_devaddr_get(void)
{
    return lora.config->devaddr;
}

void lrw_devaddr_set(uint32_t devaddr)
{
    lora.config->devaddr = devaddr;
    lora.mib_req.Type = MIB_DEV_ADDR;
    lora.mib_req.Param.DevAddr = lora.config->devaddr;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);
}

uint8_t *lrw_appkey_get(void)
{
    return lora.config->appkey;
}

void lrw_appkey_set(uint8_t appkey[16])
{
    memcpy1(lora.config->appkey, appkey, sizeof(lora.config->appkey));
    memcpy1(lora.config->nwkkey, appkey, sizeof(lora.config->nwkkey));

    lora.mib_req.Type = MIB_APP_KEY;
    lora.mib_req.Param.AppKey = lora.config->appkey;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);

    lora.mib_req.Type = MIB_NWK_KEY;
    lora.mib_req.Param.NwkKey = lora.config->nwkkey;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);
}

bool lrw_public_network_get(void)
{
    return lora.config->public_network;
}

void lrw_public_network_set(bool enable)
{
    lora.config->public_network = enable;
    lora.mib_req.Type = MIB_PUBLIC_NETWORK;
    lora.mib_req.Param.EnablePublicNetwork = lora.config->public_network;
    LoRaMacMibSetRequestConfirm(&lora.mib_req);
}

int8_t lrw_snr_get(void)
{
    return lora.rx_params.snr;
}

int16_t lrw_rssi_get(void)
{
    return lora.rx_params.rssi;
}

void lrw_tx_datarate_set(int8_t tx_datarate)
{
    lora.config->tx_datarate = tx_datarate;
}

int8_t lrw_tx_datarate_get(void)
{
    return lora.config->tx_datarate;
}

LoraState_t lrw_isack_get(void)
{
    return lora.tx_params.ack_received;
}

LoRaMacRegion_t lrw_region_get(void)
{
    return lora.config->region;
}

bool lrw_region_set(LoRaMacRegion_t region)
{
    // test is region build
    if (!RegionIsActive(region))
        return false;

    lora.config->region = region;

    // set default duty cycle for region
    GetPhyParams_t req = {.Attribute = PHY_DUTY_CYCLE};
    PhyParam_t resp = RegionGetPhyParam(region, &req);
    lrw_duty_cycle_set(resp.Value != 0);

    // TODO
    // InitDefaultsParams_t init_param = { .Type = INIT_TYPE_DEFAULTS };
    // RegionInitDefaults(region, &init_param);

    // get default channel mask
    // req.Attribute = PHY_CHANNELS_DEFAULT_MASK;
    // PhyParam_t respm = RegionGetPhyParam(region, &req);
    // log_dump(respm.ChannelsMask, lrw_get_channels_mask_length() * 2, "respm.ChannelsMask");
    // log_debug("respm.ChannelsMask[0] %d %04x", respm.ChannelsMask[0], respm.ChannelsMask[0]);
    // memset(lora.config->channels_mask, 0, sizeof(lora.config->channels_mask));
    // RegionCommonChanMaskCopy(lora.config->channels_mask, respm.ChannelsMask, lrw_get_channels_mask_length());
    // log_dump(lora.config->channels_mask, lrw_get_channels_mask_length() * 2, "lora.config->channels_mask");

    return true;
}

uint8_t lrw_get_chmask_length(void)
{
    // REGION_NVM_CHANNELS_MASK_SIZE
    if ((lora.config->region == LORAMAC_REGION_AU915) ||
        (lora.config->region == LORAMAC_REGION_US915) ||
        (lora.config->region == LORAMAC_REGION_CN470))
        return 6;
    return 1;
}

lrw_channel_list_t lrw_get_channel_list(void)
{
    lrw_channel_list_t result;

    result.chmask_length = lrw_get_chmask_length();

    GetPhyParams_t req = {.Attribute = PHY_MAX_NB_CHANNELS};
    PhyParam_t resp = RegionGetPhyParam(lrw_region_get(), &req);
    result.length = resp.Value;

    lora.mib_req.Type = MIB_CHANNELS;
    LoRaMacMibGetRequestConfirm(&lora.mib_req);
    result.channels = lora.mib_req.Param.ChannelList;

    lora.mib_req.Type = MIB_CHANNELS_MASK;
    LoRaMacMibGetRequestConfirm(&lora.mib_req);
    result.chmask = lora.mib_req.Param.ChannelsMask;

    lora.mib_req.Type = MIB_CHANNELS_DEFAULT_MASK;
    LoRaMacMibGetRequestConfirm(&lora.mib_req);
    result.chmask_default = lora.mib_req.Param.ChannelsDefaultMask;

    return result;
}

bool lrw_chmask_set(uint16_t chmask[LRW_CHMASK_LENGTH])
{
    memcpy(lora.config->chmask, chmask, sizeof(lora.config->chmask));

    lora.mib_req.Type = MIB_CHANNELS_MASK;
    lora.mib_req.Param.ChannelsMask = lora.config->chmask;
    if (LoRaMacMibSetRequestConfirm(&lora.mib_req) != LORAMAC_STATUS_OK)
        return false;

    lora.mib_req.Type = MIB_CHANNELS_DEFAULT_MASK;
    lora.mib_req.Param.ChannelsDefaultMask = lora.config->chmask;
    return LoRaMacMibSetRequestConfirm(&lora.mib_req) == LORAMAC_STATUS_OK;
}

bool lrw_unconfirmed_message_repeats_set(uint8_t repeats)
{
    lora.config->tx_repeats = repeats;
    lora.mib_req.Type = MIB_CHANNELS_NB_TRANS;
    lora.mib_req.Param.ChannelsNbTrans = lora.config->tx_repeats;
    return LoRaMacMibSetRequestConfirm(&lora.mib_req) == LORAMAC_STATUS_OK;
}

uint8_t lrw_unconfirmed_message_repeats_get(void)
{
    return lora.config->tx_repeats;
}

void lrw_save_config(void)
{
    lora.callbacks->config_save();
}

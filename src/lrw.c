#include "lrw.h"
#include <LoRaWAN/Utilities/timeServer.h>
#include <LoRaWAN/Utilities/utilities.h>
#include <loramac-node/src/mac/LoRaMac.h>
#include <loramac-node/src/mac/LoRaMacTest.h>
#include <loramac-node/src/mac/region/Region.h>
#include <loramac-node/src/radio/radio.h>
#include "adc.h"
#include "cmd.h"
#include "system.h"
#include "halt.h"
#include "log.h"
#include "part.h"
#include "utils.h"
#include "nvm.h"

#define MAX_BAT 254


static McpsConfirm_t tx_params;
McpsIndication_t lrw_rx_params;


static struct {
    const char *name;
    int id;
} region_map[] = {
    { "AS923", LORAMAC_REGION_AS923 },
    { "AU915", LORAMAC_REGION_AU915 },
    { "CN470", LORAMAC_REGION_CN470 },
    { "CN779", LORAMAC_REGION_CN779 },
    { "EU433", LORAMAC_REGION_EU433 },
    { "EU868", LORAMAC_REGION_EU868 },
    { "KR920", LORAMAC_REGION_KR920 },
    { "IN865", LORAMAC_REGION_IN865 },
    { "US915", LORAMAC_REGION_US915 },
    { "RU864", LORAMAC_REGION_RU864 }
};


static uint16_t nvm_flags;
static struct {
    part_t crypto;
    part_t mac1;
    part_t mac2;
    part_t se;
    part_t region1;
    part_t region2;
    part_t classb;
} nvm_parts;


static int region2id(const char *name)
{
    if (name == NULL) return -1;

    for (unsigned int i = 0; i < sizeof(region_map) / sizeof(region_map[0]); i++)
        if (!strcmp(region_map[i].name, name)) return region_map[i].id;
    return -2;
}


static const char *region2str(int id)
{
    for (unsigned int i = 0; i < sizeof(region_map) / sizeof(region_map[0]); i++)
        if (region_map[i].id == id ) return region_map[i].name;
    return NULL;
}



static uint8_t get_battery_level(void)
{
    // callback to get the battery level in % of full charge (254 full charge, 0
    // no charge)
    return MAX_BAT;
}


static void process_notify(void)
{
    // The LoRa radio generated an IRQ. Disable sleep so that LoRaMacProcess()
    // gets invoked immediately to handle the event.
    system_disallow_sleep(SYSTEM_MODULE_RADIO);
}


static void save_state(void)
{
    LoRaMacStatus_t rc;
    LoRaMacNvmData_t *s;

    if (nvm_flags == LORAMAC_NVM_NOTIFY_FLAG_NONE)
        return;

    rc = LoRaMacStop();
    if (rc != LORAMAC_STATUS_OK) {
        log_error("LoRaMac: Error while stopping in save_state: %d", rc);
        return;
    }

    s = lrw_get_state();

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_CRYPTO) {
        log_debug("Saving Crypto state to NVM");
        if (!part_write(&nvm_parts.crypto, 0, &s->Crypto, sizeof(s->Crypto)))
            log_error("Error while writing Crypto state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_MAC_GROUP1) {
        log_debug("Saving MacGroup1 state to NVM");
        if (!part_write(&nvm_parts.mac1, 0, &s->MacGroup1, sizeof(s->MacGroup1)))
            log_error("Error while writing MacGroup1 state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_MAC_GROUP2) {
        log_debug("Saving MacGroup2 state to NVM");
        if (!part_write(&nvm_parts.mac2, 0, &s->MacGroup2, sizeof(s->MacGroup2)))
            log_error("Error while writing MacGroup2 state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_SECURE_ELEMENT) {
        log_debug("Saving SecureElement state to NVM");
        if (!part_write(&nvm_parts.se, 0, &s->SecureElement, sizeof(s->SecureElement)))
            log_error("Error while writing SecureElement state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_REGION_GROUP1) {
        log_debug("Saving RegionGroup1 state to NVM");
        if (!part_write(&nvm_parts.region1, 0, &s->RegionGroup1, sizeof(s->RegionGroup1)))
            log_error("Error while writing RegionGroup1 state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_REGION_GROUP2) {
        log_debug("Saving RegionGroup2 state to NVM");
        if (!part_write(&nvm_parts.region2, 0, &s->RegionGroup2, sizeof(s->RegionGroup2)))
            log_error("Error while writing RegionGroup2 state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_CLASS_B) {
        log_debug("Saving ClassB state to NVM");
        if (!part_write(&nvm_parts.classb, 0, &s->ClassB, sizeof(s->ClassB)))
            log_error("Error while writing ClassB state to NVM");
    }

    nvm_flags = LORAMAC_NVM_NOTIFY_FLAG_NONE;
    LoRaMacStart();
}


static void restore_state(void)
{
    size_t size;
    const unsigned char *p;
    LoRaMacNvmData_t s;

    memset(&s, 0, sizeof(s));

    p = part_mmap(&size, &nvm_parts.crypto);
    if (p && size >= sizeof(s.Crypto)) memcpy(&s.Crypto, p, sizeof(s.Crypto));

    p = part_mmap(&size, &nvm_parts.mac1);
    if (p && size >= sizeof(s.MacGroup1)) memcpy(&s.MacGroup1, p, sizeof(s.MacGroup1));

    p = part_mmap(&size, &nvm_parts.mac2);
    if (p && size >= sizeof(s.MacGroup2)) memcpy(&s.MacGroup2, p, sizeof(s.MacGroup2));

    p = part_mmap(&size, &nvm_parts.se);
    if (p && size >= sizeof(s.SecureElement)) memcpy(&s.SecureElement, p, sizeof(s.SecureElement));

    p = part_mmap(&size, &nvm_parts.region1);
    if (p && size >= sizeof(s.RegionGroup1)) memcpy(&s.RegionGroup1, p, sizeof(s.RegionGroup1));

    p = part_mmap(&size, &nvm_parts.region2);
    if (p && size >= sizeof(s.RegionGroup2)) memcpy(&s.RegionGroup2, p, sizeof(s.RegionGroup2));

    p = part_mmap(&size, &nvm_parts.classb);
    if (p && size >= sizeof(s.ClassB)) memcpy(&s.ClassB, p, sizeof(s.ClassB));

    MibRequestConfirm_t r = {
        .Type = MIB_NVM_CTXS,
        .Param = { .Contexts = &s }
    };
    int rc = LoRaMacMibSetRequestConfirm(&r);
    if (rc != LORAMAC_STATUS_OK)
        log_error("LoRaMac: Error while restoring NVM state: %d", rc);
}


static int restore_region()
{
    size_t size;
    LoRaMacRegion_t region;
    uint32_t crc;

    const LoRaMacNvmDataGroup2_t *p = part_mmap(&size, &nvm_parts.mac2);
    if (p == NULL) goto out;
    if (size < sizeof(LoRaMacNvmDataGroup2_t)) goto out;

    // Only restore the region parameter value if the crc32 checksum over the
    // entire block matches, or if the checksum calculate over the region
    // parameter only matches. The latter is a special case used by
    // lrw_set_region to indicate that the structure has a valid region value,
    // but the entire block should not be restored. This is used to
    // re-initialize the parameters from defaults when switching regions.

    memcpy(&crc, &p->Crc32, sizeof(crc));

    if (check_block_crc(p, size) || Crc32((uint8_t *)&p->Region, sizeof(p->Region)) == crc) {
        memcpy(&region, &p->Region, sizeof(region));
        return region;
    }

out:
    return region2id(DEFAULT_ACTIVE_REGION);
}


static void nvm_data_change(uint16_t flags)
{
    nvm_flags |= flags;
}


static void on_ack(bool ack_received)
{
    if (ack_received) {
        cmd_print("+ACK\r\n\r\n");
    } else {
        cmd_print("+NOACK\r\n\r\n");
    }
}


static void recv(uint8_t port, uint8_t *buffer, uint8_t length)
{
    atci_printf("+RECV=%d,%d\r\n\r\n", port, length);

    if (sysconf.data_format) {
        atci_print_buffer_as_hex(buffer, length);
    } else {
        atci_write((char *) buffer, length);
    }
}


static void mcps_confirm(McpsConfirm_t *param)
{
    log_debug("mcps_confirm: McpsRequest: %d, Channel: %ld AckReceived: %d", param->McpsRequest, param->Channel, param->AckReceived);
    tx_params = *param;

    if (param->McpsRequest == MCPS_CONFIRMED)
        on_ack(param->AckReceived == 1);
}


static void mcps_indication(McpsIndication_t *param)
{
    log_debug("mcps_indication: status: %d rssi: %d", param->Status, param->Rssi);

    lrw_rx_params.Status = param->Status;

    if (lrw_rx_params.Status != LORAMAC_EVENT_INFO_STATUS_OK) {
        return;
    }

    if (param->RxData) {
        lrw_rx_params.RxDatarate = param->RxDatarate;
        lrw_rx_params.Rssi = param->Rssi;
        lrw_rx_params.Snr = param->Snr;
        lrw_rx_params.DownLinkCounter = param->DownLinkCounter;
        lrw_rx_params.RxSlot = param->RxSlot;

        recv(param->Port, param->Buffer, param->BufferSize);
    }

    if (param->IsUplinkTxPending == true) {
        // do nothing for now
    }
}


// Copy the device class value from sys config to the MIB. The value in MIB can
// be overwritten by LoRaMac at runtime, e.g., after a Join.
static int sync_device_class(void)
{
    int rc;
    MibRequestConfirm_t r = { .Type = MIB_DEVICE_CLASS };

    rc = LoRaMacMibGetRequestConfirm(&r);
    if (rc != LORAMAC_STATUS_OK) return rc;

    if (r.Param.Class == sysconf.device_class)
        return LORAMAC_STATUS_OK;

    r.Param.Class = sysconf.device_class;
    return LoRaMacMibSetRequestConfirm(&r);
}


#ifdef LORAMAC_ABP_VERSION
static int set_abp_mac_version(void)
{
    // If we are in ABP mode and the application has defined a specific MAC
    // version to be used in this mode, set it now. There is no automatic
    // version negotiation in ABP mode, so this needs to be done manually.
    MibRequestConfirm_t r = {
        .Type = MIB_ABP_LORAWAN_VERSION,
        .Param = { .AbpLrWanVersion = { .Value = LORAMAC_ABP_VERSION }}};
    return LoRaMacMibSetRequestConfirm(&r);
}
#endif


static void mlme_confirm(MlmeConfirm_t *param)
{
    log_debug("mlme_confirm: MlmeRequest: %d Status: %d", param->MlmeRequest, param->Status);
    tx_params.Status = param->Status;

    if (param->MlmeRequest == MLME_JOIN) {
        MibRequestConfirm_t r = { .Type = MIB_NETWORK_ACTIVATION };
        LoRaMacMibGetRequestConfirm(&r);

        if (param->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
            // TODO: Restore channel mask from a previously saved version in
            // case the LNS has the wrong channel mask configured.

#ifdef LORAMAC_ABP_VERSION
            if (r.Param.NetworkActivation == ACTIVATION_TYPE_ABP)
                set_abp_mac_version();
#endif
        }

        if (r.Param.NetworkActivation != ACTIVATION_TYPE_ABP)
            cmd_event(CMD_EVENT_JOIN, param->Status == LORAMAC_EVENT_INFO_STATUS_OK
                ? CMD_JOIN_SUCCEEDED
                : CMD_JOIN_FAILED);

        // During the Join operation, LoRaMac internally switches the device
        // class to class A. Thus, we need to restore the original class from
        // sysconf.device_class here.
        sync_device_class();
    } else if (param->MlmeRequest == MLME_LINK_CHECK) {
        if (param->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
            cmd_event(CMD_EVENT_NETWORK, CMD_NET_ANSWER);
            cmd_ans(param->DemodMargin, param->NbGateways);
        } else {
            cmd_event(CMD_EVENT_NETWORK, CMD_NET_NOANSWER);
        }
    }
}


static void mlme_indication(MlmeIndication_t *param)
{
    log_debug("MlmeIndication: MlmeIndication: %d Status: %d", param->MlmeIndication, param->Status);
    lrw_rx_params.Status = param->Status;
}


static LoRaMacPrimitives_t primitives = {
    .MacMcpsConfirm    = mcps_confirm,
    .MacMcpsIndication = mcps_indication,
    .MacMlmeConfirm    = mlme_confirm,
    .MacMlmeIndication = mlme_indication
};


static LoRaMacCallback_t callbacks = {
    .GetBatteryLevel     = get_battery_level,
    .GetTemperatureLevel = adc_get_temperature_celsius,
    .NvmDataChange       = nvm_data_change,
    .MacProcessNotify    = process_notify
};


static void init_nvm(const part_block_t *nvm_block)
{
    if (part_find(&nvm_parts.crypto, nvm_block, "crypto") &&
        part_create(&nvm_parts.crypto, nvm_block, "crypto", sizeof(LoRaMacCryptoNvmData_t)))
        goto error;

    if (part_find(&nvm_parts.mac1, nvm_block, "mac1") &&
        part_create(&nvm_parts.mac1, nvm_block, "mac1", sizeof(LoRaMacNvmDataGroup1_t)))
        goto error;

    if (part_find(&nvm_parts.mac2, nvm_block, "mac2") &&
        part_create(&nvm_parts.mac2, nvm_block, "mac2", sizeof(LoRaMacNvmDataGroup2_t)))
        goto error;

    if (part_find(&nvm_parts.se, nvm_block, "se") &&
        part_create(&nvm_parts.se, nvm_block, "se", sizeof(SecureElementNvmData_t)))
        goto error;

    if (part_find(&nvm_parts.region1, nvm_block, "region1") &&
        part_create(&nvm_parts.region1, nvm_block, "region1", sizeof(RegionNvmDataGroup1_t)))
        goto error;

    if (part_find(&nvm_parts.region2, nvm_block, "region2") &&
        part_create(&nvm_parts.region2, nvm_block, "region2", sizeof(RegionNvmDataGroup2_t)))
        goto error;

    if (part_find(&nvm_parts.classb, nvm_block, "classb") &&
        part_create(&nvm_parts.classb, nvm_block, "classb", sizeof(LoRaMacClassBNvmData_t)))
        goto error;

    return;
error:
    halt("Could not initialize NVM");
}


static void log_device_info(void)
{
    MibRequestConfirm_t r;

    log_compose();
    log_debug("LoRaMac: Device");

    r.Type = MIB_DEV_EUI;
    LoRaMacMibGetRequestConfirm(&r);
    log_debug(" DevEUI: %02X%02X%02X%02X%02X%02X%02X%02X",
        r.Param.DevEui[0], r.Param.DevEui[1], r.Param.DevEui[2], r.Param.DevEui[3],
        r.Param.DevEui[4], r.Param.DevEui[5], r.Param.DevEui[6], r.Param.DevEui[7]);

    r.Type = MIB_DEVICE_CLASS;
    LoRaMacMibGetRequestConfirm(&r);
    log_debug(" class: %c", r.Param.Class + 'A');

    r.Type = MIB_ADR;
    LoRaMacMibGetRequestConfirm(&r);
    log_debug(" ADR: %d", r.Param.AdrEnable);

    log_finish();
}


static void log_network_info(void)
{
    MibRequestConfirm_t r;

    log_compose();
    log_debug("LoRaMac: Network");

    r.Type = MIB_PUBLIC_NETWORK;
    LoRaMacMibGetRequestConfirm(&r);
    log_debug(" public: %d", r.Param.EnablePublicNetwork);

    r.Type = MIB_NETWORK_ACTIVATION;
    LoRaMacMibGetRequestConfirm(&r);
    log_debug(" activated: ");
    switch(r.Param.NetworkActivation) {
        case ACTIVATION_TYPE_NONE: log_debug("No");   break;
        case ACTIVATION_TYPE_ABP : log_debug("ABP");  break;
        case ACTIVATION_TYPE_OTAA: log_debug("OTAA"); break;
        default: log_debug("?"); break;
    }

    if (r.Param.NetworkActivation != ACTIVATION_TYPE_NONE) {
        r.Type = MIB_LORAWAN_VERSION;
        LoRaMacMibGetRequestConfirm(&r);
        log_debug(" MAC: %d.%d.%d",
            r.Param.LrWanVersion.LoRaWan.Fields.Major,
            r.Param.LrWanVersion.LoRaWan.Fields.Minor,
            r.Param.LrWanVersion.LoRaWan.Fields.Patch);

        r.Type = MIB_NET_ID;
        LoRaMacMibGetRequestConfirm(&r);
        log_debug(" NetID: %08lX", r.Param.NetID);

        r.Type = MIB_DEV_ADDR;
        LoRaMacMibGetRequestConfirm(&r);
        log_debug(" DevAddr: %08lX", r.Param.DevAddr);
    }

    log_finish();
}


void lrw_init(const part_block_t *nvm_block)
{
    static const uint8_t zero_eui[SE_EUI_SIZE];
    LoRaMacStatus_t rc;
    MibRequestConfirm_t r;

    memset(&tx_params, 0, sizeof(tx_params));
    memset(&lrw_rx_params, 0, sizeof(lrw_rx_params));

    init_nvm(nvm_block);
    LoRaMacRegion_t region = restore_region();

    log_debug("LoRaMac: Initializing for region %s, regional parameters RP%03d-%d.%d.%d",
        region2str(region), REGION_VERSION >> 24, (REGION_VERSION >> 16) & 0xff,
        (REGION_VERSION >> 8) & 0xff, REGION_VERSION & 0xff);
    rc = LoRaMacInitialization(&primitives, &callbacks, region);
    switch(rc) {
        case LORAMAC_STATUS_OK:
            break;

        case LORAMAC_STATUS_PARAMETER_INVALID:
            halt("LoRaMac: Invalid initialization parameter(s)");
            break;

        case LORAMAC_STATUS_REGION_NOT_SUPPORTED:
            log_error("LoRaMac: Unsupported region %s", region2str(region));
            return;

        default:
            halt("LoRaMac: Initialization error");
            return;
    }

    restore_state();

    r.Type = MIB_SYSTEM_MAX_RX_ERROR;
    r.Param.SystemMaxRxError = 20;
    LoRaMacMibSetRequestConfirm(&r);

    sync_device_class();

    r.Type = MIB_DEV_EUI;
    LoRaMacMibGetRequestConfirm(&r);
    uint8_t *deveui = r.Param.DevEui;

    // If we get a DevEUI consisting of all zeroes, generate a unique one based
    // off of the MCU's unique id.
    if (!memcmp(deveui, zero_eui, sizeof(zero_eui))) {
        system_get_unique_id(deveui);
        rc = LoRaMacMibSetRequestConfirm(&r);
        if (rc != LORAMAC_STATUS_OK)
            log_error("LoRaMac: Error while setting DevEUI: %d", rc);
    }

    log_device_info();

    r.Type = MIB_DEV_ADDR;
    LoRaMacMibGetRequestConfirm(&r);
    uint32_t devaddr = r.Param.DevAddr;

    // If we get a zero DevAddr, generate a unique one from the MCU's unique ID.
    if (devaddr == 0) {
        r.Param.DevAddr = devaddr = randr(0, 0x01FFFFFF);
        rc = LoRaMacMibSetRequestConfirm(&r);
        if (rc != LORAMAC_STATUS_OK)
            log_error("LoRaMac: Error while setting DevAddr: %d", rc);
    }

    log_network_info();
}


int lrw_send(uint8_t port, void *buffer, uint8_t length, bool confirmed)
{
    McpsReq_t mr;
    LoRaMacTxInfo_t txi;
    LoRaMacStatus_t rc;

    MibRequestConfirm_t r = { .Type = MIB_CHANNELS_DATARATE };
    LoRaMacMibGetRequestConfirm(&r);

    rc = LoRaMacQueryTxPossible(length, &txi);
    if (rc != LORAMAC_STATUS_OK) {
        // The payload is too long to fit into the message or there is some
        // other error.
        log_debug("Cannot transmit %d bytes", length);

        // Send an empty frame in order to flush MAC commands
        mr.Type = MCPS_UNCONFIRMED;
        mr.Req.Unconfirmed.fBuffer = NULL;
        mr.Req.Unconfirmed.fBufferSize = 0;
        mr.Req.Unconfirmed.Datarate = r.Param.ChannelsDatarate;
        return rc;
    }

    if (confirmed == false) {
        mr.Type = MCPS_UNCONFIRMED;
        mr.Req.Unconfirmed.fPort = port;
        mr.Req.Unconfirmed.fBufferSize = length;
        mr.Req.Unconfirmed.fBuffer = buffer;
        mr.Req.Unconfirmed.Datarate = r.Param.ChannelsDatarate;
    } else {
        mr.Type = MCPS_CONFIRMED;
        mr.Req.Confirmed.fPort = port;
        mr.Req.Confirmed.fBufferSize = length;
        mr.Req.Confirmed.fBuffer = buffer;
        mr.Req.Unconfirmed.Datarate = r.Param.ChannelsDatarate;
    }

    rc = LoRaMacMcpsRequest(&mr);
    if (rc != LORAMAC_STATUS_OK)
        log_debug("Transmission failed: %d", rc);

    return rc;
}


void lrw_process()
{
    system_allow_sleep(SYSTEM_MODULE_RADIO);
    if (Radio.IrqProcess != NULL) Radio.IrqProcess();
    LoRaMacProcess();
    save_state();
}


int lrw_isack_get(void)
{
    return tx_params.AckReceived;
}


// lrw_channel_list_t lrw_get_channel_list(void)
// {
//     lrw_channel_list_t result;

//     result.chmask_length = lrw_get_chmask_length();

//     GetPhyParams_t phy = {.Attribute = PHY_MAX_NB_CHANNELS};
//     PhyParam_t resp = RegionGetPhyParam(lrw_region_get(), &phy);
//     result.length = resp.Value;

//     req.Type = MIB_CHANNELS;
//     LoRaMacMibGetRequestConfirm(&req);
//     result.channels = req.Param.ChannelList;

//     req.Type = MIB_CHANNELS_MASK;
//     LoRaMacMibGetRequestConfirm(&req);
//     result.chmask = req.Param.ChannelsMask;

//     req.Type = MIB_CHANNELS_DEFAULT_MASK;
//     LoRaMacMibGetRequestConfirm(&req);
//     result.chmask_default = req.Param.ChannelsDefaultMask;

//     return result;
// }


// bool lrw_chmask_set(uint16_t chmask[LRW_CHMASK_LENGTH])
// {
//     memcpy(config->chmask, chmask, sizeof(config->chmask));

//     req.Type = MIB_CHANNELS_MASK;
//     req.Param.ChannelsMask = config->chmask;
//     if (LoRaMacMibSetRequestConfirm(&req) != LORAMAC_STATUS_OK)
//         return false;

//     req.Type = MIB_CHANNELS_DEFAULT_MASK;
//     req.Param.ChannelsDefaultMask = config->chmask;
//     return LoRaMacMibSetRequestConfirm(&req) == LORAMAC_STATUS_OK;
// }


LoRaMacNvmData_t *lrw_get_state()
{
    MibRequestConfirm_t r = { .Type = MIB_NVM_CTXS };
    LoRaMacMibGetRequestConfirm(&r);
    return r.Param.Contexts;
}


int lrw_join(void)
{
    MlmeReq_t mlme = { .Type = MLME_JOIN };

    MibRequestConfirm_t r = { .Type = MIB_NETWORK_ACTIVATION };
    LoRaMacMibGetRequestConfirm(&r);

    if (r.Param.NetworkActivation == ACTIVATION_TYPE_ABP) {
        // LoRaMac uses the same approach for both types of activation. In ABP
        // one still needs to invoke MLME_JOIN, although no actual Join will be
        // sent. The library will simply use the opportunity to perform internal
        // initialization.
        mlme.Req.Join.NetworkActivation = ACTIVATION_TYPE_ABP;
    } else {
        mlme.Req.Join.NetworkActivation = ACTIVATION_TYPE_OTAA;
        mlme.Req.Join.Datarate = DR_0;
    }
    return LoRaMacMlmeRequest(&mlme);
}


int lrw_set_region(unsigned int region)
{
    if (!RegionIsActive(region))
        return LORAMAC_STATUS_REGION_NOT_SUPPORTED;

    // Store the new region id in the NVM state in group MacGroup2
    LoRaMacNvmData_t *state = lrw_get_state();

    // Region did not change, nothing to do
    if (region == state->MacGroup2.Region) return -1;

    // The following function deactivates the MAC, the radio, and initializes
    // the MAC parameters to defaults.
    int rv = LoRaMacDeInitialization();
    if (rv != LORAMAC_STATUS_OK) return rv;

    // Reset all configuration parameters except the secure element. Note that
    // we intentionally do not recompute the CRC32 checksums here (except for
    // MacGroup2) since we don't want the state to be reloaded upon reboot. We
    // want the LoRaMac to initialize itself from defaults.
    memset(&state->Crypto, 0, sizeof(state->Crypto));
    memset(&state->MacGroup1, 0, sizeof(state->MacGroup1));
    memset(&state->MacGroup2, 0, sizeof(state->MacGroup2));
    memset(&state->RegionGroup1, 0, sizeof(state->RegionGroup1));
    memset(&state->RegionGroup2, 0, sizeof(state->RegionGroup2));
    memset(&state->ClassB, 0, sizeof(state->ClassB));

    // Update the region and regenerate the CRC for this block so that the
    // region will be picked up upon reboot.
    state->MacGroup2.Region = region;

    // We don't want to restore the entire MacGroup2 on the next reboot, but we
    // do want to restore the region parameter. Thus, calculate the CRC32 value
    // only over the region field and save it into the Crc32 parameter in the
    // structure. That way, the checksum will fail for the entire structure, but
    // the function that retrieves the region from it will additional check if
    // the checksum matches the region parameter and if yes, reload it.
    state->MacGroup2.Crc32 = Crc32(&state->MacGroup2.Region, sizeof(state->MacGroup2.Region));

    // Save all reset parameters in non-volatile memory.
    nvm_data_change(
        LORAMAC_NVM_NOTIFY_FLAG_CRYPTO        |
        LORAMAC_NVM_NOTIFY_FLAG_MAC_GROUP1    |
        LORAMAC_NVM_NOTIFY_FLAG_MAC_GROUP2    |
        LORAMAC_NVM_NOTIFY_FLAG_REGION_GROUP1 |
        LORAMAC_NVM_NOTIFY_FLAG_REGION_GROUP2 |
        LORAMAC_NVM_NOTIFY_FLAG_CLASS_B);

    return LORAMAC_STATUS_OK;
}


unsigned int lrw_get_mode(void)
{
    MibRequestConfirm_t r = { .Type = MIB_NETWORK_ACTIVATION };
    LoRaMacMibGetRequestConfirm(&r);

    switch(r.Param.NetworkActivation) {
        case ACTIVATION_TYPE_NONE: // If the value is None, we are in OTAA mode prior to Join
        case ACTIVATION_TYPE_OTAA:
            return 1;

        case ACTIVATION_TYPE_ABP:
        default:
            return 0;
    }
}


int lrw_set_mode(unsigned int mode)
{
    if (mode > 1) return LORAMAC_STATUS_PARAMETER_INVALID;

    MibRequestConfirm_t r = { .Type = MIB_NETWORK_ACTIVATION };
    LoRaMacMibGetRequestConfirm(&r);

    if (mode == 0) {
        // ABP mode. Invoke lrw_activate right away. No Join will be sent, but
        // the library will perform any necessary internal initialization.

        // If we are in ABP mode already, there is nothing to do
        if (r.Param.NetworkActivation != ACTIVATION_TYPE_ABP) {
            r.Type = MIB_NETWORK_ACTIVATION;
            r.Param.NetworkActivation = ACTIVATION_TYPE_ABP;
            LoRaMacMibSetRequestConfirm(&r);
            return lrw_join();
        }
    } else {
        if (r.Param.NetworkActivation != ACTIVATION_TYPE_OTAA) {
            // If we are in ABP mode or have no activation mode, set the mode to
            // none util a Join is executed.
            r.Param.NetworkActivation = ACTIVATION_TYPE_NONE;
            return LoRaMacMibSetRequestConfirm(&r);
        }
    }

    return LORAMAC_STATUS_OK;
}


void lrw_set_maxeirp(unsigned int maxeirp)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    state->MacGroup2.MacParams.MaxEirp = maxeirp;
    state->MacGroup2.Crc32 = Crc32((uint8_t *)&state->MacGroup2, sizeof(state->MacGroup2) - 4);
    nvm_data_change(LORAMAC_NVM_NOTIFY_FLAG_MAC_GROUP2);
}


int lrw_set_dwell(uint8_t uplink, uint8_t downlink)
{
    if (uplink > 1 || downlink > 1) return LORAMAC_STATUS_PARAMETER_INVALID;

    LoRaMacNvmData_t *state = lrw_get_state();
    state->MacGroup2.MacParams.UplinkDwellTime = uplink;
    state->MacGroup2.MacParams.DownlinkDwellTime = downlink;
    state->MacGroup2.Crc32 = Crc32((uint8_t *)&state->MacGroup2, sizeof(state->MacGroup2) - 4);
    nvm_data_change(LORAMAC_NVM_NOTIFY_FLAG_MAC_GROUP2);
    return 0;
}


int lrw_check_link(bool piggyback)
{
    LoRaMacStatus_t rc;
    MlmeReq_t mlr = { .Type = MLME_LINK_CHECK };

    rc = LoRaMacMlmeRequest(&mlr);
    if (rc != LORAMAC_STATUS_OK) {
        log_debug("Link check request failed: %d", rc);
        return rc;
    }

    if (!piggyback) {
        MibRequestConfirm_t mbr = { .Type = MIB_CHANNELS_DATARATE };
        LoRaMacMibGetRequestConfirm(&mbr);

        // Send an empty frame to piggy-back the link check operation on
        McpsReq_t mcr;
        memset(&mcr, 0, sizeof(mcr));
        mcr.Type = MCPS_UNCONFIRMED;
        mcr.Req.Unconfirmed.Datarate = mbr.Param.ChannelsDatarate;

        rc = LoRaMacMcpsRequest(&mcr);
        if (rc != LORAMAC_STATUS_OK)
            log_debug("Empty frame TX failed: %d", rc);
    }

    return rc;
}


int lrw_set_class(DeviceClass_t device_class)
{
    sysconf.device_class = device_class;
    sysconf_modified = true;
    return sync_device_class();
}


DeviceClass_t lrw_get_class(void)
{
    return sysconf.device_class;
}

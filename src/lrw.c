#include "lrw.h"
#include <LoRaWAN/Utilities/timeServer.h>
#include <LoRaWAN/Utilities/utilities.h>
#include <loramac-node/src/mac/LoRaMac.h>
#include <loramac-node/src/mac/LoRaMacTest.h>
#include <loramac-node/src/mac/region/Region.h>
#include "adc.h"
#include "cmd.h"
#include "system.h"
#include "halt.h"
#include "log.h"
#include "part.h"
#include "utils.h"

#define MAX_BAT 254


bool lrw_irq = false;

static McpsConfirm_t tx_params;
static McpsIndication_t rx_params;

// Remember activation mode in this variable. 0 means ABP, 1 means OTAA. This
// variables does not need to be saved in NVM.
unsigned int activation_mode = 0;


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
} nvm;


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


static void process_irq(void)
{
    lrw_irq = true;
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
        if (!part_write(&nvm.crypto, 0, &s->Crypto, sizeof(s->Crypto)))
            log_error("Error while writing Crypto state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_MAC_GROUP1) {
        log_debug("Saving MacGroup1 state to NVM");
        if (!part_write(&nvm.mac1, 0, &s->MacGroup1, sizeof(s->MacGroup1)))
            log_error("Error while writing MacGroup1 state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_MAC_GROUP2) {
        log_debug("Saving MacGroup2 state to NVM");
        if (!part_write(&nvm.mac2, 0, &s->MacGroup2, sizeof(s->MacGroup2)))
            log_error("Error while writing MacGroup2 state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_SECURE_ELEMENT) {
        log_debug("Saving SecureElement state to NVM");
        if (!part_write(&nvm.se, 0, &s->SecureElement, sizeof(s->SecureElement)))
            log_error("Error while writing SecureElement state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_REGION_GROUP1) {
        log_debug("Saving RegionGroup1 state to NVM");
        if (!part_write(&nvm.region1, 0, &s->RegionGroup1, sizeof(s->RegionGroup1)))
            log_error("Error while writing RegionGroup1 state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_REGION_GROUP2) {
        log_debug("Saving RegionGroup2 state to NVM");
        if (!part_write(&nvm.region2, 0, &s->RegionGroup2, sizeof(s->RegionGroup2)))
            log_error("Error while writing RegionGroup2 state to NVM");
    }

    if (nvm_flags & LORAMAC_NVM_NOTIFY_FLAG_CLASS_B) {
        log_debug("Saving ClassB state to NVM");
        if (!part_write(&nvm.classb, 0, &s->ClassB, sizeof(s->ClassB)))
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

    p = part_mmap(&size, &nvm.crypto);
    if (p && size >= sizeof(s.Crypto)) memcpy(&s.Crypto, p, size);

    p = part_mmap(&size, &nvm.mac1);
    if (p && size >= sizeof(s.MacGroup1)) memcpy(&s.MacGroup1, p, size);

    p = part_mmap(&size, &nvm.mac2);
    if (p && size >= sizeof(s.MacGroup2)) memcpy(&s.MacGroup2, p, size);

    p = part_mmap(&size, &nvm.se);
    if (p && size >= sizeof(s.SecureElement)) memcpy(&s.SecureElement, p, size);

    p = part_mmap(&size, &nvm.region1);
    if (p && size >= sizeof(s.RegionGroup1)) memcpy(&s.RegionGroup1, p, size);

    p = part_mmap(&size, &nvm.region2);
    if (p && size >= sizeof(s.RegionGroup2)) memcpy(&s.RegionGroup2, p, size);

    p = part_mmap(&size, &nvm.classb);
    if (p && size >= sizeof(s.ClassB)) memcpy(&s.ClassB, p, size);

    MibRequestConfirm_t r = {
        .Type = MIB_NVM_CTXS,
        .Param = { .Contexts = &s }
    };
    int rc = LoRaMacMibSetRequestConfirm(&r);
    if (rc != LORAMAC_STATUS_OK)
        log_error("LoRaMac: Error while restoring contexts: %d", rc);
}


static int restore_region()
{
    size_t size;
    LoRaMacRegion_t region;
    uint32_t crc;

    const LoRaMacNvmDataGroup2_t *p = part_mmap(&size, &nvm.mac2);
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
    atci_write((char *) buffer, length);
    // atci_print_buffer_as_hex(buffer, length);
}


static void mcps_confirm(McpsConfirm_t *param)
{
    log_debug("mcps_confirm: McpsRequest: %d, Channel: %ld AckReceived: %d", param->McpsRequest, param->Channel, param->AckReceived);
    tx_params = *param;
    on_ack(true);
}


static void mcps_indication(McpsIndication_t *param)
{
    log_debug("mcps_indication: status: %d rssi: %d", param->Status, param->Rssi);

    rx_params.Status = param->Status;

    if (rx_params.Status != LORAMAC_EVENT_INFO_STATUS_OK) {
        return;
    }

    if (param->RxData) {
        rx_params.RxDatarate = param->RxDatarate;
        rx_params.Rssi = param->Rssi;
        rx_params.Snr = param->Snr;
        rx_params.DownLinkCounter = param->DownLinkCounter;
        rx_params.RxSlot = param->RxSlot;

        recv(param->Port, param->Buffer, param->BufferSize);
    }

    if (param->IsUplinkTxPending == true) {
        // do nothing for now
    }
}


static void mlme_confirm(MlmeConfirm_t *param)
{
    log_debug("mlme_confirm: MlmeRequest: %d Status: %d", param->MlmeRequest, param->Status);

    tx_params.Status = param->Status;

    if (param->MlmeRequest == MLME_JOIN) {
        if (param->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
            // // Set class after join
            // req.Type = MIB_DEVICE_CLASS;
            // LoRaMacMibGetRequestConfirm(&req);
            // if (config->class != req.Param.Class) {
            //     req.Param.Class = config->class;
            //     LoRaMacMibSetRequestConfirm(&req);
            // }

            // overwrites the channel mask of the obtained connection to confirm
            // req.Type = MIB_CHANNELS_MASK;
            // req.Param.ChannelsMask = config->chmask;
            // LoRaMacMibSetRequestConfirm(&req);
            cmd_event(CMD_EVENT_JOIN, CMD_JOIN_SUCCEEDED);
        } else {
            cmd_event(CMD_EVENT_JOIN, CMD_JOIN_FAILED);
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
    rx_params.Status = param->Status;
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
    .MacProcessNotify    = process_irq
};


static void init_nvm(const part_block_t *nvm_block)
{
    if (part_find(&nvm.crypto, nvm_block, "crypto") &&
        part_create(&nvm.crypto, nvm_block, "crypto", sizeof(LoRaMacCryptoNvmData_t)))
        goto error;

    if (part_find(&nvm.mac1, nvm_block, "mac1") &&
        part_create(&nvm.mac1, nvm_block, "mac1", sizeof(LoRaMacNvmDataGroup1_t)))
        goto error;

    if (part_find(&nvm.mac2, nvm_block, "mac2") &&
        part_create(&nvm.mac2, nvm_block, "mac2", sizeof(LoRaMacNvmDataGroup2_t)))
        goto error;

    if (part_find(&nvm.se, nvm_block, "se") &&
        part_create(&nvm.se, nvm_block, "se", sizeof(SecureElementNvmData_t)))
        goto error;

    if (part_find(&nvm.region1, nvm_block, "region1") &&
        part_create(&nvm.region1, nvm_block, "region1", sizeof(RegionNvmDataGroup1_t)))
        goto error;

    if (part_find(&nvm.region2, nvm_block, "region2") &&
        part_create(&nvm.region2, nvm_block, "region2", sizeof(RegionNvmDataGroup2_t)))
        goto error;

    if (part_find(&nvm.classb, nvm_block, "classb") &&
        part_create(&nvm.classb, nvm_block, "classb", sizeof(LoRaMacClassBNvmData_t)))
        goto error;

    return;
error:
    halt("Could not initialize NVM");
}


void lrw_init(const part_block_t *nvm_block)
{
    static const uint8_t zero_eui[SE_EUI_SIZE];
    LoRaMacStatus_t rc;
    MibRequestConfirm_t r;

    memset(&tx_params, 0, sizeof(tx_params));
    memset(&rx_params, 0, sizeof(rx_params));

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

    r.Type = MIB_LORAWAN_VERSION;
    LoRaMacMibGetRequestConfirm(&r);
    uint32_t ver = r.Param.LrWanVersion.LoRaWan.Value;
    log_debug("LoRaMac: MAC version: %ld.%ld.%ld",
        ver >> 24, (ver >> 16) & 0xff, (ver >> 8) & 0xff);

#ifdef LORAMAC_ABP_VERSION
    r.Type = MIB_ABP_LORAWAN_VERSION;
    r.Param.AbpLrWanVersion.Value = LORAMAC_ABP_VERSION;
    rc = LoRaMacMibSetRequestConfirm(&r);
    if (rc != LORAMAC_STATUS_OK)
        log_error("LoRaMac: Error while setting LoRa MAC version for ABP: %d", rc);

    log_debug("LoRaMac: ABP MAC version: %ld.%ld.%ld",
        r.Param.AbpLrWanVersion.Value >> 24,
        (r.Param.AbpLrWanVersion.Value >> 16) & 0xff,
        (r.Param.AbpLrWanVersion.Value >> 8) & 0xff);
#endif

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

    r.Type = MIB_ADR;
    LoRaMacMibGetRequestConfirm(&r);
    const int adr = r.Param.AdrEnable;

    r.Type = MIB_PUBLIC_NETWORK;
    LoRaMacMibGetRequestConfirm(&r);
    const int public = r.Param.EnablePublicNetwork;

    r.Type = MIB_DEVICE_CLASS;
    LoRaMacMibGetRequestConfirm(&r);
    const int class = r.Param.Class;

    log_debug("LoRaMac: DevEUI: %02X%02X%02X%02X%02X%02X%02X%02X DevAddr: %08lX ADR: %d public: %d, class: %c",
        deveui[0], deveui[1], deveui[2], deveui[3],
        deveui[4], deveui[5], deveui[6], deveui[7],
        devaddr, adr, public, class + 'A');

    r.Type = MIB_SYSTEM_MAX_RX_ERROR;
    r.Param.SystemMaxRxError = 20;
    LoRaMacMibSetRequestConfirm(&r);
}


int lrw_send(uint8_t port, void *buffer, uint8_t length, bool confirmed)
{
    McpsReq_t mr;
    LoRaMacTxInfo_t txi;
    LoRaMacStatus_t rc;

    rc = LoRaMacQueryTxPossible(length, &txi);
    if (rc != LORAMAC_STATUS_OK) {
        // The payload is too long to fit into the message or there is some
        // other error.
        log_debug("Cannot transmit %d bytes", length);

        // Send an empty frame in order to flush MAC commands
        mr.Type = MCPS_UNCONFIRMED;
        mr.Req.Unconfirmed.fBuffer = NULL;
        mr.Req.Unconfirmed.fBufferSize = 0;
        //mr.Req.Unconfirmed.Datarate = lrw_tx_datarate_get();
        return -rc;
    }

    if (confirmed == false) {
        mr.Type = MCPS_UNCONFIRMED;
        mr.Req.Unconfirmed.fPort = port;
        mr.Req.Unconfirmed.fBufferSize = length;
        mr.Req.Unconfirmed.fBuffer = buffer;
        //mr.Req.Unconfirmed.Datarate = lrw_tx_datarate_get();
    } else {
        mr.Type = MCPS_CONFIRMED;
        mr.Req.Confirmed.fPort = port;
        mr.Req.Confirmed.fBufferSize = length;
        mr.Req.Confirmed.fBuffer = buffer;
        //mr.Req.Confirmed.Datarate = lrw_tx_datarate_get();
    }

    rc = LoRaMacMcpsRequest(&mr);
    if (rc != LORAMAC_STATUS_OK)
        log_debug("Transmission failed: %d", rc);

    return -rc;
}


void lrw_process()
{
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


int lrw_activate()
{
    int rc;
    MlmeReq_t mlme;

    mlme.Type = MLME_JOIN;
    mlme.Req.Join.Datarate = DR_0; // LoRaParamInit->tx_datarate;

    if (activation_mode == 1) {
        if (LoRaMacIsBusy()) return -LORAMAC_STATUS_BUSY;
        mlme.Req.Join.NetworkActivation = ACTIVATION_TYPE_OTAA;
    } else {
        mlme.Req.Join.NetworkActivation = ACTIVATION_TYPE_ABP;
    }

    rc = LoRaMacMlmeRequest(&mlme);
    if (rc != LORAMAC_STATUS_OK)
        log_error("LoRaMac: Activation failed: %d", rc);

    return -rc;
}


int lrw_set_region(unsigned int region)
{
    if (!RegionIsActive(region))
        return -LORAMAC_STATUS_REGION_NOT_SUPPORTED;

    // Store the new region id in the NVM state in group MacGroup2
    LoRaMacNvmData_t *state = lrw_get_state();

    // Region did not change, nothing to do
    if (region == state->MacGroup2.Region) return 1;

    // The following function deactivates the MAC, the radio, and initializes
    // the MAC parameters to defaults.
    int rv = LoRaMacDeInitialization();
    if (rv != LORAMAC_STATUS_OK) return -rv;

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
    return activation_mode;
}


int lrw_set_mode(unsigned int mode)
{
    if (mode > 1) return -1;

    activation_mode = mode;
    if (mode == 0) lrw_activate();
    return 0;
}

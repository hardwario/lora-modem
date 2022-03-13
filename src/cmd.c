#include "cmd.h"
#include <loramac-node/src/radio/radio.h>
#include <loramac-node/src/radio/sx1276/sx1276.h>
#include <loramac-node/src/mac/secure-element-nvm.h>
#include <loramac-node/src/mac/LoRaMacTest.h>
#include "lrw.h"
#include "system.h"
#include "config.h"
#include "gpio.h"
#include "log.h"
#include "rtc.h"
#include "nvm.h"
#include "console.h"
#include "halt.h"


typedef enum cmd_errno {
    ERR_UNKNOWN_CMD   =  -1,  // Unknown command
    ERR_PARAM_NO      =  -2,  // Invalid number of parameters
    ERR_PARAM         =  -3,  // Invalid parameter value(s)
    ERR_FACNEW_FAILED =  -4,  // Factory reset failed
    ERR_NO_JOIN       =  -5,  // Device has not joined LoRaWAN yet
    ERR_JOINED        =  -6,  // Device has already joined LoRaWAN
    ERR_BUSY          =  -7,  // Resource unavailable: LoRa MAC is transmitting
    ERR_VERSION       =  -8,  // New firmware version must be different
    ERR_MISSING_INFO  =  -9,  // Missing firmware information
    ERR_FLASH_ERROR   = -10,  // Flash read/write error
    ERR_UPDATE_FAILED = -11,  // Firmware update failed
    ERR_PAYLOAD_LONG  = -12,  // Payload is too long
    ERR_NO_ABP        = -13,  // Only supported in ABP activation mode
    ERR_NO_OTAA       = -14,  // Only supported in OTAA activation mode
    ERR_BAND          = -15,  // RF band is not supported
    ERR_POWER         = -16,  // Power value too high
    ERR_UNSUPPORTED   = -17,  // Not supported in the current band
    ERR_DUTYCYCLE     = -18,  // Cannot transmit due to duty cycling
    ERR_NO_CHANNEL    = -19,  // Channel unavailable due to LBT or error
    ERR_TOO_MANY      = -20   // Too many link check requests
} cmd_errno_t;


static uint8_t port;
static bool request_confirmation;

bool schedule_reset = false;


static void transmit(atci_param_t *param);


#define abort(num) do {                    \
    atci_printf("+ERR=%d\r\n\r\n", (num)); \
    return;                                \
} while (0)

#define EOL() atci_print("\r\n\n");

#define OK(...) do {                 \
    atci_printf("+OK=" __VA_ARGS__); \
    EOL();                           \
} while (0)

#define OK_() atci_print("+OK\r\n\r\n")


static inline uint32_t ntohl(uint32_t v)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return (v & 0xff) << 24 | (v & 0xff00) << 8 | (v & 0xff0000) >> 8 | (v & 0xff000000) >> 24;
#else
    return v;
#endif
}


#define abort_on_error(status) do { \
    int rc = status2error(status);  \
    if (rc < 0) abort(rc);          \
} while (0)


static int status2error(int status)
{
    if (status <= 0) return -status;
    switch ((status)) {
        case LORAMAC_STATUS_BUSY:                  return ERR_BUSY;         break;
        case LORAMAC_STATUS_SERVICE_UNKNOWN:       return ERR_UNKNOWN_CMD;  break;
        case LORAMAC_STATUS_NO_NETWORK_JOINED:     return ERR_NO_JOIN;      break;
        case LORAMAC_STATUS_DUTYCYCLE_RESTRICTED:  return ERR_DUTYCYCLE;    break;
        case LORAMAC_STATUS_REGION_NOT_SUPPORTED:  return ERR_BAND;         break;
        case LORAMAC_STATUS_FREQUENCY_INVALID:     return ERR_UNSUPPORTED;  break;
        case LORAMAC_STATUS_DATARATE_INVALID:      return ERR_UNSUPPORTED;  break;
        case LORAMAC_STATUS_FREQ_AND_DR_INVALID:   return ERR_UNSUPPORTED;  break;
        case LORAMAC_STATUS_LENGTH_ERROR:          return ERR_PAYLOAD_LONG; break;
        case LORAMAC_STATUS_NO_CHANNEL_FOUND:      return ERR_NO_CHANNEL;   break;
        case LORAMAC_STATUS_NO_FREE_CHANNEL_FOUND: return ERR_NO_CHANNEL;   break;
        default:                                   return ERR_PARAM;        break;
    }
}


static int parse_enabled(atci_param_t *param)
{
    if (param->offset >= param->length) return -1;
    if (param->length - param->offset != 1) return -1;

    switch (param->txt[param->offset++]) {
        case '0': return 0;
        case '1': return 1;
        default : return -1;
    }
}


static int parse_port(atci_param_t *param)
{
    uint32_t v;

    if (!atci_param_get_uint(param, &v))
        return -1;

    if (v < 1 || v > 223)
        return -1;

    return v;
}


static void get_uart(void)
{
    OK("%d,%d,%d,%d,%d", sysconf.uart_baudrate, 8, 1, 0, 0);
}


static void set_uart(atci_param_t *param)
{
    uint32_t v;
    if (!atci_param_get_uint(param, &v)) abort(ERR_PARAM);

    switch(v) {
        case 4800:  break;
        case 9600:  break;
        case 19200: break;
        case 38400: break;
        default: abort(ERR_PARAM);
    }

    sysconf.uart_baudrate = v;
    sysconf_modified = true;

    OK_();
}


// Backwards compatible implementation of AT+VER
static void get_version_comp(void)
{
    OK("%s,%s", VERSION_COMPAT, BUILD_DATE_COMPAT);
}


// AT$VER with more detailed firmware version and build time
static void get_version(void)
{
    OK("%s [LoRaMac %s],%s", VERSION, LIB_VERSION, BUILD_DATE);
}


static void get_model(void)
{
    OK("ABZ");
}


static void reboot(atci_param_t *param)
{
    (void)param;
    OK_();
    schedule_reset = true;
    console_flush();
}


static void factory_reset(atci_param_t *param)
{
    (void)param;

    if (LoRaMacStop() != LORAMAC_STATUS_OK)
        abort(ERR_FACNEW_FAILED);
    OK_();

    if (nvm_erase() == 0) {
        cmd_event(CMD_EVENT_MODULE, CMD_MODULE_FACNEW);
        schedule_reset = true;
        console_flush();
    }
}


static void get_band(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    OK("%d", state->MacGroup2.Region);
}


static void set_band(atci_param_t *param)
{
    uint32_t value;

    if (!atci_param_get_uint(param, &value))
        abort(ERR_PARAM);

    int rv = lrw_set_region(value);
    abort_on_error(rv);

    OK_();
    if (rv == 0) {
        // Emit a factory reset event since we have reset a significant portion
        // of the internal state (this is to match the original firmware which
        // does full factory reset on band change).
        cmd_event(CMD_EVENT_MODULE, CMD_MODULE_FACNEW);
        console_flush();
        schedule_reset = true;
    }
}


static void get_class(void)
{
    MibRequestConfirm_t r = { .Type = MIB_DEVICE_CLASS };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("%d", r.Param.Class);
}


static void set_class(atci_param_t *param)
{
    uint32_t v;
    if (!atci_param_get_uint(param, &v)) abort(ERR_PARAM);

    if (v > 2) abort(ERR_PARAM);

    MibRequestConfirm_t r = { .Type = MIB_DEVICE_CLASS };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    if (r.Param.Class == v) {
        OK_();
        return;
    }

    r.Param.Class = v;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_mode(void)
{
    OK("%d", lrw_get_mode());
}


static void set_mode(atci_param_t *param)
{
    uint32_t v;
    if (!atci_param_get_uint(param, &v)) abort(ERR_PARAM);
    if (v > 1) abort(ERR_PARAM);

    abort_on_error(lrw_set_mode(v));
    OK_();
}


static void get_devaddr(void)
{
    MibRequestConfirm_t r = { .Type = MIB_DEV_ADDR };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("%08lX", r.Param.DevAddr);
}


static void set_devaddr(atci_param_t *param)
{
    uint32_t buf;
    if (atci_param_get_buffer_from_hex(param, &buf, sizeof(buf)) != sizeof(buf))
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_DEV_ADDR,
        .Param = { .DevAddr = ntohl(buf) }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_deveui(void)
{
    MibRequestConfirm_t r = { .Type = MIB_DEV_EUI };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    atci_print("+OK=");
    atci_print_buffer_as_hex(r.Param.DevEui, SE_EUI_SIZE);
    EOL();
}


static void set_deveui(atci_param_t *param)
{
    uint8_t eui[SE_EUI_SIZE];
    if (atci_param_get_buffer_from_hex(param, eui, SE_EUI_SIZE) != SE_EUI_SIZE)
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_DEV_EUI,
        .Param = { .DevEui = eui }
    };

    abort_on_error(LoRaMacMibSetRequestConfirm(&r));
    OK_();
}


static void get_joineui(void)
{
    MibRequestConfirm_t r = { .Type = MIB_JOIN_EUI };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    atci_print("+OK=");
    atci_print_buffer_as_hex(r.Param.JoinEui, SE_EUI_SIZE);
    EOL();
}


static void set_joineui(atci_param_t *param)
{
    uint8_t eui[SE_EUI_SIZE];
    if (atci_param_get_buffer_from_hex(param, eui, SE_EUI_SIZE) != SE_EUI_SIZE)
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_JOIN_EUI,
        .Param = { .JoinEui = eui }
    };

    abort_on_error(LoRaMacMibSetRequestConfirm(&r));
    OK_();
}


static void get_nwkskey(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    atci_print("+OK=");
    atci_print_buffer_as_hex(&state->SecureElement.KeyList[NWK_S_ENC_KEY].KeyValue, SE_KEY_SIZE);
    EOL();
}


static void set_nwkskey(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE) != SE_KEY_SIZE)
        abort(ERR_PARAM);

    // Forwarding network session integrity key. This is the network session key for 1.0.x devices.
    MibRequestConfirm_t r = {
        .Type  = MIB_F_NWK_S_INT_KEY,
        .Param = { .FNwkSIntKey = key }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    // Service network session integrity key. This is not used in 1.0.x. Must be the same as the forwarding key above.
    r.Type  = MIB_S_NWK_S_INT_KEY;
    r.Param.SNwkSIntKey = key;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    // Network session encryption key. Not used by 1.0.x devices. Must be the same as the forwarding key above.
    r.Type  = MIB_NWK_S_ENC_KEY;
    r.Param.NwkSEncKey = key;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_appskey(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    atci_print("+OK=");
    atci_print_buffer_as_hex(&state->SecureElement.KeyList[APP_S_KEY].KeyValue, SE_KEY_SIZE);
    EOL();
}


static void set_appskey(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE) != SE_KEY_SIZE)
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_APP_S_KEY,
        .Param = { .AppSKey = key }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_appkey(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    atci_print("+OK=");
    atci_print_buffer_as_hex(&state->SecureElement.KeyList[APP_KEY].KeyValue, SE_KEY_SIZE);
    EOL();
}


static void set_appkey(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE) != SE_KEY_SIZE)
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_APP_KEY,
        .Param = { .AppKey = key }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_NWK_KEY;
    r.Param.NwkKey = key;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void join(atci_param_t *param)
{
    (void)param;

    abort_on_error(lrw_activate());

    OK_();
}


// static void get_joindc(void)
// {
//     abort(ERR_UNKNOWN_CMD);
// }


// static void set_joindc(atci_param_t *param)
// {
//     (void)param;
//     abort(ERR_UNKNOWN_CMD);
// }


static void lncheck(atci_param_t *param)
{
    int piggyback = 0;

    if (param != NULL) {
        piggyback = parse_enabled(param);
        if (piggyback == -1) abort(ERR_PARAM);
    }

    abort_on_error(lrw_check_link(piggyback == 1));
    OK_();
}


// static void get_rfparam(void)
// {
//     abort(ERR_UNKNOWN_CMD);
// }


// static void set_rfparam(atci_param_t *param)
// {
//     (void)param;
//     abort(ERR_UNKNOWN_CMD);
// }


static void get_rfpower(void)
{
    MibRequestConfirm_t r = { .Type  = MIB_CHANNELS_TX_POWER };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("%d", r.Param.ChannelsTxPower);
}


static void set_rfpower(atci_param_t *param)
{
    uint32_t val;
    if (!atci_param_get_uint(param, &val)) abort(ERR_PARAM);
    if (val > 15) abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_CHANNELS_TX_POWER,
        .Param = { .ChannelsTxPower = val }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));
    OK_();
}


static void get_nwk(void)
{
    MibRequestConfirm_t r = { .Type = MIB_PUBLIC_NETWORK };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("%d", r.Param.EnablePublicNetwork);
}


static void set_nwk(atci_param_t *param)
{
    int enabled = parse_enabled(param);
    if (enabled == -1) abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_PUBLIC_NETWORK,
        .Param = { .EnablePublicNetwork = enabled }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_adr(void)
{
    MibRequestConfirm_t r = { .Type = MIB_ADR };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("%d", r.Param.AdrEnable);
}


static void set_adr(atci_param_t *param)
{
    int enabled = parse_enabled(param);
    if (enabled == -1) abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_ADR,
        .Param = { .AdrEnable = enabled }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_dr(void)
{
    MibRequestConfirm_t r = { .Type = MIB_CHANNELS_DATARATE };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("%d", r.Param.ChannelsDatarate);
}


static void set_dr(atci_param_t *param)
{
    uint32_t val;
    if (!atci_param_get_uint(param, &val)) abort(ERR_PARAM);
    if (val > 15) abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_CHANNELS_DATARATE,
        .Param = { .ChannelsDatarate = val }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


// static void get_delay(void)
// {
//     abort(ERR_UNKNOWN_CMD);
// }


// static void set_delay(atci_param_t *param)
// {
//     (void)param;
//     abort(ERR_UNKNOWN_CMD);
// }


// static void get_adrack(void)
// {
//     abort(ERR_UNKNOWN_CMD);
// }


// static void set_adrack(atci_param_t *param)
// {
//     (void)param;
//     abort(ERR_UNKNOWN_CMD);
// }


static void get_rx2(void)
{
    MibRequestConfirm_t r = { .Type = MIB_RX2_CHANNEL };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));

    OK("%ld,%d", r.Param.Rx2Channel.Frequency, r.Param.Rx2Channel.Datarate);
}


static void set_rx2(atci_param_t *param)
{
    uint32_t freq, dr;

    if (!atci_param_get_uint(param, &freq)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);
    if (!atci_param_get_uint(param, &dr)) abort(ERR_PARAM);
    if (dr > 15) abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type = MIB_RX2_CHANNEL,
        .Param = {
            .Rx2Channel = {
                .Frequency = freq,
                .Datarate = dr
            }
        }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_dutycycle(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    OK("%d", state->MacGroup2.DutyCycleOn);
}


static void set_dutycycle(atci_param_t *param)
{
    int enabled = parse_enabled(param);
    if (enabled == -1) abort(ERR_PARAM);

    LoRaMacTestSetDutyCycleOn(enabled);
    OK_();
}


static void get_sleep(void)
{
    OK("%d", sysconf.sleep);
}


static void set_sleep(atci_param_t *param)
{
    uint32_t v;

    if (!atci_param_get_uint(param, &v))
        abort(ERR_PARAM);

    if (v > 1) abort(ERR_PARAM);

    sysconf.sleep = v;
    sysconf_modified = true;
    OK_();
}


static void get_port(void)
{
    OK("%d", sysconf.default_port);
}


static void set_port(atci_param_t *param)
{
    int p = parse_port(param);
    if (p < 0) abort(ERR_PARAM);

    sysconf.default_port = p;
    sysconf_modified = true;
    OK_();
}


static void get_rep(void)
{
    MibRequestConfirm_t r = { .Type = MIB_CHANNELS_NB_TRANS };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("%d", r.Param.ChannelsNbTrans);
}


static void set_rep(atci_param_t *param)
{
    uint32_t v;
    if (!atci_param_get_uint(param, &v)) abort(ERR_PARAM);

    if (v > 15) abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_CHANNELS_NB_TRANS,
        .Param = { .ChannelsNbTrans = v }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_dformat(void)
{
    OK("%d", sysconf.data_format);
}


static void set_dformat(atci_param_t *param)
{
    uint32_t v;

    if (!atci_param_get_uint(param, &v))
        abort(ERR_PARAM);

    if (v != 0 && v != 1)
        abort(ERR_PARAM);

    sysconf.data_format = v;
    sysconf_modified = true;

    OK_();
}


static void get_to(void)
{
    OK("%d", sysconf.uart_timeout);
}


static void set_to(atci_param_t *param)
{
    uint32_t v;

    if (!atci_param_get_uint(param, &v))
        abort(ERR_PARAM);

    if (v < 1 || v > 65535)
        abort(ERR_PARAM);

    sysconf.uart_timeout = v;
    sysconf_modified = true;

    OK_();
}


static void utx(atci_param_t *param)
{
    uint32_t size;
    port = sysconf.default_port;

    if (param == NULL) abort(ERR_PARAM);
    if (!atci_param_get_uint(param, &size))
        abort(ERR_PARAM);

    // The maximum payload size in LoRaWAN seems to be 242 bytes (US region) in
    // the most favorable conditions. If the payload is transmitted hex-encoded
    // by the client, we need to read twice as much data.

    unsigned int mul = sysconf.data_format == 1 ? 2 : 1;
    if (size > 242 * mul)
        abort(ERR_PAYLOAD_LONG);

    request_confirmation = false;
    if (!atci_set_read_next_data(size,
        sysconf.data_format == 1 ? ATCI_ENCODING_HEX : ATCI_ENCODING_BIN, transmit))
        abort(ERR_PAYLOAD_LONG);
}


static void ctx(atci_param_t *param)
{
    (void)param;
    utx(param);
    request_confirmation = true;
}


// static void get_mcast(void)
// {
//     abort(ERR_UNKNOWN_CMD);
// }


// static void set_mcast(atci_param_t *param)
// {
//     (void)param;
//     abort(ERR_UNKNOWN_CMD);
// }


static void transmit(atci_param_t *param)
{
    abort_on_error(lrw_send(port, param->txt, param->length, request_confirmation));
    OK_();
}


static void putx(atci_param_t *param)
{
    int p;

    if (param == NULL) abort(ERR_PARAM);
    p = parse_port(param);
    if (p < 0) abort(ERR_PARAM);

    if (!atci_param_is_comma(param))
        abort(ERR_PARAM);

    utx(param);
    port = p;
}


static void pctx(atci_param_t *param)
{
    putx(param);
    request_confirmation = true;
}


static void get_frmcnt(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    OK("%ld,%ld", state->Crypto.FCntList.FCntUp ,state->Crypto.FCntList.FCntDown);
}


// static void get_msize(void)
// {
//     abort(ERR_UNKNOWN_CMD);
// }


static void get_rfq(void)
{
    OK("%d,%d", lrw_rx_params.Rssi, lrw_rx_params.Snr);
}


static void get_dwell(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    OK("%d,%d", state->MacGroup2.MacParams.UplinkDwellTime,
        state->MacGroup2.MacParams.DownlinkDwellTime);
}


static void set_dwell(atci_param_t *param)
{
    uint8_t uplink, downlink;

    switch (param->txt[param->offset++]) {
        case '0': uplink = 0; break;
        case '1': uplink = 1; break;
        default : abort(ERR_PARAM);
    }

    if (!atci_param_is_comma(param))
        abort(ERR_PARAM);

    switch (param->txt[param->offset++]) {
        case '0': downlink = 0; break;
        case '1': downlink = 1; break;
        default : abort(ERR_PARAM);
    }

    abort_on_error(lrw_set_dwell(uplink, downlink));
    OK_();
}


static void get_maxeirp(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    OK("%.0f", state->MacGroup2.MacParams.MaxEirp);
}


static void set_maxeirp(atci_param_t *param)
{
    uint32_t val;

    if (!atci_param_get_uint(param, &val))
        abort(ERR_PARAM);

    lrw_set_maxeirp(val);
    OK_();
}


// static void get_rssith(void)
// {
//     abort(ERR_UNKNOWN_CMD);
// }


// static void set_rssith(atci_param_t *param)
// {
//     (void)param;
//     abort(ERR_UNKNOWN_CMD);
// }


// static void get_cst(void)
// {
//     abort(ERR_UNKNOWN_CMD);
// }


// static void set_cst(atci_param_t *param)
// {
//     (void)param;
//     abort(ERR_UNKNOWN_CMD);
// }


// static void get_backoff(void)
// {
//     abort(ERR_UNKNOWN_CMD);
// }


// static void get_chmask(void)
// {
// }


// static void set_chmask(atci_param_t *param)
// {
//     uint16_t chmask[LRW_CHMASK_LENGTH];
//     memset(chmask, 0, sizeof(chmask));

//     size_t length = atci_param_get_buffer_from_hex(param, chmask, sizeof(chmask));

//     if (length == 0)
//     {
//         abort(ERR_PARAM);
//         return;
//     }

//     if (lrw_chmask_set(chmask))
//     {
//         config_save();
//         OK_();
//     }
//     else
//     {
//         abort(ERR_PARAM);
//     }
// }


static void get_rtynum(void)
{
    get_rep();
}


static void set_rtynum(atci_param_t *param)
{
    set_rep(param);
}


static void get_netid(void)
{
    MibRequestConfirm_t r = { .Type = MIB_NET_ID };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("%08lX", r.Param.NetID);
}


static void set_netid(atci_param_t *param)
{
    uint32_t buf;
    if (atci_param_get_buffer_from_hex(param, &buf, sizeof(buf)) != sizeof(buf))
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_NET_ID,
        .Param = { .NetID = ntohl(buf) }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


// static void get_channels(void)
// {
//     lrw_channel_list_t list = lrw_get_channel_list();

//     // log_debug("%d %d", list.length, list.chmask_length);
//     // log_dump(list.chmask, list.chmask_length * 2, "masks");
//     // log_dump(list.chmask_default, list.chmask_length * 2, "default_mask");

//     for (uint8_t i = 0; i < list.length; i++)
//     {
//         if (list.channels[i].Frequency == 0)
//             continue;

//         uint8_t is_enable = (i / 16) < list.chmask_length ? (list.chmask[i / 16] >> (i % 16)) & 0x01 : 0;

//         atci_printf("$CHANNELS: %d,%d,%d,%d,%d,%d\r\n",
//                     is_enable,
//                     list.channels[i].Frequency,
//                     list.channels[i].Rx1Frequency,
//                     list.channels[i].DrRange.Fields.Min,
//                     list.channels[i].DrRange.Fields.Max,
//                     list.channels[i].Band);
//     }
//     OK_();
// }


static void dbg(atci_param_t *param)
{
    (void)param;
    // RF_IDLE = 0,   //!< The radio is idle
    // RF_RX_RUNNING, //!< The radio is in reception state
    // RF_TX_RUNNING, //!< The radio is in transmission state
    // RF_CAD,        //!< The radio is doing channel activity detection
    atci_printf("$DBG: \"stop_mode_mask\",%d\r\n", system_get_stop_mode_mask());
    atci_printf("$DBG: \"radio_state\",%d\r\n", Radio.GetStatus());
    OK_();
}


static void ping(atci_param_t *param)
{
    int confirm = 0;
    if (param != NULL) {
        confirm = parse_enabled(param);
        if (confirm == -1) abort(ERR_PARAM);
    }

    abort_on_error(lrw_send(sysconf.default_port, "ping", 4, confirm == 1));
    OK_();
}


static void activated(void)
{
    MibRequestConfirm_t r = { .Type = MIB_NETWORK_ACTIVATION };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("%d", r.Param.NetworkActivation);
}


static void do_halt(atci_param_t *param)
{
    (void)param;
    OK_();
    console_flush();

    halt(NULL);
}


static const atci_command_t cmds[] = {
    {"+UART",      NULL,          set_uart,      get_uart,         NULL, "Configure UART interface"},
    {"+VER",       NULL,          NULL,          get_version_comp, NULL, "Firmware version and build time"},
    {"+DEV",       NULL,          NULL,          get_model,        NULL, "Device model"},
    {"+REBOOT",    reboot,        NULL,          NULL,             NULL, "Reboot"},
    {"+FACNEW",    factory_reset, NULL,          NULL,             NULL, "Restore modem to factory"},
    {"+BAND",      NULL,          set_band,      get_band,         NULL, "Configure radio band (region)"},
    {"+CLASS",     NULL,          set_class,     get_class,        NULL, "Configure LoRaWAN class"},
    {"+MODE",      NULL,          set_mode,      get_mode,         NULL, "Configure activation mode (1:OTTA 0:ABP)"},
    {"+DEVADDR",   NULL,          set_devaddr,   get_devaddr,      NULL, "Configure DevAddr"},
    {"+DEVEUI",    NULL,          set_deveui,    get_deveui,       NULL, "Configure DevEUI"},
    {"+APPEUI",    NULL,          set_joineui,   get_joineui,      NULL, "Configure JoinEUI (AppEUI)"},
    {"+NWKSKEY",   NULL,          set_nwkskey,   get_nwkskey,      NULL, "Configure NwkSKey"},
    {"+APPSKEY",   NULL,          set_appskey,   get_appskey,      NULL, "Configure AppSKey"},
    {"+APPKEY",    NULL,          set_appkey,    get_appkey,       NULL, "Configure AppKey"},
    {"+JOIN",      join,          NULL,          NULL,             NULL, "Send OTAA Join packet"},
    // {"+JOINDC",    NULL,          set_joindc,    get_joindc,       NULL, "Configure OTAA Join duty cycling"},
    {"+LNCHECK",   lncheck,       lncheck,       NULL,             NULL, "Perform link check"},
    // {"+RFPARAM",   NULL,          set_rfparam,   get_rfparam,      NULL, "Configure RF channel parameters"},
    {"+RFPOWER",   NULL,          set_rfpower,   get_rfpower,      NULL, "Configure RF power"},
    {"+NWK",       NULL,          set_nwk,       get_nwk,          NULL, "Configure public/private LoRa network setting"},
    {"+ADR",       NULL,          set_adr,       get_adr,          NULL, "Configure adaptive data rate (ADR)"},
    {"+DR",        NULL,          set_dr,        get_dr,           NULL, "Configure data rate (DR)"},
    // {"+DELAY",     NULL,          set_delay,     get_delay,        NULL, "Configure receive window offsets"},
    // {"+ADRACK",    NULL,          set_adrack,    get_adrack,       NULL, "Configure ADR ACK parameters"},
    {"+RX2",       NULL,          set_rx2,       get_rx2,          NULL, "Configure RX2 window frequency and data rate"},
    {"+DUTYCYCLE", NULL,          set_dutycycle, get_dutycycle,    NULL, "Configure duty cycling in EU868"},
    {"+SLEEP",     NULL,          set_sleep,     get_sleep,        NULL, "Configure low power (sleep) mode"},
    {"+PORT",      NULL,          set_port,      get_port,         NULL, "Configure default port number for uplink messages <1,223>"},
    {"+REP",       NULL,          set_rep,       get_rep,          NULL, "Unconfirmed message repeats [1..15]"},
    {"+DFORMAT",   NULL,          set_dformat,   get_dformat,      NULL, "Configure payload format used by the modem"},
    {"+TO",        NULL,          set_to,        get_to,           NULL, "Configure UART port timeout"},
    {"+UTX",       utx,           NULL,          NULL,             NULL, "Send unconfirmed uplink message"},
    {"+CTX",       ctx,           NULL,          NULL,             NULL, "Send confirmed uplink message"},
    // {"+MCAST",     NULL,          set_mcast,     get_mcast,        NULL, "Configure multicast addresses"},
    {"+PUTX",      putx,          NULL,          NULL,             NULL, "Send unconfirmed uplink message to port"},
    {"+PCTX",      pctx,          NULL,          NULL,             NULL, "Send confirmed uplink message to port"},
    {"+FRMCNT",    NULL,          NULL,          get_frmcnt,       NULL, "Return current values for uplink and downlink counters"},
    // {"+MSIZE",     NULL,          NULL,          get_msize,        NULL, "Return maximum payload size for current data rate"},
    {"+RFQ",       NULL,          NULL,          get_rfq,          NULL, "Return RSSI and SNR of the last received message"},
    {"+DWELL",     NULL,          set_dwell,     get_dwell,        NULL, "Configure dwell setting for AS923"},
    {"+MAXEIRP",   NULL,          set_maxeirp,   get_maxeirp,      NULL, "Configure maximum EIRP"},
    // {"+RSSITH",    NULL,          set_rssith,    get_rssith,       NULL, "Configure RSSI threshold for LBT"},
    // {"+CST",       NULL,          set_cst,       get_cst,          NULL, "Configure carrier sensor time (CST) for LBT"},
    // {"+BACKOFF",   NULL,          NULL,          get_backoff,      NULL, "Return duty cycle backoff time for EU868"},
    // {"+CHMASK",    NULL,          set_chmask,    get_chmask,       NULL, "Configure channel mask"},
    {"+RTYNUM",    NULL,          set_rtynum,    get_rtynum,       NULL, "Configure number of confirmed uplink message retries"},
    {"+NETID",     NULL,          set_netid,     get_netid,        NULL, "Configure LoRaWAN network identifier"},
    // {"$CHANNELS",  NULL,          NULL,          get_channels,     NULL, ""},
    {"$VER",       NULL,          NULL,          get_version,      NULL, "Firmware version and build time"},
    {"$DBG",       dbg,           NULL,          NULL,             NULL, ""},
    {"$PING",      ping,          NULL,          NULL,             NULL, "Send ping message"},
    {"$ACTIVATED", NULL,          NULL,          activated,        NULL, "Returns network activation status (0: not activate, >0: activated"},
    {"$HALT",      do_halt,       NULL,          NULL,             NULL, "Halt the modem"},
    ATCI_COMMAND_CLAC,
    ATCI_COMMAND_HELP};


void cmd_init(unsigned int baudrate)
{
    atci_init(baudrate, cmds, ATCI_COMMANDS_LENGTH(cmds));
}


void cmd_event(unsigned int type, unsigned int subtype)
{
    atci_printf("+EVENT=%d,%d\r\n\r\n", type, subtype);
}


void cmd_ans(unsigned int margin, unsigned int gwcnt)
{
    atci_printf("+ANS=2,%d,%d\r\n\r\n", margin, gwcnt);
}

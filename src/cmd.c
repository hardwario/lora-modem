#include "cmd.h"
#include <string.h>
#include <loramac-node/src/radio/radio.h>
#include <loramac-node/src/mac/secure-element.h>
#include <loramac-node/src/mac/secure-element-nvm.h>
#include <loramac-node/src/mac/LoRaMacTest.h>
#include <loramac-node/src/mac/LoRaMacCrypto.h>
#include <LoRaWAN/Utilities/timeServer.h>
#include "lrw.h"
#include "system.h"
#include "gpio.h"
#include "log.h"
#include "rtc.h"
#include "nvm.h"
#include "halt.h"
#include "utils.h"
#include "sx1276-board.h"

// These are global variables exported by radio.c that store the RSSI and SNR of
// the most recent received packet.
extern int16_t radio_rssi;
extern int8_t radio_snr;


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
    ERR_TOO_MANY      = -20,  // Too many link check requests
    ERR_ACCESS_DENIED = -50,  // Read access to security keys is denied
    ERR_DETACH_DENIED = -51   // The re-attach GPIO is active
} cmd_errno_t;


static uint8_t port;
static bool request_confirmation;
static TimerEvent_t payload_timer;

bool schedule_reset = false;

#if DETACHABLE_LPUART == 1

static Gpio_t attach_pin = {
    .port     = GPIOB,
    .pinIndex = GPIO_PIN_12
};

#endif

#define abort(num) do {                     \
    atci_printf("+ERR=%d" ATCI_EOL, (num)); \
    return;                                 \
} while (0)

#define EOL() atci_print(ATCI_EOL);

#define OK(...) do {                 \
    atci_printf("+OK=" __VA_ARGS__); \
    EOL();                           \
} while (0)

#define OK_() atci_print(ATCI_OK)


static inline uint32_t ntohl(uint32_t v)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return (v & 0xff) << 24 | (v & 0xff00) << 8 | (v & 0xff0000) >> 8 | (v & 0xff000000) >> 24;
#else
    return v;
#endif
}


#define abort_on_error(status) do {  \
    int __rc = status2error(status); \
    if (__rc < 0) abort(__rc);       \
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


/*
 * Use this function to parse a single argument that must be either 0 or 1. Note
 * that if the AT command accepts multiple arguments separated by commas, this
 * function cannot be used.
 */
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


static uint8_t *find_key(KeyIdentifier_t id)
{
    LoRaMacNvmData_t *state = lrw_get_state();

    for (int i = 0; i < NUM_OF_KEYS; i++) {
        if (state->SecureElement.KeyList[i].KeyID == id)
            return state->SecureElement.KeyList[i].KeyValue;
    }
    return NULL;
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
    char *mode;

#if defined(DEBUG)
    mode = "debug";
#elif defined(RELEASE)
    mode = "release";
#else
    mode = "?";
#endif

    OK("%s,%s,%s,%d.%d.%d,%d.%d.%d,%d.%d.%d,RP%03d-%d.%d.%d,%s,%s", VERSION, BUILD_DATE, LIB_VERSION,
        LORAMAC_VERSION >> 24, (LORAMAC_VERSION >> 16) & 0xff, (LORAMAC_VERSION >> 8) & 0xff,
        LORAMAC_FALLBACK_VERSION >> 24, (LORAMAC_FALLBACK_VERSION >> 16) & 0xff, (LORAMAC_FALLBACK_VERSION >> 8) & 0xff,
        LORAMAC_ABP_VERSION >> 24, (LORAMAC_ABP_VERSION >> 16) & 0xff, (LORAMAC_ABP_VERSION >> 8) & 0xff,
        REGION_VERSION >> 24, (REGION_VERSION >> 16) & 0xff,
        (REGION_VERSION >> 8) & 0xff, REGION_VERSION & 0xff,
        ENABLED_REGIONS,
        mode);
}


static void get_model(void)
{
    OK("ABZ");
}


static void reboot(atci_param_t *param)
{
    int hard = 0;

    if (param != NULL) {
        hard = parse_enabled(param);
        if (hard == -1) abort(ERR_PARAM);
    }

    if (hard) {
        NVIC_SystemReset();
    } else {
        OK_();
        schedule_reset = true;
        atci_flush();
    }
}


static void facnew(atci_param_t *param)
{
    uint32_t flags = 0;

    if (param != NULL) {
        if (!atci_param_get_uint(param, &flags))
            abort(ERR_PARAM);

        if (param->offset != param->length)
            abort(ERR_PARAM_NO);
    }

    // Function lrw_factory_reset performs a lengthy operation whose status is
    // not immediately known. Hence, the function returns void. To find out
    // whether the reset has been successfully performed, the caller can observe
    // the arrival of +EVENT=0,1 prior to the arrival of +EVENT=0,0. The
    // function always performs a reboot at the end (even if factory reset
    // fails), however, +EVENT=1,0 is only sent if factory reset succeeded.

    // The OK below indicates to the caller that the factory reset operation has
    // been successfully started, i.e., all parameters are correct and the MAC
    // was successfully stopped.
    if (LoRaMacStop() != LORAMAC_STATUS_OK)
        abort(ERR_FACNEW_FAILED);
    OK_();

#define RESET_DEVNONCE(flags) (((flags) & (1 << 0)) != 0)
#define RESET_DEVEUI(flags) (((flags) & (1 << 1)) != 0)

    lrw_factory_reset(RESET_DEVNONCE(flags), RESET_DEVEUI(flags));
}


static void get_band(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    OK("%d", state->MacGroup2.Region);
}


static void set_band(atci_param_t *param)
{
    uint32_t value;

    if (!atci_param_get_uint(param, &value)) abort(ERR_PARAM);
    if (value > 9) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    int rv = lrw_set_region(value);
    abort_on_error(rv);

    OK_();
    if (rv == 0) {
        // Emit a factory reset event since we have reset a significant portion
        // of the internal state (this is to match the original firmware which
        // does full factory reset on band change).
        cmd_event(CMD_EVENT_MODULE, CMD_MODULE_FACNEW);
        atci_flush();
        schedule_reset = true;
    }
}


static void get_class(void)
{
    OK("%d", lrw_get_class());
}


static void set_class(atci_param_t *param)
{
    uint32_t v;
    if (!atci_param_get_uint(param, &v)) abort(ERR_PARAM);

    // In original firmware compatiblity mode, only class A (0) and class C (2)
    // can be configured with this command.
    if (v != 0 && v != 2) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    abort_on_error(lrw_set_class(v));
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

    if (param->offset != param->length) abort(ERR_PARAM_NO);

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
    if (atci_param_get_buffer_from_hex(param, &buf, sizeof(buf), 0) != sizeof(buf))
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
    if (atci_param_get_buffer_from_hex(param, eui, SE_EUI_SIZE, 0) != SE_EUI_SIZE)
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
    if (atci_param_get_buffer_from_hex(param, eui, SE_EUI_SIZE, 0) != SE_EUI_SIZE)
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
    if (sysconf.lock_keys) abort(ERR_ACCESS_DENIED);

    atci_print("+OK=");

    // We operate in a backwards-compatible 1.0 mode here and in that mode, the
    // various network session keys are the same and the canonical version is in
    // FNwkSIntKey.

    atci_print_buffer_as_hex(find_key(F_NWK_S_INT_KEY), SE_KEY_SIZE);
    EOL();
}


static void set_nwkskey(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE, 0) != SE_KEY_SIZE)
        abort(ERR_PARAM);

    // We implement a mode compatible with the original Type ABZ firmware which
    // only supports LoRaWAN 1.0. Thus, we need to operate in a LoRaWAN 1.0
    // backwards-compatible mode here. In this mode, the NwkSKey becomes
    // FNwkSIntKey (forwarding network session integrity key). The other two
    // network keys required by our 1.1 implementation are set to the same
    // value.

    // Forwarding network session integrity key. This is the network session key
    // for 1.0.x devices.
    MibRequestConfirm_t r = {
        .Type  = MIB_F_NWK_S_INT_KEY,
        .Param = { .FNwkSIntKey = key }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    // Service network session integrity key. This is not used in 1.0.x. Must be
    // the same as the forwarding key above.
    r.Type  = MIB_S_NWK_S_INT_KEY;
    r.Param.SNwkSIntKey = key;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    // Network session encryption key. Not used by 1.0.x devices. Must be the
    // same as the forwarding key above.
    r.Type  = MIB_NWK_S_ENC_KEY;
    r.Param.NwkSEncKey = key;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_appskey(void)
{
    if (sysconf.lock_keys) abort(ERR_ACCESS_DENIED);

    atci_print("+OK=");
    atci_print_buffer_as_hex(find_key(APP_S_KEY), SE_KEY_SIZE);
    EOL();
}


static void set_appskey(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE, 0) != SE_KEY_SIZE)
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
    if (sysconf.lock_keys) abort(ERR_ACCESS_DENIED);

    atci_print("+OK=");
    atci_print_buffer_as_hex(find_key(APP_KEY), SE_KEY_SIZE);
    EOL();
}


static void set_appkey_10(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE, 0) != SE_KEY_SIZE)
        abort(ERR_PARAM);

    // The original firmware supports LoRaWAN 1.0 and does not provide an AT
    // command to set the other root key (NwkKey). Hence, we must assume that we
    // will be operating in the backwards-compatible single root key scheme
    // documented in LoRaWAN 1.1 Section 6.1.1.3. In that scheme, AppSKey is
    // derived from NwkKey and not from AppKey. Thus, we need to set the value
    // configured here to both AppKey and NwkKey.

    MibRequestConfirm_t r = {
        .Type  = MIB_NWK_KEY,
        .Param = { .NwkKey = key }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_APP_KEY;
    r.Param.AppKey = key;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void set_appkey_11(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE, 0) != SE_KEY_SIZE)
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_APP_KEY,
        .Param = { .AppKey = key }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));
    OK_();
}


static void join(atci_param_t *param)
{
    uint32_t datarate = DR_0;

    // If we are not in OTAA mode, abort with -14 just like the original Murata
    // Modem firmware.
    if (lrw_get_mode() == 0) abort(ERR_NO_OTAA);

    // Configure the default number of OTAA Join transmissions to nine. In
    // regions that use all 64 channels (such as US915), this is the number of
    // retransmissions that is needed for the Join retransmissions to cycle
    // through all eight-channel sub-bands, plus one extra transmission for the
    // 500 kHz sub-band.
    uint32_t tries = 9;

    if (param != NULL) {
        if (!atci_param_get_uint(param, &datarate)) abort(ERR_PARAM);
        if (datarate > 15) abort(ERR_PARAM);

        if (param->offset != param->length) {
            if (!atci_param_is_comma(param)) abort(ERR_PARAM);

            if (!atci_param_get_uint(param, &tries)) abort(ERR_PARAM);
            if (tries < 1 || tries > 16) abort(ERR_PARAM);

            if (param->offset != param->length) abort(ERR_PARAM_NO);
        }
    }

    abort_on_error(lrw_join(datarate, tries));
    OK_();
}


static void get_joindc(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    OK("%d", state->MacGroup2.JoinDutyCycleOn);
}


static void set_joindc(atci_param_t *param)
{
    int enabled = parse_enabled(param);
    if (enabled == -1) abort(ERR_PARAM);

    LoRaMacTestSetJoinDutyCycleOn(enabled);
    OK_();
}


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


static void get_rfparam(void)
{
    ChannelParams_t *c;
    LoRaMacNvmData_t *state = lrw_get_state();
    GetPhyParams_t pr = { .Attribute = PHY_MAX_NB_CHANNELS };
    unsigned nb_channels = RegionGetPhyParam(state->MacGroup2.Region, &pr).Value;

    MibRequestConfirm_t r = { .Type = MIB_CHANNELS };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));

    // Count the number of channels that have non-zero frequency
    unsigned n = 0;
    for (unsigned i = 0; i < nb_channels; i++)
        if (r.Param.ChannelList[i].Frequency != 0) n++;

    atci_printf("+OK=%d", n);
    for (unsigned i = 0; i < nb_channels; i++) {
        c = r.Param.ChannelList + i;
        if (c->Frequency == 0) continue;
        atci_printf(";%d,%ld,%d,%d", i, c->Frequency, c->DrRange.Fields.Min, c->DrRange.Fields.Max);
    }
    EOL();
}


static void set_rfparam(atci_param_t *param)
{
    LoRaMacStatus_t rc;
    uint32_t id, freq, min_dr, max_dr;

    if (!atci_param_get_uint(param, &id)) abort(ERR_PARAM);
    if (id > UINT8_MAX) abort(ERR_PARAM);

    if (param->offset < param->length) {
        if (!atci_param_is_comma(param)) abort(ERR_PARAM);

        if (!atci_param_get_uint(param, &freq)) abort(ERR_PARAM);
        if (!atci_param_is_comma(param)) abort(ERR_PARAM);

        if (!atci_param_get_uint(param, &min_dr)) abort(ERR_PARAM);
        if (min_dr > INT8_MAX) abort(ERR_PARAM);
        if (!atci_param_is_comma(param)) abort(ERR_PARAM);

        if (!atci_param_get_uint(param, &max_dr)) abort(ERR_PARAM);
        if (max_dr > INT8_MAX) abort(ERR_PARAM);

        if (param->offset != param->length) abort(ERR_PARAM_NO);

        ChannelParams_t params = { .Frequency = freq };
        params.DrRange.Fields.Min = min_dr;
        params.DrRange.Fields.Max = max_dr;

        rc = LoRaMacChannelAdd(id, params);
    } else {
        rc = LoRaMacChannelRemove(id);
    }

    abort_on_error(rc);
    OK_();
}


// A version compatible with the original Type ABZ firmware
static void get_rfpower_comp(void)
{
    MibRequestConfirm_t r = { .Type  = MIB_CHANNELS_TX_POWER };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("0,%d", r.Param.ChannelsTxPower);
}


// A version compatible with the original Type ABZ firmware
static void set_rfpower_comp(atci_param_t *param)
{
    uint32_t paboost, val;

    if (!atci_param_get_uint(param, &paboost)) abort(ERR_PARAM);
    if (paboost != 0 && paboost != 1)
        abort(ERR_PARAM);

    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &val)) abort(ERR_PARAM);
    if (val > 15) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    MibRequestConfirm_t r = {
        .Type  = MIB_CHANNELS_DEFAULT_TX_POWER,
        .Param = { .ChannelsDefaultTxPower = val }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_CHANNELS_TX_POWER;
    r.Param.ChannelsTxPower = val;
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


// A version compatible with the original Type ABZ firmware
static void get_dr_comp(void)
{
    MibRequestConfirm_t r = { .Type = MIB_CHANNELS_DATARATE };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    OK("%d", r.Param.ChannelsDatarate);
}


// A version compatible with the original Type ABZ firmware
static void set_dr_comp(atci_param_t *param)
{
    uint32_t val;
    if (!atci_param_get_uint(param, &val)) abort(ERR_PARAM);
    if (val > 15) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    MibRequestConfirm_t r = {
        .Type  = MIB_CHANNELS_DEFAULT_DATARATE,
        .Param = { .ChannelsDefaultDatarate = val }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_CHANNELS_DATARATE;
    r.Param.ChannelsDatarate = val;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_delay(void)
{
    MibRequestConfirm_t r;

    r.Type = MIB_JOIN_ACCEPT_DELAY_1;
    LoRaMacMibGetRequestConfirm(&r);
    int join1 = r.Param.JoinAcceptDelay1;

    r.Type = MIB_JOIN_ACCEPT_DELAY_2;
    LoRaMacMibGetRequestConfirm(&r);
    int join2 = r.Param.JoinAcceptDelay2;

    r.Type = MIB_RECEIVE_DELAY_1;
    LoRaMacMibGetRequestConfirm(&r);
    int rx1 = r.Param.ReceiveDelay1;

    r.Type = MIB_RECEIVE_DELAY_2;
    LoRaMacMibGetRequestConfirm(&r);
    int rx2 = r.Param.ReceiveDelay2;

    OK("%d,%d,%d,%d", join1, join2, rx1, rx2);
}


static void set_delay(atci_param_t *param)
{
    MibRequestConfirm_t r;
    uint32_t join1, join2, rx1, rx2;

    if (!atci_param_get_uint(param, &join1)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);
    if (!atci_param_get_uint(param, &join2)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);
    if (!atci_param_get_uint(param, &rx1)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);
    if (!atci_param_get_uint(param, &rx2)) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    r.Type = MIB_JOIN_ACCEPT_DELAY_1;
    r.Param.JoinAcceptDelay1 = join1;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_JOIN_ACCEPT_DELAY_2;
    r.Param.JoinAcceptDelay2 = join2;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_RECEIVE_DELAY_1;
    r.Param.ReceiveDelay1 = rx1;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_RECEIVE_DELAY_2;
    r.Param.ReceiveDelay2 = rx2;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_adrack(void)
{
    uint16_t limit;
    MibRequestConfirm_t r;

    r.Type = MIB_ADR_ACK_LIMIT;
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    limit = r.Param.AdrAckLimit;

    r.Type = MIB_ADR_ACK_DELAY;
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));

    OK("%d,%d", limit, r.Param.AdrAckDelay);
}


static void set_adrack(atci_param_t *param)
{
    uint32_t limit, delay;
    if (!atci_param_get_uint(param, &limit)) abort(ERR_PARAM);
    if (limit > UINT16_MAX) abort(ERR_PARAM);

    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &delay)) abort(ERR_PARAM);
    if (delay > UINT16_MAX) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    MibRequestConfirm_t r = { .Type = MIB_ADR_ACK_LIMIT };
    r.Param.AdrAckLimit = limit;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_ADR_ACK_DEFAULT_LIMIT;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_ADR_ACK_DELAY;
    r.Param.AdrAckDelay = delay;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_ADR_ACK_DEFAULT_DELAY;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


// A version compatible with the original Type ABZ firmware
static void get_rx2_comp(void)
{
    MibRequestConfirm_t r = { .Type = MIB_RX2_CHANNEL };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));

    OK("%ld,%d", r.Param.Rx2Channel.Frequency, r.Param.Rx2Channel.Datarate);
}


// A version compatible with the original Type ABZ firmware
static void set_rx2_comp(atci_param_t *param)
{
    uint32_t freq, dr;

    if (!atci_param_get_uint(param, &freq)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);
    if (!atci_param_get_uint(param, &dr)) abort(ERR_PARAM);
    if (dr > 15) abort(ERR_PARAM);
    if (param->offset != param->length) abort(ERR_PARAM_NO);

    MibRequestConfirm_t r = {
        .Type = MIB_RX2_DEFAULT_CHANNEL,
        .Param = {
            .Rx2DefaultChannel = {
                .Frequency = freq,
                .Datarate = dr
            }
        }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_RX2_CHANNEL;
    r.Param.Rx2Channel.Frequency = freq;
    r.Param.Rx2Channel.Datarate = dr;
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

    if (!atci_param_get_uint(param, &v)) abort(ERR_PARAM);
    if (v > 1) abort(ERR_PARAM);
    if (param->offset != param->length) abort(ERR_PARAM_NO);

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
    if (param->offset != param->length) abort(ERR_PARAM_NO);

    sysconf.default_port = p;
    sysconf_modified = true;
    OK_();
}


static void get_rep(void)
{
    OK("%d", sysconf.unconfirmed_retransmissions);
}


static void set_rep(atci_param_t *param)
{
    uint32_t v;
    if (!atci_param_get_uint(param, &v)) abort(ERR_PARAM);
    if (v < 1 || v > 15) abort(ERR_PARAM);
    if (param->offset != param->length) abort(ERR_PARAM_NO);

    sysconf.unconfirmed_retransmissions = v;
    sysconf_modified = true;
    OK_();
}


static void get_dformat(void)
{
    OK("%d", sysconf.data_format);
}


static void set_dformat(atci_param_t *param)
{
    uint32_t v;

    if (!atci_param_get_uint(param, &v)) abort(ERR_PARAM);
    if (v != 0 && v != 1) abort(ERR_PARAM);
    if (param->offset != param->length) abort(ERR_PARAM_NO);

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

    if (!atci_param_get_uint(param, &v)) abort(ERR_PARAM);
    if (v < 1 || v > 65535) abort(ERR_PARAM);
    if (param->offset != param->length) abort(ERR_PARAM_NO);

    sysconf.uart_timeout = v;
    sysconf_modified = true;

    OK_();
}


static void payload_timeout(void *ctx)
{
    (void)ctx;
    log_debug("Payload reader timed out after %ld ms", payload_timer.ReloadValue);
    atci_abort_read_next_data();
}


static void transmit(atci_data_status_t status, atci_param_t *param)
{
    TimerStop(&payload_timer);

    if (status == ATCI_DATA_ENCODING_ERROR)
        abort(ERR_PARAM);

    // The original Type ABZ firmware returns an OK if payload submission times
    // out and sends an incomplete message, i.e., it sends whatever had been
    // received before the timer fired. Hence, we do not check for
    // ATCI_DATA_ABORTED here.

    if (port != 0 && param->length == 0) {
        // LoRaMAC cannot reliably send a message with an empty payload to a
        // non-zero port number. If the library has any MAC commands waiting to
        // be piggy-backed, it would internally change the port number of the
        // message to zero in order to be able to stuff the MAC commands into
        // the payload. Hence, a message with an empty payload is not guaranteed
        // to be sent to the correct port number and may not be received by the
        // application server. Thus, we don't support empty payloads and require
        // that the application provides at least one byte if port is not 0.
        abort(ERR_PARAM);
    }

    abort_on_error(lrw_send(port, param->txt, param->length, request_confirmation));
    OK_();
}


static void utx(atci_param_t *param)
{
    uint32_t size;
    port = sysconf.default_port;

    if (param == NULL) abort(ERR_PARAM_NO);
    if (!atci_param_get_uint(param, &size)) abort(ERR_PARAM);

    // The maximum payload size in LoRaWAN seems to be 242 bytes (US region) in
    // the most favorable conditions. If the payload is transmitted hex-encoded
    // by the client, we need to read twice as much data.
    unsigned int mul = sysconf.data_format == 1 ? 2 : 1;
    if (size > 242 * mul) abort(ERR_PAYLOAD_LONG);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    TimerInit(&payload_timer, payload_timeout);
    TimerSetValue(&payload_timer, sysconf.uart_timeout);
    TimerStart(&payload_timer);

    request_confirmation = false;
    if (!atci_set_read_next_data(size,
        sysconf.data_format == 1 ? ATCI_ENCODING_HEX : ATCI_ENCODING_BIN, transmit))
        abort(ERR_PAYLOAD_LONG);
}


static void ctx(atci_param_t *param)
{
    utx(param);
    request_confirmation = true;
}


static void get_mcast(void)
{
    McChannelParams_t *c;
    LoRaMacNvmData_t *state = lrw_get_state();
    int n = 0;
    KeyIdentifier_t keys[LORAMAC_MAX_MC_CTX * 2] = {
        MC_NWK_S_KEY_0, MC_APP_S_KEY_0,
        MC_NWK_S_KEY_1, MC_APP_S_KEY_1,
        MC_NWK_S_KEY_2, MC_APP_S_KEY_2,
        MC_NWK_S_KEY_3, MC_APP_S_KEY_3
    };

    for (int i = 0; i < LORAMAC_MAX_MC_CTX; i++) {
        c = &state->MacGroup2.MulticastChannelList[i].ChannelParams;
        if (c->IsEnabled) n++;
    }

    atci_printf("+OK=%d", n);
    for (int i = 0; i < LORAMAC_MAX_MC_CTX; i++) {
        c = &state->MacGroup2.MulticastChannelList[i].ChannelParams;
        if (!c->IsEnabled) continue;

        atci_printf(";%d,%08lX,", c->GroupID, c->Address);
        atci_print_buffer_as_hex(find_key(keys[2 * i]), SE_KEY_SIZE);
        atci_print(",");
        atci_print_buffer_as_hex(find_key(keys[2 * i + 1]), SE_KEY_SIZE);
    }
    EOL();
}


static void set_mcast(atci_param_t *param)
{
    uint32_t id, addr;
    LoRaMacStatus_t rc;
    uint8_t nwkskey[SE_KEY_SIZE];
    uint8_t appskey[SE_KEY_SIZE];

    if (!atci_param_get_uint(param, &id)) abort(ERR_PARAM);
    if (id >= LORAMAC_MAX_MC_CTX) abort(ERR_PARAM);

    if (param->offset < param->length) {
        if (!atci_param_is_comma(param)) abort(ERR_PARAM);

        if (atci_param_get_buffer_from_hex(param, &addr, sizeof(addr), sizeof(addr) * 2) != sizeof(addr))
            abort(ERR_PARAM);

        if (!atci_param_is_comma(param)) abort(ERR_PARAM);

        if (atci_param_get_buffer_from_hex(param, nwkskey, SE_KEY_SIZE, SE_KEY_SIZE * 2) != SE_KEY_SIZE)
            abort(ERR_PARAM);

        if (!atci_param_is_comma(param)) abort(ERR_PARAM);

        if (atci_param_get_buffer_from_hex(param, appskey, SE_KEY_SIZE, SE_KEY_SIZE * 2) != SE_KEY_SIZE)
            abort(ERR_PARAM);

        if (param->offset != param->length) abort(ERR_PARAM_NO);

        McChannelParams_t c = {
            .IsEnabled = true,
            .IsRemotelySetup = false,
            .GroupID = id,
            .Address = ntohl(addr),
            .McKeys = {
                .Session = {
                    .McNwkSKey = nwkskey,
                    .McAppSKey = appskey
                }
            },
            .FCountMin = 0,
            .FCountMax = UINT32_MAX
        };

        LoRaMacMcChannelDelete(id);
        rc = LoRaMacMcChannelSetup(&c);

    } else {
        rc = LoRaMacMcChannelDelete(id);
    }

    abort_on_error(rc);
    OK_();
}


static void putx(atci_param_t *param)
{
    if (param == NULL) abort(ERR_PARAM_NO);
    int p = parse_port(param);
    if (p < 0) abort(ERR_PARAM);

    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    utx(param);
    port = p;
}


static void pctx(atci_param_t *param)
{
    putx(param);
    request_confirmation = true;
}


static void cw(atci_param_t *param)
{
    uint32_t freq, timeout;
    int32_t power;

    if (!atci_param_get_uint(param, &freq)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_int(param, &power)) abort(ERR_PARAM);
    if (power < INT8_MIN || power > INT8_MAX) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &timeout)) abort(ERR_PARAM);
    if (timeout > UINT16_MAX) abort(ERR_PARAM);

    // Make sure there are no additional parameters that we don't understand.
    if (param->offset != param->length) abort(ERR_PARAM_NO);

    log_debug("$CW: freq=%ld Hz power=%ld dBm timeout=%ld s", freq, power, timeout);

    // We could have invoked Radio.SetTxContinuousWave directly here. The
    // benefit of invoking the function via the MIB is that it forces the MAC
    // into a LORAMAC_TX_RUNNING state for the duration of the transmission,
    // which will prevent other transmission attempts from disrupting the
    // ongoing carrier wave transmission. There appears to be no other way to
    // transition into that state manually.

    MlmeReq_t r = {
        .Type = MLME_TXCW,
        .Req = { .TxCw = {
            .Timeout = timeout,
            .Frequency = freq,
            .Power = power
    }}};
    abort_on_error(lrw_mlme_request(&r));

    lrw_event_subtype = CMD_CERT_CW_ENDED;

    // Continuous carrier wave transmission internally reconfigures some of the
    // SX1276 DIO pins and interrupts. Rather than trying to restore everything
    // to a functioning state, we automatically perform a modem reset after the
    // transmission has ended. This command is for certification purposes only,
    // so this behavior is fine.
    schedule_reset = true;
    OK_();
}


static void cm_clk_irq_handler(void* context)
{
    (void) context;
    static uint32_t i;

    // AT$CM generates a continuous stream of ones and zeros modulated with FSK.
    // This interrupt handler is invoked on the falling edge of the clock signal
    // generated on DIO1. It alternates the state of DIO2 in order to generate
    // the sequence of ones and zeroes.
    GpioWrite(&SX1276.DIO2, (i++ % 2) == 0);
}


static void cm(atci_param_t *param)
{
    // Example with 868.3 MHz center frequency, 250 kHz deviation, 4800 Bd data
    // rate, transmission power -10 dBm, and 2 second timeout:
    // AT$CM 868300000,250000,4800,-10,2
    uint32_t freq, timeout, fdev, datarate;
    int32_t power;

    if (!atci_param_get_uint(param, &freq)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &fdev)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &datarate)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_int(param, &power)) abort(ERR_PARAM);
    if (power < INT8_MIN || power > INT8_MAX) abort(ERR_PARAM);

    if (!atci_param_is_comma(param)) abort(ERR_PARAM);
    if (!atci_param_get_uint(param, &timeout)) abort(ERR_PARAM);
    if (timeout > UINT16_MAX) abort(ERR_PARAM);

    // Make sure there are no additional parameters that we don't understand.
    if (param->offset != param->length) abort(ERR_PARAM_NO);

    log_debug("$CM: freq=%ld Hz fdev=%ld Hz datarate=%ld Bd power=%ld dBm timeout=%ld s",
        freq, fdev, datarate, power, timeout);

    // Rewire SX1276 interrupt handlers. We disable everything but the interrupt
    // handler on DIO1, which points to our continuous mode interrupt handler.
    DioIrqHandler *irq[] = { NULL, cm_clk_irq_handler, NULL, NULL, NULL, NULL };
    SX1276IoIrqInit(irq);

    // Invoke the continuous carrier wave MIB request. This is the same
    // operation that AT$CW performs. We technically don't need to invoke this
    // command here since the SetTxConfig command invoked below resets most of
    // the settings performed by TXCW and stops the radio again. We primarily
    // invoke the MIB command here to move the MAC into LORAMAC_TX_RUNNING
    // state. This solution is a bit hackish, but LoRaMac-node does not seem to
    // provide any other API.

    MlmeReq_t r = {
        .Type = MLME_TXCW,
        .Req = { .TxCw = {
            .Timeout = timeout,
            .Frequency = freq,
            .Power = power
    }}};
    abort_on_error(lrw_mlme_request(&r));
    schedule_reset = true;

    timeout *= 1000;

    // Configure the radio in FSK mode with the selected transmission power, FSK
    // deviation, and data rate. We provide 0x5 as a dummy preamble, but since
    // we're operating in the continuous mode here, the preamble won't be used
    // (it's only used in the packet mode). Internally, SetTXConfig switches the
    // radio into the packet mode and puts on stand by.
    Radio.SetTxConfig(MODEM_FSK, power, fdev, 0, datarate, 0, 5, false, false, 0, 0, 0, timeout);

    // Since SetTxConfig internally forces the radio into packet mode, we need
    // to switch to the continous mode here.
    SX1276Write(REG_PACKETCONFIG2, SX1276Read(REG_PACKETCONFIG2) & RF_PACKETCONFIG2_DATAMODE_MASK);

    // Disable DIO0, enable modulator clock on DIO1
    SX1276Write(REG_DIOMAPPING1, RF_DIOMAPPING1_DIO0_11 | RF_DIOMAPPING1_DIO1_00);

    // Generate falling edge interrupts on DIO1. We have configured the radio to
    // generate a FSK modulator clock on this pin. The radio samples on the
    // rising edge which means that we can modify the state of DIO2 on the
    // falling edge.
    GPIO_InitTypeDef dio1 = {
        .Mode = GPIO_MODE_IT_FALLING,
        .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_HIGH
    };
    gpio_init(SX1276.DIO1.port, SX1276.DIO1.pinIndex, &dio1);

    // Configure DIO2 GPIO as output
    GPIO_InitTypeDef dio2 = {
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_HIGH
    };
    gpio_init(SX1276.DIO2.port, SX1276.DIO2.pinIndex, &dio2);

    // Radio.SetTxConfig we call above puts the modem into a standby mode again
    // and resets the TX timeout timer. Thus, we need to invoke SX1276SetTx here
    // to start transmitting and to reset the TX timeout timer.
    SX1276SetTx(timeout);

    lrw_event_subtype = CMD_CERT_CM_ENDED;

    // Reboot the modem once the transmission has finished. Since the AT$CW and
    // AT$CM commands are primarily for certification, we don't bother restoring
    // DIO port configuration and interrupt handlers and instead force the modem
    // to reboot.
    schedule_reset = true;
    OK_();
}


static void get_frmcnt(void)
{
    uint32_t down;
    LoRaMacNvmData_t *state = lrw_get_state();

    MibRequestConfirm_t r = { .Type = MIB_LORAWAN_VERSION };
    LoRaMacMibGetRequestConfirm(&r);

    if (r.Param.LrWanVersion.LoRaWan.Fields.Minor == 0)
        down = state->Crypto.FCntList.FCntDown;
    else
        down = state->Crypto.FCntList.AFCntDown;

    // For compatiblity with the original firmware, return 0 if the downlink
    // counter still has the initial value (no downlink was received yet).
    OK("%lu,%lu", state->Crypto.FCntList.FCntUp,
        down == FCNT_DOWN_INITIAL_VALUE ? 0 : down);
}


static void get_msize(void)
{
    LoRaMacTxInfo_t txi;
    LoRaMacStatus_t rc = LoRaMacQueryTxPossible(0, &txi);
    switch(rc) {
        case LORAMAC_STATUS_OK:
            OK("%d", txi.MaxPossibleApplicationDataSize);
            break;

        case LORAMAC_STATUS_LENGTH_ERROR:
            OK("%d", 0);
            break;

        default:
            abort_on_error(rc);
            break;
    }
}


static void get_rfq(void)
{
    OK("%d,%d", radio_rssi, radio_snr);
}


static void get_dwell(void)
{
    LoRaMacNvmData_t *state = lrw_get_state();
    OK("%d,%d", state->MacGroup2.MacParams.UplinkDwellTime,
        state->MacGroup2.MacParams.DownlinkDwellTime);
}


static void set_dwell(atci_param_t *param)
{
    bool uplink, downlink;

    switch (param->txt[param->offset++]) {
        case '0': uplink = false; break;
        case '1': uplink = true; break;
        default : abort(ERR_PARAM);
    }

    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    switch (param->txt[param->offset++]) {
        case '0': downlink = false; break;
        case '1': downlink = true; break;
        default : abort(ERR_PARAM);
    }

    if (param->offset != param->length) abort(ERR_PARAM_NO);

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

    if (!atci_param_get_uint(param, &val)) abort(ERR_PARAM);
    if (param->offset != param->length) abort(ERR_PARAM_NO);

    lrw_set_maxeirp(val);
    OK_();
}


static void get_rssith(void)
{
    MibRequestConfirm_t r = { .Type = MIB_RSSI_FREE_THRESHOLD };

    LoRaMacStatus_t rc = LoRaMacMibGetRequestConfirm(&r);
    if (rc == LORAMAC_STATUS_ERROR) abort(ERR_UNSUPPORTED);
    abort_on_error(rc);

    OK("%d", r.Param.RssiFreeThreshold);
}


static void set_rssith(atci_param_t *param)
{
    int32_t rssi;

    if (!atci_param_get_int(param, &rssi)) abort(ERR_PARAM);
    if (rssi < INT16_MIN || rssi > INT16_MAX) abort(ERR_PARAM);
    if (param->offset != param->length) abort(ERR_PARAM_NO);

    MibRequestConfirm_t r = { .Type = MIB_RSSI_FREE_THRESHOLD };
    r.Param.RssiFreeThreshold = rssi;

    LoRaMacStatus_t rc = LoRaMacMibSetRequestConfirm(&r);
    if (rc == LORAMAC_STATUS_ERROR) abort(ERR_UNSUPPORTED);
    abort_on_error(rc);

    OK_();
}


static void get_cst(void)
{
    MibRequestConfirm_t r = { .Type = MIB_CARRIER_SENSE_TIME };

    LoRaMacStatus_t rc = LoRaMacMibGetRequestConfirm(&r);
    if (rc == LORAMAC_STATUS_ERROR) abort(ERR_UNSUPPORTED);
    abort_on_error(rc);

    OK("%lu", r.Param.CarrierSenseTime);
}


static void set_cst(atci_param_t *param)
{
    uint32_t cst;

    if (!atci_param_get_uint(param, &cst)) abort(ERR_PARAM);
    if (param->offset != param->length) abort(ERR_PARAM_NO);

    MibRequestConfirm_t r = { .Type = MIB_CARRIER_SENSE_TIME };
    r.Param.CarrierSenseTime = cst;

    LoRaMacStatus_t rc = LoRaMacMibSetRequestConfirm(&r);
    if (rc == LORAMAC_STATUS_ERROR) abort(ERR_UNSUPPORTED);
    abort_on_error(rc);

    OK_();
}


static void get_backoff(void)
{
    TimerTime_t now = rtc_tick2ms(rtc_get_timer_value());
    OK("%ld", lrw_dutycycle_deadline > now ? lrw_dutycycle_deadline - now : 0);
}


// A version compatible with the original Type ABZ firmware
static void get_chmask_comp(void)
{
    MibRequestConfirm_t r = { .Type = MIB_CHANNELS_MASK };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));
    atci_print("+OK=");
    atci_print_buffer_as_hex(r.Param.ChannelsMask, lrw_get_max_channels() / 8);
    EOL();
}


static bool parse_chmask(uint16_t *buf, size_t len, atci_param_t *param)
{
    int chmask_bytes = lrw_get_max_channels() / 8;

    memset(buf, 0, len);
    int read = atci_param_get_buffer_from_hex(param, buf, len, chmask_bytes * 2);

    if (read != chmask_bytes) return false;
    return true;
}


// A version compatible with the original Type ABZ firmware
static void set_chmask_comp(atci_param_t *param)
{
    uint16_t chmask[REGION_NVM_CHANNELS_MASK_SIZE];

    if (!parse_chmask(chmask, sizeof(chmask), param)) abort(ERR_PARAM);

    // Make sure all data from the value have been consumed
    if (param->length != param->offset) abort(ERR_PARAM_NO);

    // First set the default channel mask. The default channel mask is the
    // channel mask used before Join or ADR.
    MibRequestConfirm_t r = {
        .Type  = MIB_CHANNELS_DEFAULT_MASK,
        .Param = { .ChannelsDefaultMask = chmask }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    // Then update the channel mask currently in use
    r.Type = MIB_CHANNELS_MASK;
    r.Param.ChannelsMask = chmask;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_rtynum(void)
{
    OK("%d", sysconf.confirmed_retransmissions);
}


static void set_rtynum(atci_param_t *param)
{
    uint32_t v;
    if (!atci_param_get_uint(param, &v)) abort(ERR_PARAM);
    if (v < 1 || v > 15) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    sysconf.confirmed_retransmissions = v;
    sysconf_modified = true;
    OK_();
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
    if (atci_param_get_buffer_from_hex(param, &buf, sizeof(buf), 0) != sizeof(buf))
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_NET_ID,
        .Param = { .NetID = ntohl(buf) }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


#if DEBUG_LOG != 0
static void dbg(atci_param_t *param)
{
    (void)param;
    // RF_IDLE = 0,   //!< The radio is idle
    // RF_RX_RUNNING, //!< The radio is in reception state
    // RF_TX_RUNNING, //!< The radio is in transmission state
    // RF_CAD,        //!< The radio is doing channel activity detection
    atci_printf("sleep_lock=%d stop_lock=%d radio_state=%d loramac_busy=%d\r\n",
        system_sleep_lock, system_stop_lock, Radio.GetStatus(), LoRaMacIsBusy());

    OK_();
}
#endif

static void do_halt(atci_param_t *param)
{
    (void)param;
    OK_();
    atci_flush();

    halt(NULL);
}


static void get_nwkkey(void)
{
    if (sysconf.lock_keys) abort(ERR_ACCESS_DENIED);

    atci_print("+OK=");
    atci_print_buffer_as_hex(find_key(NWK_KEY), SE_KEY_SIZE);
    EOL();
}


static void set_nwkkey(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE, 0) != SE_KEY_SIZE)
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_NWK_KEY,
        .Param = { .NwkKey = key }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_fnwksintkey(void)
{
    if (sysconf.lock_keys) abort(ERR_ACCESS_DENIED);

    atci_print("+OK=");
    atci_print_buffer_as_hex(find_key(F_NWK_S_INT_KEY), SE_KEY_SIZE);
    EOL();
}


static void set_fnwksintkey(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE, 0) != SE_KEY_SIZE)
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_F_NWK_S_INT_KEY,
        .Param = { .FNwkSIntKey = key }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_snwksintkey(void)
{
    if (sysconf.lock_keys) abort(ERR_ACCESS_DENIED);

    atci_print("+OK=");
    atci_print_buffer_as_hex(find_key(S_NWK_S_INT_KEY), SE_KEY_SIZE);
    EOL();
}


static void set_snwksintkey(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE, 0) != SE_KEY_SIZE)
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_S_NWK_S_INT_KEY,
        .Param = { .SNwkSIntKey = key }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_nwksenckey(void)
{
    if (sysconf.lock_keys) abort(ERR_ACCESS_DENIED);

    atci_print("+OK=");
    atci_print_buffer_as_hex(find_key(NWK_S_ENC_KEY), SE_KEY_SIZE);
    EOL();
}


static void set_nwksenckey(atci_param_t *param)
{
    uint8_t key[SE_KEY_SIZE];

    if (atci_param_get_buffer_from_hex(param, key, SE_KEY_SIZE, 0) != SE_KEY_SIZE)
        abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_NWK_S_ENC_KEY,
        .Param = { .NwkSEncKey = key }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_chmask(void)
{
    atci_print("+OK=");

    MibRequestConfirm_t r = { .Type = MIB_CHANNELS_MASK };
    LoRaMacMibGetRequestConfirm(&r);
    atci_print_buffer_as_hex(r.Param.ChannelsMask, lrw_get_max_channels() / 8);

    atci_print(",");

    r.Type = MIB_CHANNELS_DEFAULT_MASK;
    LoRaMacMibGetRequestConfirm(&r);
    atci_print_buffer_as_hex(r.Param.ChannelsDefaultMask, lrw_get_max_channels() / 8);

    EOL();
}


static void set_chmask(atci_param_t *param)
{
    uint16_t chmask1[REGION_NVM_CHANNELS_MASK_SIZE];
    uint16_t chmask2[REGION_NVM_CHANNELS_MASK_SIZE];

    if (!parse_chmask(chmask1, sizeof(chmask1), param)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);
    if (!parse_chmask(chmask2, sizeof(chmask2), param)) abort(ERR_PARAM);

    // Make sure all data from the value have been consumed
    if (param->length != param->offset) abort(ERR_PARAM_NO);

    MibRequestConfirm_t r = {
        .Type  = MIB_CHANNELS_DEFAULT_MASK,
        .Param = { .ChannelsDefaultMask = chmask2 }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    // Then update the channel mask currently in use
    r.Type = MIB_CHANNELS_MASK;
    r.Param.ChannelsMask = chmask1;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_rx2(void)
{
    MibRequestConfirm_t r1 = { .Type = MIB_RX2_CHANNEL };
    LoRaMacMibGetRequestConfirm(&r1);

    MibRequestConfirm_t r2 = { .Type = MIB_RX2_DEFAULT_CHANNEL };
    LoRaMacMibGetRequestConfirm(&r2);

    OK("%ld,%d,%ld,%d", r1.Param.Rx2Channel.Frequency, r1.Param.Rx2Channel.Datarate,
        r2.Param.Rx2DefaultChannel.Frequency, r2.Param.Rx2DefaultChannel.Datarate);
}


static void set_rx2(atci_param_t *param)
{
    uint32_t freq1, dr1, freq2, dr2;

    if (!atci_param_get_uint(param, &freq1)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);
    if (!atci_param_get_uint(param, &dr1)) abort(ERR_PARAM);

    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &freq2)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM);
    if (!atci_param_get_uint(param, &dr2)) abort(ERR_PARAM);

    if (dr1 > 15 || dr2 > 15) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    MibRequestConfirm_t r = {
        .Type = MIB_RX2_DEFAULT_CHANNEL,
        .Param = {
            .Rx2DefaultChannel = {
                .Frequency = freq2,
                .Datarate = dr2
            }
        }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_RX2_CHANNEL;
    r.Param.Rx2Channel.Frequency = freq1;
    r.Param.Rx2Channel.Datarate = dr1;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_dr(void)
{
    MibRequestConfirm_t r1 = { .Type = MIB_CHANNELS_DATARATE };
    LoRaMacMibGetRequestConfirm(&r1);

    MibRequestConfirm_t r2 = { .Type = MIB_CHANNELS_DEFAULT_DATARATE };
    LoRaMacMibGetRequestConfirm(&r2);

    OK("%d,%d", r1.Param.ChannelsDatarate, r2.Param.ChannelsDefaultDatarate);
}


static void set_dr(atci_param_t *param)
{
    uint32_t val1, val2;

    if (!atci_param_get_uint(param, &val1)) abort(ERR_PARAM);
    if (val1 > 15) abort(ERR_PARAM);

    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &val2)) abort(ERR_PARAM);
    if (val2 > 15) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    MibRequestConfirm_t r = {
        .Type  = MIB_CHANNELS_DEFAULT_DATARATE,
        .Param = { .ChannelsDefaultDatarate = val2 }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_CHANNELS_DATARATE;
    r.Param.ChannelsDatarate = val1;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}


static void get_rfpower(void)
{
    MibRequestConfirm_t r1 = { .Type  = MIB_CHANNELS_TX_POWER };
    LoRaMacMibGetRequestConfirm(&r1);
    MibRequestConfirm_t r2 = { .Type  = MIB_CHANNELS_DEFAULT_TX_POWER };
    LoRaMacMibGetRequestConfirm(&r2);
    OK("0,%d,0,%d", r1.Param.ChannelsTxPower, r2.Param.ChannelsDefaultTxPower);
}


static void set_rfpower(atci_param_t *param)
{
    uint32_t paboost1, paboost2, val1, val2;

    if (!atci_param_get_uint(param, &paboost1)) abort(ERR_PARAM);
    if (paboost1 != 0) {
        log_warning("PA boost currently unsupported");
        abort(ERR_PARAM);
    }

    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &val1)) abort(ERR_PARAM);
    if (val1 > 15) abort(ERR_PARAM);

    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &paboost2)) abort(ERR_PARAM);
    if (paboost2 != 0) {
        log_warning("PA boost currently unsupported");
        abort(ERR_PARAM);
    }

    if (!atci_param_is_comma(param)) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &val2)) abort(ERR_PARAM);
    if (val2 > 15) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    MibRequestConfirm_t r = {
        .Type  = MIB_CHANNELS_DEFAULT_TX_POWER,
        .Param = { .ChannelsDefaultTxPower = val2 }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    r.Type = MIB_CHANNELS_TX_POWER;
    r.Param.ChannelsTxPower = val1;
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));

    OK_();
}

#if DEBUG_LOG != 0
static void get_loglevel(void)
{
    OK("%d", log_get_level());
}


static void set_loglevel(atci_param_t *param)
{
    (void)param;
    uint32_t level;

    if (!atci_param_get_uint(param, &level))
        abort(ERR_PARAM);

    if (level > 5) abort(ERR_PARAM);

    if (param->offset != param->length) abort(ERR_PARAM_NO);

    log_set_level(level);
    OK_();
}
#endif


static void get_cert(void)
{
    MibRequestConfirm_t r = { .Type = MIB_IS_CERT_FPORT_ON };
    abort_on_error(LoRaMacMibGetRequestConfirm(&r));

    OK("%d", r.Param.IsCertPortOn);
}


static void set_cert(atci_param_t *param)
{
    int enabled = parse_enabled(param);
    if (enabled == -1) abort(ERR_PARAM);

    MibRequestConfirm_t r = {
        .Type  = MIB_IS_CERT_FPORT_ON,
        .Param = { .IsCertPortOn = enabled }
    };
    abort_on_error(LoRaMacMibSetRequestConfirm(&r));
    OK_();
}


static void get_session(void)
{
    MibRequestConfirm_t r;

    atci_print("+OK=");

    r.Type = MIB_PUBLIC_NETWORK;
    LoRaMacMibGetRequestConfirm(&r);
    if (r.Param.EnablePublicNetwork) {
        atci_print("public");
    } else {
        atci_print("private");
    }

    r.Type = MIB_NETWORK_ACTIVATION;
    LoRaMacMibGetRequestConfirm(&r);
    atci_print(",");
    switch(r.Param.NetworkActivation) {
        case ACTIVATION_TYPE_NONE: atci_print("None"); break;
        case ACTIVATION_TYPE_ABP : atci_print("ABP");  break;
        case ACTIVATION_TYPE_OTAA: atci_print("OTAA"); break;
        default: atci_print("?"); break;
    }

    if (r.Param.NetworkActivation != ACTIVATION_TYPE_NONE) {
        r.Type = MIB_LORAWAN_VERSION;
        LoRaMacMibGetRequestConfirm(&r);
        atci_printf(",%d.%d.%d",
            r.Param.LrWanVersion.LoRaWan.Fields.Major,
            r.Param.LrWanVersion.LoRaWan.Fields.Minor,
            r.Param.LrWanVersion.LoRaWan.Fields.Patch);

        r.Type = MIB_NET_ID;
        LoRaMacMibGetRequestConfirm(&r);
        atci_printf(",%08lX", r.Param.NetID);

        r.Type = MIB_DEV_ADDR;
        LoRaMacMibGetRequestConfirm(&r);
        atci_printf(",%08lX", r.Param.DevAddr);
    }

    EOL();
}


// Manage data stored in NVM user registers
//
// To read the value in NVM register 0 use the syntaxt AT$NVM 0. To write the
// value 223 to NVM register 0, use the syntax AT$NVM 0,223.
static void nvm_userdata(atci_param_t *param)
{
    uint32_t addr, value;

    if (param == NULL) abort(ERR_PARAM);

    if (!atci_param_get_uint(param, &addr)) abort(ERR_PARAM);
    if (addr >= USER_NVM_MAX_SIZE) abort(ERR_PARAM);

    if (param->offset < param->length) {
        if (!atci_param_is_comma(param)) abort(ERR_PARAM);

        if (!atci_param_get_uint(param, &value)) abort(ERR_PARAM);
        if (value >= UINT8_MAX) abort(ERR_PARAM);

        user_nvm.values[addr] = value;
        nvm_update_user_data();
        OK_();
    } else {
        OK("%d", user_nvm.values[addr]);
    }
}


static void lock_keys(atci_param_t *param)
{
    (void)param;
    sysconf.lock_keys = 1;
    sysconf_modified = true;
    OK_();
}


#if DETACHABLE_LPUART == 1

#if FACTORY_RESET_PIN != 0
#  error DETACHABLE_LPUART and FACTORY_RESET_PIN cannot be enabled at the same time.
#endif

#if DEBUG_MCU != 0
#  error DETACHABLE_LPUART and DEBUG_MCU cannot be enabled at the same time.
#endif

static void detach_lpuart(atci_param_t *param)
{
    (void)param;

    // First check if the LPUART wake-up GPIO pin is low. If it is, the host
    // indicates that it wants to reattach the port. If that's the case, we
    // return an error.
    int v = gpio_read(attach_pin.port, attach_pin.pinIndex);
    if (v == 0) abort(ERR_DETACH_DENIED);

    // The SPI lines are connected to PB12, PB13, PB14, and PB15. We use PB12 as
    // the wake-up signal. The remaining pins are configured in analog mode with
    // no pull-up unless the factory reset pin or the debug MCU features are
    // enabled. Hence the error reported above if either of those features is
    // enabled with this feature. We would need to reconfigure those pins in
    // input mode and will not be able to put them back into the original
    // configuration when the modem is reattached.

    // Send an OK and wait for the OK to be also transmitted to the remote peer.
    OK_();
    atci_flush();

    // Finally, detach the LPUART port from its GPIOs. This operation stops DMA
    // and reconfigures LPUART GPIOs in analog input mode.
    lpuart_detach();

    // From this moment on, the modem cannot be woken up with ATCI activity. The
    // host has to pull lpuart_attach_pin down to wake the modem up and make it
    // reattach LPUART. Any incoming LoRaWAN downlinks will be buffered until
    // the ATCI port is attached again.
}


static void attach_isr(void *ctx)
{
    (void)ctx;
    lpuart_attach();
}


void cmd_init_attach_pin(void)
{
    // Note: This function is mutually exclusive with init_dbgmcu (PB12 conflict).

    GPIO_InitTypeDef gpio = {
        .Mode  = GPIO_MODE_IT_FALLING,
        .Pull  = GPIO_PULLUP,
        .Speed = GPIO_SPEED_HIGH,
    };

    if (attach_pin.port == GPIOA)      __GPIOA_CLK_ENABLE();
    else if (attach_pin.port == GPIOB) __GPIOB_CLK_ENABLE();
    else if (attach_pin.port == GPIOC) __GPIOC_CLK_ENABLE();
    else if (attach_pin.port == GPIOD) __GPIOD_CLK_ENABLE();
    else if (attach_pin.port == GPIOE) __GPIOE_CLK_ENABLE();
    else if (attach_pin.port == GPIOH) __GPIOH_CLK_ENABLE();

    gpio_init(attach_pin.port, attach_pin.pinIndex, &gpio);
    gpio_set_irq(attach_pin.port, attach_pin.pinIndex, 0, attach_isr);
}

#endif


static void get_device_time(atci_param_t *param)
{
    (void)param;
    abort_on_error(lrw_get_device_time());
    OK_();
}


static void get_time(void)
{
    SysTime_t t;

    atci_flush();

    t = SysTimeGet();
    t.Seconds -= UNIX_GPS_EPOCH_OFFSET;
    OK("%lu,%u", t.Seconds, t.SubSeconds);
}


static void set_time(atci_param_t *param)
{
    SysTime_t sys_time;
    uint32_t sec, msec;

    if (!atci_param_get_uint(param, &sec)) abort(ERR_PARAM);
    if (!atci_param_is_comma(param)) abort(ERR_PARAM_NO);
    if (!atci_param_get_uint(param, &msec)) abort(ERR_PARAM);
    if (msec > 999) abort(ERR_PARAM);
    if (param->offset != param->length) abort(ERR_PARAM_NO);

    sys_time.Seconds = sec + UNIX_GPS_EPOCH_OFFSET;
    sys_time.SubSeconds = msec;
    SysTimeSet(sys_time);

    OK_();
}


static const atci_command_t cmds[] = {
    {"+UART",        NULL,            set_uart,         get_uart,         NULL, "Configure UART interface"},
    {"+VER",         NULL,            NULL,             get_version_comp, NULL, "Firmware version and build time"},
    {"+DEV",         NULL,            NULL,             get_model,        NULL, "Device model"},
    {"+REBOOT",      reboot,          NULL,             NULL,             NULL, "Reboot the modem"},
    {"+FACNEW",      facnew,          NULL,             NULL,             NULL, "Restore modem to factory defaults"},
    {"+BAND",        NULL,            set_band,         get_band,         NULL, "Configure radio band (region)"},
    {"+CLASS",       NULL,            set_class,        get_class,        NULL, "Configure LoRaWAN class"},
    {"+MODE",        NULL,            set_mode,         get_mode,         NULL, "Configure activation mode (1:OTTA 0:ABP)"},
    {"+DEVADDR",     NULL,            set_devaddr,      get_devaddr,      NULL, "Configure DevAddr"},
    {"+DEVEUI",      NULL,            set_deveui,       get_deveui,       NULL, "Configure DevEUI"},
    {"+APPEUI",      NULL,            set_joineui,      get_joineui,      NULL, "Configure AppEUI (JoinEUI)"},
    {"+NWKSKEY",     NULL,            set_nwkskey,      get_nwkskey,      NULL, "Configure NwkSKey (LoRaWAN 1.0)"},
    {"+APPSKEY",     NULL,            set_appskey,      get_appskey,      NULL, "Configure AppSKey"},
    {"+APPKEY",      NULL,            set_appkey_10,    get_appkey,       NULL, "Configure AppKey (LoRaWAN 1.0)"},
    {"+JOIN",        join,            NULL,             NULL,             NULL, "Send OTAA Join packet"},
    {"+JOINDC",      NULL,            set_joindc,       get_joindc,       NULL, "Configure OTAA Join duty cycling"},
    {"+LNCHECK",     lncheck,         lncheck,          NULL,             NULL, "Perform link check"},
    {"+RFPARAM",     NULL,            set_rfparam,      get_rfparam,      NULL, "Configure RF channel parameters"},
    {"+RFPOWER",     NULL,            set_rfpower_comp, get_rfpower_comp, NULL, "Configure RF power"},
    {"+NWK",         NULL,            set_nwk,          get_nwk,          NULL, "Configure public/private LoRa network setting"},
    {"+ADR",         NULL,            set_adr,          get_adr,          NULL, "Configure adaptive data rate (ADR)"},
    {"+DR",          NULL,            set_dr_comp,      get_dr_comp,      NULL, "Configure data rate (DR)"},
    {"+DELAY",       NULL,            set_delay,        get_delay,        NULL, "Configure receive window offsets"},
    {"+ADRACK",      NULL,            set_adrack,       get_adrack,       NULL, "Configure ADR ACK parameters"},
    {"+RX2",         NULL,            set_rx2_comp,     get_rx2_comp,     NULL, "Configure RX2 window frequency and data rate"},
    {"+DUTYCYCLE",   NULL,            set_dutycycle,    get_dutycycle,    NULL, "Configure duty cycling in EU868"},
    {"+SLEEP",       NULL,            set_sleep,        get_sleep,        NULL, "Configure low power (sleep) mode"},
    {"+PORT",        NULL,            set_port,         get_port,         NULL, "Configure default port number for uplink messages <1,223>"},
    {"+REP",         NULL,            set_rep,          get_rep,          NULL, "Unconfirmed message repeats [1..15]"},
    {"+DFORMAT",     NULL,            set_dformat,      get_dformat,      NULL, "Configure payload format used by the modem"},
    {"+TO",          NULL,            set_to,           get_to,           NULL, "Configure UART port timeout"},
    {"+UTX",         utx,             NULL,             NULL,             NULL, "Send unconfirmed uplink message"},
    {"+CTX",         ctx,             NULL,             NULL,             NULL, "Send confirmed uplink message"},
    {"+MCAST",       NULL,            set_mcast,        get_mcast,        NULL, "Configure multicast addresses and keys"},
    {"+PUTX",        putx,            NULL,             NULL,             NULL, "Send unconfirmed uplink message to port"},
    {"+PCTX",        pctx,            NULL,             NULL,             NULL, "Send confirmed uplink message to port"},
    {"+FRMCNT",      NULL,            NULL,             get_frmcnt,       NULL, "Return current values for uplink and downlink counters"},
    {"+MSIZE",       NULL,            NULL,             get_msize,        NULL, "Return maximum payload size for current data rate"},
    {"+RFQ",         NULL,            NULL,             get_rfq,          NULL, "Return RSSI and SNR of the last received message"},
    {"+DWELL",       NULL,            set_dwell,        get_dwell,        NULL, "Configure dwell setting for AS923"},
    {"+MAXEIRP",     NULL,            set_maxeirp,      get_maxeirp,      NULL, "Configure maximum EIRP"},
    {"+RSSITH",      NULL,            set_rssith,       get_rssith,       NULL, "Configure RSSI threshold for LBT"},
    {"+CST",         NULL,            set_cst,          get_cst,          NULL, "Configure carrier sensor time (CST) for LBT"},
    {"+BACKOFF",     NULL,            NULL,             get_backoff,      NULL, "Return duty cycle backoff time for EU868"},
    {"+CHMASK",      NULL,            set_chmask_comp,  get_chmask_comp,  NULL, "Configure channel mask"},
    {"+RTYNUM",      NULL,            set_rtynum,       get_rtynum,       NULL, "Configure number of confirmed uplink message retries"},
    {"+NETID",       NULL,            set_netid,        get_netid,        NULL, "Configure LoRaWAN network identifier"},
    {"$VER",         NULL,            NULL,             get_version,      NULL, "Firmware version and build time"},
#if DEBUG_LOG != 0
    {"$DBG",         dbg,             NULL,             NULL,             NULL, ""},
#endif
    {"$HALT",        do_halt,         NULL,             NULL,             NULL, "Halt the modem"},
    {"$JOINEUI",     NULL,            set_joineui,      get_joineui,      NULL, "Configure JoinEUI"},
    {"$NWKKEY",      NULL,            set_nwkkey,       get_nwkkey,       NULL, "Configure NwkKey (LoRaWAN 1.1)"},
    {"$APPKEY",      NULL,            set_appkey_11,    get_appkey,       NULL, "Configure AppKey (LoRaWAN 1.1)"},
    {"$FNWKSINTKEY", NULL,            set_fnwksintkey,  get_fnwksintkey,  NULL, "Configure FNwkSIntKey (LoRaWAN 1.1)"},
    {"$SNWKSINTKEY", NULL,            set_snwksintkey,  get_snwksintkey,  NULL, "Configure SNwkSIntKey (LoRaWAN 1.1)"},
    {"$NWKSENCKEY",  NULL,            set_nwksenckey,   get_nwksenckey,   NULL, "Configure NwkSEncKey (LoRaWAN 1.1)"},
    {"$CHMASK",      NULL,            set_chmask,       get_chmask,       NULL, "Configure channel mask"},
    {"$RX2",         NULL,            set_rx2,          get_rx2,          NULL, "Configure RX2 window frequency and data rate"},
    {"$DR",          NULL,            set_dr,           get_dr,           NULL, "Configure data rate (DR)"},
    {"$RFPOWER",     NULL,            set_rfpower,      get_rfpower,      NULL, "Configure RF power"},
#if DEBUG_LOG != 0
    {"$LOGLEVEL",    NULL,            set_loglevel,     get_loglevel,     NULL, "Configure logging on USART port"},
#endif
    {"$CERT",        NULL,            set_cert,         get_cert,         NULL, "Enable or disable LoRaWAN certification port"},
    {"$SESSION",     NULL,            NULL,             get_session,      NULL, "Get network session information"},
    {"$CW",          cw,              NULL,             NULL,             NULL, "Start continuous carrier wave transmission"},
    {"$CM",          cm,              NULL,             NULL,             NULL, "Start continuous modulated FSK transmission"},
    {"$NVM",         nvm_userdata,    NULL,             NULL,             NULL, "Manage data in NVM user registers"},
    {"$LOCKKEYS",    lock_keys,       NULL,             NULL,             NULL, "Prevent read access to security keys from ATCI"},
#if DETACHABLE_LPUART == 1
    {"$DETACH",      detach_lpuart,   NULL,             NULL,             NULL, "Disconnect LPUART (ATCI) GPIOs"},
#endif
    {"$TIME",        NULL,            set_time,         get_time,         NULL, "Get or set modem's RTC time (GPS time)"},
    {"$DEVTIME",     get_device_time, NULL,             NULL,             NULL, "Get network time via DeviceTimeReq MAC command"},
    ATCI_COMMAND_CLAC,
    ATCI_COMMAND_HELP};


void cmd_init(unsigned int baudrate)
{
    atci_init(baudrate, cmds, ATCI_COMMANDS_LENGTH(cmds));
}


void cmd_event(unsigned int type, unsigned int subtype)
{
    atci_printf("+EVENT=%d,%d" ATCI_EOL, type, subtype);
}

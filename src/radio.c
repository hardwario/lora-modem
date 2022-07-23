#include <loramac-node/src/radio/sx1276/sx1276.h>
#include "log.h"


int16_t radio_rssi;
int8_t radio_snr;

// Below, we replace the RxDone callback given to us by LoRaMac-node with our
// own version so that we can save the RSSI and SNR if each received packet.
// The original callback (the one from LoRaMac-node) is kept here.
static void (*OrigRxDone)(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);

#if defined (DEBUG)

static const char *modem2str(RadioModems_t modem)
{
    switch(modem) {
        case MODEM_FSK  : return "FSK";
        case MODEM_LORA : return "LoRa";
        default         : return "?";
    }
}


static const char *lora_bandwidth2str(uint32_t bandwidth)
{
    switch(bandwidth) {
        case 0: return "125kHz";
        case 1: return "250kHz";
        case 2: return "500kHz";
        default: return "?";
    }
}


static const char *lora_sf2str(uint32_t sf)
{
    switch(sf) {
        case  6: return "SF6";
        case  7: return "SF7";
        case  8: return "SF8";
        case  9: return "SF9";
        case 10: return "SF10";
        case 11: return "SF11";
        case 12: return "SF12";
        default: return "SF?";
    }
}


static const char *coderate2str(uint8_t coderate)
{
    switch(coderate) {
        case 1 : return "4/5"; break;
        case 2 : return "4/6"; break;
        case 3 : return "4/7"; break;
        case 4 : return "4/8"; break;
        default: return "?/?"; break;
    }
}

#endif

static bool SX1276CheckRfFrequency(__attribute__((unused)) uint32_t frequency)
{
    // Implement check. Currently all frequencies are supported
    log_debug("SX1276CheckRfFrequency: %ld", frequency);
    return true;
}


static void SetChannel(uint32_t freq)
{
    log_debug("SX1276SetChannel: %.3f MHz", (float)freq / (float)1000000);
    SX1276SetChannel(freq);
}


static void SetTxConfig(RadioModems_t modem, int8_t power, uint32_t fdev,
    uint32_t bandwidth, uint32_t datarate, uint8_t coderate,
    uint16_t preambleLen, bool fixLen, bool crcOn, bool freqHopOn,
    uint8_t hopPeriod, bool iqInverted, uint32_t timeout)
{
#if defined (DEBUG)
    log_compose();
    log_debug("SX1276SetTxConfig: %d dBm", power);
    log_debug(" %s", modem2str(modem));

    if (modem == MODEM_LORA) {
        log_debug(" %s/%s %s", lora_sf2str(datarate),
            lora_bandwidth2str(bandwidth), coderate2str(coderate));
        log_debug(" preamb=%d", preambleLen);

        if (fixLen) log_debug(" fixLen");
        if (crcOn) log_debug(" CRC");
        if (freqHopOn) log_debug(" fHop(%d)", hopPeriod);
        if (iqInverted) log_debug(" iqInv");
    } else if (modem == MODEM_FSK) {
        log_debug(" fdev=%ld dr=%ld preamb=%d", fdev, datarate, preambleLen);
        if (fixLen) log_debug(" fixLen");
        if (crcOn) log_debug(" CRC");
    }

    log_debug(" tout=%ldms", timeout);
    log_finish();
#endif

    SX1276SetTxConfig(modem, power, fdev, bandwidth, datarate, coderate,
        preambleLen, fixLen, crcOn, freqHopOn, hopPeriod, iqInverted,
        timeout);
}


static void SetRxConfig(RadioModems_t modem, uint32_t bandwidth, uint32_t datarate,
    uint8_t coderate, uint32_t bandwidthAfc, uint16_t preambleLen,
    uint16_t symbTimeout, bool fixLen, uint8_t payloadLen, bool crcOn,
    bool freqHopOn, uint8_t hopPeriod, bool iqInverted, bool rxContinuous)
{
#if defined (DEBUG)
    log_compose();
    log_debug("SX1276SetRxConfig: %s", modem2str(modem));

    if (modem == MODEM_LORA) {
        log_debug(" %s/%s %s", lora_sf2str(datarate),
            lora_bandwidth2str(bandwidth), coderate2str(coderate));
        log_debug(" preamb=%d", preambleLen);
        log_debug(" symTout=%d", symbTimeout);

        if (fixLen) log_debug(" fixLen(%d)", payloadLen);
        if (crcOn) log_debug(" CRC");
        if (freqHopOn) log_debug(" fHop(%d)", hopPeriod);
        if (iqInverted) log_debug(" iqInv");
        if (rxContinuous) log_debug(" rxCont");
    } else if (modem == MODEM_FSK) {
        log_debug(" bw=%ld", bandwidth);
        log_debug(" dr=%ld", datarate);
        log_debug(" bwAfc=%ld", bandwidthAfc);
        log_debug(" preamb=%d", preambleLen);
        log_debug(" symTout=%d", symbTimeout);
        if (fixLen) log_debug(" fixLen(%d)", payloadLen);
        if (crcOn) log_debug(" CRC");
        if (rxContinuous) log_debug(" rxCont");
    }

    log_finish();
#endif

    SX1276SetRxConfig(modem, bandwidth, datarate, coderate, bandwidthAfc,
        preambleLen, symbTimeout, fixLen, payloadLen, crcOn, freqHopOn,
        hopPeriod, iqInverted, rxContinuous);
}


// This is our custom RxDone callback. We save the RSSI and SNR in global static
// variables so that they could be accessed from the application and delegate to
// the original callback.
static void RxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    radio_rssi = rssi;
    radio_snr = snr;
    if (OrigRxDone != NULL) OrigRxDone(payload, size, rssi, snr);
}


static void Init(RadioEvents_t *events)
{
    // Save the original RxDone callback and replace it with our own version
    OrigRxDone = events->RxDone;
    events->RxDone = RxDone;
    SX1276Init(events);
}


// Radio driver structure initialization
const struct Radio_s Radio = {
    .Init = Init,
    .GetStatus = SX1276GetStatus,
    .SetModem = SX1276SetModem,
    .SetChannel = SetChannel,
    .IsChannelFree = SX1276IsChannelFree,
    .Random = SX1276Random,
    .SetRxConfig = SetRxConfig,
    .SetTxConfig = SetTxConfig,
    .CheckRfFrequency = SX1276CheckRfFrequency,
    .TimeOnAir = SX1276GetTimeOnAir,
    .Send = SX1276Send,
    .Sleep = SX1276SetSleep,
    .Standby = SX1276SetStby,
    .Rx = SX1276SetRx,
    .StartCad = SX1276StartCad,
    .SetTxContinuousWave = SX1276SetTxContinuousWave,
    .Rssi = SX1276ReadRssi,
    .Write = SX1276Write,
    .Read = SX1276Read,
    .WriteBuffer = SX1276WriteBuffer,
    .ReadBuffer = SX1276ReadBuffer,
    .SetMaxPayloadLength = SX1276SetMaxPayloadLength,
    .SetPublicNetwork = SX1276SetPublicNetwork,
    .GetWakeupTime = SX1276GetWakeupTime,
    .IrqProcess = NULL,
    .RxBoosted = NULL,
    .SetRxDutyCycle = NULL
};

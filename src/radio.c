#include <loramac-node/src/radio/radio.h>
#include <loramac-node/src/radio/sx1276/sx1276.h>
#include "sx1276io.h"
#include "log.h"

static bool SX1276CheckRfFrequency(uint32_t frequency)
{
    // Implement check. Currently all frequencies are supported
    log_debug("Check frequency %ld", frequency);
    return true;
}

void SetChannel(uint32_t freq)
{
    SX1276SetChannel(freq);
    log_debug("SX1276: SetChannel freq: %ld", freq);
}

void SetTxConfig(RadioModems_t modem, int8_t power, uint32_t fdev,
                 uint32_t bandwidth, uint32_t datarate,
                 uint8_t coderate, uint16_t preambleLen,
                 bool fixLen, bool crcOn, bool freqHopOn,
                 uint8_t hopPeriod, bool iqInverted, uint32_t timeout)
{

    SX1276SetTxConfig(modem, power, fdev,
                      bandwidth, datarate,
                      coderate, preambleLen,
                      fixLen, crcOn, freqHopOn,
                      hopPeriod, iqInverted, timeout);
    log_debug("SX1276 SetTxConfig modem: %d power: %d fdev: %ld bandwidth: %ld datarate %ld", modem, power, fdev, bandwidth, datarate);
    log_debug("SX1276 SetTxConfig coderate: %d preambleLen: %d fixLen: %d crcOn: %d freqHopOn %d", coderate, preambleLen, fixLen, crcOn, freqHopOn);
    log_debug("SX1276 SetTxConfig hopPeriod: %d iqInverted: %d timeout: %ld ", hopPeriod, iqInverted, timeout);
}

void SetRxConfig(RadioModems_t modem, uint32_t bandwidth,
                 uint32_t datarate, uint8_t coderate,
                 uint32_t bandwidthAfc, uint16_t preambleLen,
                 uint16_t symbTimeout, bool fixLen,
                 uint8_t payloadLen,
                 bool crcOn, bool freqHopOn, uint8_t hopPeriod,
                 bool iqInverted, bool rxContinuous)
{
    SX1276SetRxConfig(modem, bandwidth,
                      datarate, coderate,
                      bandwidthAfc, preambleLen,
                      symbTimeout, fixLen,
                      payloadLen,
                      crcOn, freqHopOn, hopPeriod,
                      iqInverted, rxContinuous);
    log_debug("SX1276 SetRxConfig modem: %d bandwidth: %ld datarate %ld", modem, bandwidth, datarate);
    log_debug("SX1276 SetRxConfig coderate: %d bandwidthAfc: %ld preambleLen: %d symbTimeout: %d fixLen %d", coderate, bandwidthAfc, preambleLen, symbTimeout, fixLen);
    log_debug("SX1276 SetRxConfig payloadLen: %d crcOn: %d freqHopOn: %d hopPeriod: %d iqInverted: %d rxContinuous: %d", payloadLen, crcOn, freqHopOn, hopPeriod, iqInverted, rxContinuous);
}

// Radio driver structure initialization
const struct Radio_s Radio =
{
    .Init = SX1276Init,
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


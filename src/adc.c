#include "adc.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include "log.h"

#define VDDA_VREFINT_CAL ((uint32_t)3000)

// Internal voltage reference, parameter VREFINT_CAL
#define VREFINT_CAL ((uint16_t *)((uint32_t)0x1FF80078))

// Internal temperature sensor: constants data used for indicative values in
// this example. Refer to device datasheet for min/typ/max values.

// Internal temperature sensor, parameter TS_CAL1: TS ADC raw data acquired at a
// temperature of 110 DegC (+-5 DegC), VDDA = 3.3 V (+-10 mV).
#define TEMP30_CAL_ADDR ((uint16_t *)((uint32_t)0x1FF8007A))

// Internal temperature sensor, parameter TS_CAL2: TS ADC raw data acquired at a
// temperature of  30 DegC (+-5 DegC), VDDA = 3.3 V (+-10 mV).
#define TEMP110_CAL_ADDR ((uint16_t *)((uint32_t)0x1FF8007E))

// Vdda value with which temperature sensor has been calibrated in production
// (+-10 mV).
#define VDDA_TEMP_CAL ((uint32_t)3000)

#define COMPUTE_TEMPERATURE(TS_ADC_DATA, VDDA_APPLI) \
    (((((((int32_t)((TS_ADC_DATA * VDDA_APPLI) / VDDA_TEMP_CAL) - \
    (int32_t)*TEMP30_CAL_ADDR)) * (int32_t)(110 - 30)) << 8) / \
    (int32_t)(*TEMP110_CAL_ADDR - *TEMP30_CAL_ADDR)) + (30 << 8))


static ADC_HandleTypeDef adc = {
    .Init = {
        .OversamplingMode = DISABLE,

        .ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4,
        .LowPowerAutoPowerOff = DISABLE,
        .LowPowerFrequencyMode = ENABLE,
        .LowPowerAutoWait = DISABLE,

        .Resolution = ADC_RESOLUTION_12B,
        .SamplingTime = ADC_SAMPLETIME_160CYCLES_5,
        .ScanConvMode = ADC_SCAN_DIRECTION_FORWARD,
        .DataAlign = ADC_DATAALIGN_RIGHT,
        .ContinuousConvMode = DISABLE,
        .DiscontinuousConvMode = DISABLE,
        .ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE,
        .EOCSelection = ADC_EOC_SINGLE_CONV,
        .DMAContinuousRequests = DISABLE
    }
};


void adc_init(void)
{
    // We do not initialize ADC when the system boots up. Instead, the ADC
    // peripheral is initialized on first use, e.g., when the LoRa MAC attempts
    // to measure batter or temperature levels.
}


void adc_deinit(void)
{
    if (adc.Instance != NULL) {
        __HAL_RCC_ADC1_CLK_ENABLE();
        HAL_ADC_DeInit(&adc);
        adc.Instance = NULL;
    }
    __HAL_RCC_ADC1_CLK_DISABLE();
}


void adc_before_stop(void)
{
    // Disable ADC entirely before going to Stop mode
    if (adc.Instance != NULL) {
        __HAL_RCC_ADC1_CLK_ENABLE();
        HAL_ADC_DeInit(&adc);
        adc.Instance = NULL;
    }
}


void adc_after_stop(void)
{
    // Do nothing when waking up from Stop mode. We will instead initialize the
    // ADC on first use.
}


uint16_t adc_get_value(uint32_t channel)
{
    HAL_StatusTypeDef rc;
    ADC_ChannelConfTypeDef cfg = { 0 };

    __HAL_RCC_ADC1_CLK_ENABLE();

    if (adc.Instance == NULL) {
        // This branch will execute if ADC has not been initialized yet. This
        // happens the first time ADC is used after boot or the first time the
        // ADC is used after waking up from the Stop mode.

        // Wait for the Vrefint to stabilize if we're waking up from Stop mode
        __HAL_RCC_PWR_CLK_ENABLE();
        while (__HAL_PWR_GET_FLAG(PWR_FLAG_VREFINTRDY) == RESET);
        __HAL_RCC_PWR_CLK_DISABLE();

        adc.Instance = ADC1;
        rc = HAL_ADC_Init(&adc);
        if (rc != HAL_OK) {
            log_error("Error while initializing ADC: %d", rc);
            goto error;
        }

        rc = HAL_ADCEx_Calibration_Start(&adc, ADC_SINGLE_ENDED);
        if (rc != HAL_OK) {
            log_error("Error while calibrating ADC: %d", rc);
            goto error;
        }
    }

    // Deselect all channels
    cfg.Channel = ADC_CHANNEL_MASK;
    cfg.Rank = ADC_RANK_NONE;
    rc = HAL_ADC_ConfigChannel(&adc, &cfg);
    if (rc != HAL_OK) {
        log_error("Error in HAL_ADC_ConfigChannel: %d", rc);
        goto error;
    }

    // Configure ADC channel
    cfg.Channel = channel;
    cfg.Rank = ADC_RANK_CHANNEL_NUMBER;
    rc = HAL_ADC_ConfigChannel(&adc, &cfg);
    if (rc != HAL_OK) {
        log_error("Error in HAL_ADC_ConfigChannel: %d", rc);
        goto error;
    }

    rc = HAL_ADC_Start(&adc);
    if (rc != HAL_OK) {
        log_error("Error in HAL_ADC_Start: %d", rc);
        goto error;
    }

    rc = HAL_ADC_PollForConversion(&adc, HAL_MAX_DELAY);
    if (rc != HAL_OK) {
        log_error("Error in HAL_ADC_PollForConversion: %d", rc);
        goto error2;
    }

    uint16_t v = HAL_ADC_GetValue(&adc);
    HAL_ADC_Stop(&adc);
    __HAL_RCC_ADC1_CLK_DISABLE();
    return v;

error2:
    HAL_ADC_Stop(&adc);
error:
    if (adc.Instance != NULL) {
        HAL_ADC_DeInit(&adc);
        adc.Instance = NULL;
    }
    __HAL_RCC_ADC1_CLK_DISABLE();
    return 0;
}


uint16_t adc_get_battery_level(void)
{
    uint16_t v = adc_get_value(ADC_CHANNEL_VREFINT);
    if (v == 0) return 0;
    return (((uint32_t)VDDA_VREFINT_CAL * (*VREFINT_CAL)) / v);
}


uint16_t adc_get_temperature_level(void)
{
    uint32_t v = adc_get_battery_level();
    uint16_t t = adc_get_value(ADC_CHANNEL_TEMPSENSOR);
    return COMPUTE_TEMPERATURE(t, v);
}


float adc_get_temperature_celsius(void)
{
    uint16_t t = adc_get_temperature_level();
    uint16_t i = t >> 8;
    uint16_t f = ((t - (i << 8)) * 100) >> 8;
    float v = (float)i + (float)f / 100.f;
    log_debug("adc_get_temperature_celsius: %f", v);
    return v;
}

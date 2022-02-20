#include "adc.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include "log.h"

#define VDDA_VREFINT_CAL ((uint32_t)3000)

/* Internal voltage reference, parameter VREFINT_CAL*/
#define VREFINT_CAL ((uint16_t *)((uint32_t)0x1FF80078))

/* Internal temperature sensor: constants data used for indicative values in  */
/* this example. Refer to device datasheet for min/typ/max values.            */

/* Internal temperature sensor, parameter TS_CAL1: TS ADC raw data acquired at
 *a temperature of 110 DegC (+-5 DegC), VDDA = 3.3 V (+-10 mV). */
#define TEMP30_CAL_ADDR ((uint16_t *)((uint32_t)0x1FF8007A))

/* Internal temperature sensor, parameter TS_CAL2: TS ADC raw data acquired at
 *a temperature of  30 DegC (+-5 DegC), VDDA = 3.3 V (+-10 mV). */
#define TEMP110_CAL_ADDR ((uint16_t *)((uint32_t)0x1FF8007E))

/* Vdda value with which temperature sensor has been calibrated in production
   (+-10 mV). */
#define VDDA_TEMP_CAL ((uint32_t)3000)

#define COMPUTE_TEMPERATURE(TS_ADC_DATA, VDDA_APPLI) \
    (((((((int32_t)((TS_ADC_DATA * VDDA_APPLI) / VDDA_TEMP_CAL) - (int32_t)*TEMP30_CAL_ADDR)) * (int32_t)(110 - 30)) << 8) / (int32_t)(*TEMP110_CAL_ADDR - *TEMP30_CAL_ADDR)) + (30 << 8))

static ADC_HandleTypeDef hadc;
static bool AdcInitialized = false;

void adc_init(void)
{
    if (AdcInitialized == false)
    {
        AdcInitialized = true;

        hadc.Instance = ADC1;

        hadc.Init.OversamplingMode = DISABLE;

        hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
        hadc.Init.LowPowerAutoPowerOff = DISABLE;
        hadc.Init.LowPowerFrequencyMode = ENABLE;
        hadc.Init.LowPowerAutoWait = DISABLE;

        hadc.Init.Resolution = ADC_RESOLUTION_12B;
        hadc.Init.SamplingTime = ADC_SAMPLETIME_160CYCLES_5;
        hadc.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
        hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
        hadc.Init.ContinuousConvMode = DISABLE;
        hadc.Init.DiscontinuousConvMode = DISABLE;
        hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
        hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
        hadc.Init.DMAContinuousRequests = DISABLE;

        __HAL_RCC_ADC1_CLK_ENABLE();

        HAL_ADC_Init(&hadc);
    }
}

void adc_deinit(void)
{
    AdcInitialized = false;
    HAL_ADC_DeInit(&hadc);
}

uint16_t adc_get_value(uint32_t channel)
{
    ADC_ChannelConfTypeDef adcConf = {0};

    uint16_t adcData = 0;

    adc_init();

    if (AdcInitialized == true)
    {
        /* wait the the Vrefint used by adc is set */
        while (__HAL_PWR_GET_FLAG(PWR_FLAG_VREFINTRDY) == RESET)
        {
        };

        __HAL_RCC_ADC1_CLK_ENABLE();

        /*calibrate ADC if any calibraiton hardware*/
        HAL_ADCEx_Calibration_Start(&hadc, ADC_SINGLE_ENDED);

        /* Deselects all channels*/
        adcConf.Channel = ADC_CHANNEL_MASK;
        adcConf.Rank = ADC_RANK_NONE;
        HAL_ADC_ConfigChannel(&hadc, &adcConf);

        /* configure adc channel */
        adcConf.Channel = channel;
        adcConf.Rank = ADC_RANK_CHANNEL_NUMBER;
        HAL_ADC_ConfigChannel(&hadc, &adcConf);

        /* Start the conversion process */
        HAL_ADC_Start(&hadc);

        /* Wait for the end of conversion */
        HAL_ADC_PollForConversion(&hadc, HAL_MAX_DELAY);

        /* Get the converted value of regular channel */
        adcData = HAL_ADC_GetValue(&hadc);

        __HAL_ADC_DISABLE(&hadc);

        __HAL_RCC_ADC1_CLK_DISABLE();
    }
    return adcData;
}

uint16_t adc_get_temperature_level(void)
{
    uint16_t measuredLevel = 0;
    uint32_t batteryLevelmV;
    uint16_t temperatureDegreeC;

    measuredLevel = adc_get_value(ADC_CHANNEL_VREFINT);

    if (measuredLevel == 0)
    {
        batteryLevelmV = 0;
    }
    else
    {
        batteryLevelmV = (((uint32_t)VDDA_VREFINT_CAL * (*VREFINT_CAL)) / measuredLevel);
    }

    measuredLevel = adc_get_value(ADC_CHANNEL_TEMPSENSOR);

    temperatureDegreeC = COMPUTE_TEMPERATURE(measuredLevel, batteryLevelmV);

    return (uint16_t)temperatureDegreeC;
}

float adc_get_temperature_celsius(void)
{
    uint16_t temp = adc_get_temperature_level();
    uint16_t temp_int = (temp) >> 8;
    uint16_t temp_frac = ((temp - (temp_int << 8)) * 100) >> 8;
    return (float) temp_int + ((float) temp_frac) / 100.f;
}

uint16_t adc_get_battery_level(void)
{
    uint16_t measuredLevel = 0;
    uint32_t batteryLevelmV;

    measuredLevel = adc_get_value(ADC_CHANNEL_VREFINT);

    if (measuredLevel == 0)
    {
        batteryLevelmV = 0;
    }
    else
    {
        batteryLevelmV = (((uint32_t)VDDA_VREFINT_CAL * (*VREFINT_CAL)) / measuredLevel);
    }

    return batteryLevelmV;
}

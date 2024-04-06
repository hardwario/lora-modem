#ifndef _ADC_H
#define _ADC_H

#include <stdint.h>

//! @brief Initializes the ADC input

void adc_init(void);

void adc_before_stop(void);

void adc_after_stop(void);

//! @brief DeInitializes the ADC

void adc_deinit(void);

//! @brief Read the analog voltage value
//! @param[in] Channel to read
//! @retval value Analogue value

uint16_t adc_get_value(uint32_t channel);

//! @brief Get the current temperature
//! @retval value temperature in degreeCelcius( q7.8 )

uint16_t adc_get_temperature_level(void);

//! @brief Get the current temperature in Celsius
//! @retval value temperature

float adc_get_temperature_celsius(void);

//! @brief Get the current battery level
//! @retval value  battery level ( 0: very low, 254: fully charged )

uint16_t adc_get_battery_level(void);

#endif // _ADC_H

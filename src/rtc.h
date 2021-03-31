#ifndef _HW_RTC_H
#define _HW_RTC_H

#include "common.h"
#include "timer.h"
#include "stm32l0xx_hal.h"

//! @param Temperature coefficient of the clock source

#define RTC_TEMP_COEFFICIENT (-0.035)

//! @param Temperature coefficient deviation of the clock source

#define RTC_TEMP_DEV_COEFFICIENT (0.0035)

//! @param Turnover temperature of the clock source

#define RTC_TEMP_TURNOVER (25.0)

//! @param Turnover temperature deviation of the clock source

#define RTC_TEMP_DEV_TURNOVER (5.0)

//! @param Initializes the RTC timer
//! @note The timer is based on the RTC

void rtc_init(void);

//! @param Stop the Alarm

void rtc_stop_alarm(void);

//! @param Return the minimum timeout the RTC is able to handle
//! @retval minimum value for a timeout

uint32_t rtc_get_min_timeout(void);

//! @brief Set the alarm
//! @note The alarm is set at Reference + timeout
//! @param timeout Duration of the Timer in ticks

void rtc_set_alarm(uint32_t timeout);

//! @brief Get the RTC timer elapsed time since the last Reference was set
//! @retval RTC Elapsed time in ticks

uint32_t rtc_get_timer_elapsed_time(void);

//! @brief Get the RTC timer value

uint32_t rtc_get_timer_value(void);

//! @brief Set the RTC timer Reference
//! @retval  Timer Reference Value in  Ticks

uint32_t rtc_set_timer_context(void);

//! @brief Get the RTC timer Reference
//! @retval Timer Value in  Ticks

uint32_t rtc_get_timer_context(void);

//! @brief a delay of delay ms by polling RTC
//! @param delay in ms

void rtc_delay_ms(uint32_t delay);

//! @brief calculates the wake up time between wake up and mcu start
//! @note resolution in RTC_ALARM_TIME_BASE

void rtc_set_mcu_wake_up_time(void);

//! @brief returns the wake up time in us
//! @retval wake up time in ticks

int16_t rtc_get_mcu_wake_up_time(void);

//! @brief converts time in ms to time in ticks
//! @param [IN] time in milliseconds
//! @retval returns time in timer ticks

uint32_t rtc_ms2tick(TimerTime_t timeMilliSec);

//! @brief converts time in ticks to time in ms
//! @param [IN] time in timer ticks
//! @retval returns time in timer milliseconds

TimerTime_t rtc_tick2ms(uint32_t tick);

//! @brief Computes the temperature compensation for a period of time on a specific temperature.
//! @param [IN] period Time period to compensate
//! @param [IN] temperature Current temperature
//! @retval Compensated time period

TimerTime_t rtc_temperature_compensation(TimerTime_t period, float temperature);

//! @brief Get system time
//! @param [IN] subSeconds in ms
//! @retval

uint32_t rtc_get_calendar_time(uint16_t *subSeconds);

//! @brief Read from backup registers
//! @param [IN]  Data 0
//! @param [IN]  Data 1

void rtc_read_backup_registers(uint32_t *Data0, uint32_t *Data1);

//! @brief Write in backup registers
//! @param [IN]  Data 0
//! @param [IN]  Data 1

void rtc_write_backup_registers(uint32_t Data0, uint32_t Data1);

#endif // _HW_RTC_H

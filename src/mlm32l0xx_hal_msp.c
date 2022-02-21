#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include <LoRaWAN/Utilities/timeServer.h>
#include "common.h"
#include "halt.h"
#include "rtc.h"
#include "gpio.h"

/* when fast wake up is enabled, the mcu wakes up in ~20us  * and
 * does not wait for the VREFINT to be settled. THis is ok for
 * most of the case except when adc must be used in this case before
 *starting the adc, you must make sure VREFINT is settled*/
#define ENABLE_FAST_WAKEUP

/**
  * @brief This function configures the source of the time base.
  * @brief  don't enable systick
  * @param TickPriority: Tick interrupt priority.
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    (void) TickPriority;
    return HAL_OK;
}

/**
  * @brief This function provides delay (in ms)
  * @param Delay: specifies the delay time length, in milliseconds.
  * @retval None
  */
void HAL_Delay(__IO uint32_t Delay)
{
    rtc_delay_ms(Delay); /* based on RTC */
}

/**
  * @brief  Initializes the MSP.
  * @retval None
  */
void HAL_MspInit(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();

    /* Disable the Power Voltage Detector */
    HAL_PWR_DisablePVD();

    /* Enables the Ultra Low Power mode */
    HAL_PWREx_EnableUltraLowPower();

    __HAL_FLASH_SLEEP_POWERDOWN_ENABLE();

/*In debug mode, e.g. when DBGMCU is activated, Arm core has always clocks
   * And will not wait that the FLACH is ready to be read. It can miss in this
   * case the first instruction. To overcome this issue, the flash remain clcoked during sleep mode
   */
#ifdef DEBUG
    do
    {
        __HAL_FLASH_SLEEP_POWERDOWN_DISABLE();
    } while (0);
#endif

#ifdef ENABLE_FAST_WAKEUP
    /*Enable fast wakeUp*/
    HAL_PWREx_EnableFastWakeUp();
#else
    HAL_PWREx_DisableFastWakeUp();
#endif
}

/**
  * @brief RTC MSP Initialization
  *        This function configures the hardware resources used in this example:
  *           - Peripheral's clock enable
  * @param hrtc: RTC handle pointer
  * @note  Care must be taken when HAL_RCCEx_PeriphCLKConfig() is used to select
  *        the RTC clock source; in this case the Backup domain will be reset in
  *        order to modify the RTC Clock source, as consequence RTC registers (including
  *        the backup registers) and RCC_CSR register are set to their reset values.
  * @retval None
  */
void HAL_RTC_MspInit(RTC_HandleTypeDef *hrtc)
{
    (void) hrtc;
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    /*##-1- Configue the RTC clock soucre ######################################*/
    /* -a- Enable LSE Oscillator */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        halt("Error while initializing oscillator");
    }

    /* -b- Select LSI as RTC clock source */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        halt("Error while initializing clock source");
    }

    /*##-2- Enable the RTC peripheral Clock ####################################*/
    /* Enable RTC Clock */
    __HAL_RCC_RTC_ENABLE();

    /*##-3- Configure the NVIC for RTC Alarm ###################################*/
    HAL_NVIC_SetPriority(RTC_IRQn, 0x0, 0);
    HAL_NVIC_EnableIRQ(RTC_IRQn);
}

/**
  * @brief RTC MSP De-Initialization
  *        This function freeze the hardware resources used in this example:
  *          - Disable the Peripheral's clock
  * @param hrtc: RTC handle pointer
  * @retval None
  */
void HAL_RTC_MspDeInit(RTC_HandleTypeDef *hrtc)
{
    (void) hrtc;
    /* Reset peripherals */
    __HAL_RCC_RTC_DISABLE();
}

/**
  * @brief  Alarm A callback.
  * @param  hrtc: RTC handle
  * @retval None
  */
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    (void) hrtc;
    TimerIrqHandler();
}

/**
  * @brief  EXTI line detection callbacks.
  * @param  GPIO_Pin: Specifies the pins connected to the EXTI line.
  * @retval None
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    gpio_hal_msp_irq_handler(GPIO_Pin);
}

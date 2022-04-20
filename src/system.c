#include "system.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include <LoRaWAN/Utilities/utilities.h>
#include "rtc.h"
#include "irq.h"
#include "io.h"
#include "halt.h"

#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_bus.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_cortex.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_iwdg.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_pwr.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_rcc.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_system.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_utils.h>

// Unique Devices IDs register set ( STM32L0xxx )
#define _SYSTEM_ID1 (0x1FF80050)
#define _SYSTEM_ID2 (0x1FF80054)
#define _SYSTEM_ID3 (0x1FF80064)

static void _system_init_flash(void);
static void _system_init_gpio(void);
static void _system_init_debug(void);
static void _system_init_clock(void);

volatile static unsigned stop_mode_mask;
volatile static unsigned sleep_mask;

void system_init(void)
{
    /* STM32 HAL library initialization*/
    HAL_Init();

    _system_init_flash();

    _system_init_gpio();

    _system_init_debug();

    _system_init_clock();

    rtc_init();

    // irq_init();
}

void system_reset(void)
{
    NVIC_SystemReset();
}

uint32_t system_get_random_seed(void)
{
    return ((*(uint32_t *)_SYSTEM_ID1) ^ (*(uint32_t *)_SYSTEM_ID2) ^ (*(uint32_t *)_SYSTEM_ID3));
}

void system_get_unique_id(uint8_t *id)
{
    id[7] = ((*(uint32_t *)_SYSTEM_ID1) + (*(uint32_t *)_SYSTEM_ID3)) >> 24;
    id[6] = ((*(uint32_t *)_SYSTEM_ID1) + (*(uint32_t *)_SYSTEM_ID3)) >> 16;
    id[5] = ((*(uint32_t *)_SYSTEM_ID1) + (*(uint32_t *)_SYSTEM_ID3)) >> 8;
    id[4] = ((*(uint32_t *)_SYSTEM_ID1) + (*(uint32_t *)_SYSTEM_ID3));
    id[3] = ((*(uint32_t *)_SYSTEM_ID2)) >> 24;
    id[2] = ((*(uint32_t *)_SYSTEM_ID2)) >> 16;
    id[1] = ((*(uint32_t *)_SYSTEM_ID2)) >> 8;
    id[0] = ((*(uint32_t *)_SYSTEM_ID2));
}

void system_wait_hsi(void)
{
    while ((RCC->CR & RCC_CR_HSIRDY) == 0)
    {
        continue;
    }
}

void system_allow_stop_mode(system_module_t module)
{
    irq_disable();
    stop_mode_mask &= ~module;
    irq_enable();
}

void system_disallow_stop_mode(system_module_t module)
{
    irq_disable();
    stop_mode_mask |= module;
    irq_enable();
}

bool system_is_stop_mode_allowed(void)
{
    irq_disable();
    bool res = stop_mode_mask == 0;
    irq_enable();
    return res;
}

unsigned system_get_stop_mode_mask(void)
{
    irq_disable();
    unsigned mask = stop_mode_mask;
    irq_enable();
    return mask;
}

int system_sleep_allowed(void)
{
    return sleep_mask == 0;
}

void system_allow_sleep(system_module_t module)
{
    irq_disable();
    sleep_mask &= ~module;
    irq_enable();
}

void system_disallow_sleep(system_module_t module)
{
    irq_disable();
    sleep_mask |= module;
    irq_enable();
}

void system_low_power(void)
{
    // Sleeping is disabled completely, do nothing
    if (sleep_mask) return;

    if (stop_mode_mask) {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    } else {
        {
            // Enter STOP mode
            BACKUP_PRIMASK();
            DISABLE_IRQ();
            system_on_enter_stop_mode();
            SET_BIT(PWR->CR, PWR_CR_CWUF);
            RESTORE_PRIMASK();
            HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
        } {
            // Wake-up from STOP mode
            BACKUP_PRIMASK();
            DISABLE_IRQ();

            // reconfigure the system clock
            __HAL_RCC_HSI_ENABLE();
            while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) == RESET)
                continue;

            __HAL_RCC_PLL_ENABLE();
            while (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) == RESET)
                continue;

            __HAL_RCC_SYSCLK_CONFIG(RCC_SYSCLKSOURCE_PLLCLK);
            while (__HAL_RCC_GET_SYSCLK_SOURCE() != RCC_SYSCLKSOURCE_STATUS_PLLCLK)
                continue;

            system_on_exit_stop_mode();
            RESTORE_PRIMASK();
        }
    }
}

static void _system_init_flash(void)
{
    // Enable prefetch
    FLASH->ACR |= FLASH_ACR_PRFTEN;

    // One wait state is used to read word from NVM
    FLASH->ACR |= FLASH_ACR_LATENCY;
}

static void _system_init_gpio(void)
{
    //Configure all GPIO's to Analog input to reduce the power consumption
    // GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Configure all GPIO as analog to reduce current consumption on non used IOs */
    /* Enable GPIOs clock */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    // GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    // GPIO_InitStruct.Pull = GPIO_NOPULL;
    // /* All GPIOs except debug pins (SWCLK and SWD) */
    // GPIO_InitStruct.Pin = GPIO_PIN_All & (~(GPIO_PIN_13 | GPIO_PIN_14));
    // HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // /* All GPIOs */
    // GPIO_InitStruct.Pin = GPIO_PIN_All;
    // HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    // HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    // HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

    GPIOA->PUPDR   = 0x24002040;
    GPIOA->AFR[0]  = 0x00006600;
    GPIOA->AFR[1]  = 0x00000040;
    GPIOA->OTYPER  = 0x00000000;
    GPIOA->OSPEEDR = 0xcc0cc0f0;
    GPIOA->ODR     = 0x00008000;
    GPIOA->MODER   = 0x69fbafaf;

    GPIOB->PUPDR   = 0x00000000;
    GPIOB->AFR[0]  = 0x00000000;
    GPIOB->AFR[1]  = 0x00000000;
    GPIOB->OTYPER  = 0x00000000;
    GPIOB->OSPEEDR = 0x000000c0;
    GPIOB->ODR     = 0x00000000;
    GPIOB->MODER   = 0xffffffbf;

    GPIOC->PUPDR   = 0x00000000;
    GPIOC->AFR[0]  = 0x00000000;
    GPIOC->AFR[1]  = 0x00000000;
    GPIOC->OTYPER  = 0x00000000;
    GPIOC->OSPEEDR = 0x00000000;
    GPIOC->ODR     = 0x00000000;
    GPIOC->MODER   = 0xffffffd5;

    GPIOH->PUPDR   = 0x00000000;
    GPIOH->AFR[0]  = 0x00000000;
    GPIOH->AFR[1]  = 0x00000000;
    GPIOH->OTYPER  = 0x00000000;
    GPIOH->OSPEEDR = 0x00000000;
    GPIOH->ODR     = 0x00000000;
    GPIOH->MODER   = 0x003c000f;

    /* Disable GPIOs clock */
    __HAL_RCC_GPIOA_CLK_DISABLE();
    __HAL_RCC_GPIOB_CLK_DISABLE();
    __HAL_RCC_GPIOC_CLK_DISABLE();
    __HAL_RCC_GPIOH_CLK_DISABLE();
}
/*
static void _system_init_debug(void)
{
    #ifdef DEBUG

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_DBGMCU);

    LL_DBGMCU_EnableDBGSleepMode();
    LL_DBGMCU_EnableDBGStopMode();
    LL_DBGMCU_EnableDBGStandbyMode();

    LL_DBGMCU_APB1_GRP1_FreezePeriph(LL_DBGMCU_APB1_GRP1_IWDG_STOP);
    LL_DBGMCU_APB1_GRP1_FreezePeriph(LL_DBGMCU_APB1_GRP1_LPTIM1_STOP);

    #else

    LL_PWR_EnableFastWakeUp();
    LL_PWR_EnableUltraLowPower();
    LL_PWR_SetRegulModeLP(LL_PWR_REGU_LPMODES_LOW_POWER);
    LL_LPM_EnableDeepSleep();

    #endif
}
*/

static void _system_init_debug(void)
{
#ifdef DEBUG
    GPIO_InitTypeDef gpioinitstruct = {0};

    // Enable the GPIO_B Clock
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // Configure the GPIO pin
    gpioinitstruct.Mode = GPIO_MODE_OUTPUT_PP;
    gpioinitstruct.Pull = GPIO_PULLUP;
    gpioinitstruct.Speed = GPIO_SPEED_HIGH;

    gpioinitstruct.Pin = (GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    HAL_GPIO_Init(GPIOB, &gpioinitstruct);

    // Reset debug Pins
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);

    __HAL_RCC_DBGMCU_CLK_ENABLE();

    HAL_DBGMCU_EnableDBGSleepMode();
    HAL_DBGMCU_EnableDBGStopMode();
    HAL_DBGMCU_EnableDBGStandbyMode();

#else
    // sw interface off
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStructure.Pull = GPIO_NOPULL;
    GPIO_InitStructure.Pin = (GPIO_PIN_13 | GPIO_PIN_14);
    __GPIOA_CLK_ENABLE();
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);
    __GPIOA_CLK_DISABLE();

    __HAL_RCC_DBGMCU_CLK_ENABLE();
    HAL_DBGMCU_DisableDBGSleepMode();
    HAL_DBGMCU_DisableDBGStopMode();
    HAL_DBGMCU_DisableDBGStandbyMode();
    __HAL_RCC_DBGMCU_CLK_DISABLE();

#endif
}

/**
  * @brief  System Clock Configuration
  *         The system Clock is configured as follow :
  *            System Clock source            = PLL (HSI)
  *            SYSCLK(Hz)                     = 32000000
  *            HCLK(Hz)                       = 32000000
  *            AHB Prescaler                  = 1
  *            APB1 Prescaler                 = 1
  *            APB2 Prescaler                 = 1
  *            HSI Frequency(Hz)              = 16000000
  *            PLLMUL                         = 6
  *            PLLDIV                         = 3
  *            Flash Latency(WS)              = 1
  * @retval None
  */

static void _system_init_clock(void)
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};

    /* Enable HSE Oscillator and Activate PLL with HSE as source */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSEState = RCC_HSE_OFF;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLLMUL_6;
    RCC_OscInitStruct.PLL.PLLDIV = RCC_PLLDIV_3;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        halt("Error while initializing oscillator");
    }

    /* Set Voltage scale1 as MCU will run at 32MHz */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* Poll VOSF bit of in PWR_CSR. Wait until it is reset to 0 */
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOS) != RESET)
    {
    };

    /* Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2
  clocks dividers */
    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
    {
        halt("Error while initializing system clock");
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

__weak void system_on_enter_stop_mode(void)
{
}

__weak void system_on_exit_stop_mode(void)
{
}


#include "system.h"
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_pwr.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_rcc.h>
#include "rtc.h"
#include "irq.h"
#include "halt.h"
#include "gpio.h"
#include "nvm.h"
#include "lrw.h"


// Unique Devices IDs register set ( STM32L0xxx )
#define _SYSTEM_ID1 (0x1FF80050)
#define _SYSTEM_ID2 (0x1FF80054)
#define _SYSTEM_ID3 (0x1FF80064)


volatile unsigned system_stop_lock;
volatile unsigned system_sleep_lock;


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
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) == RESET) continue;
}


// Note: this function must be called with interrupts disabled
void system_idle(void)
{
    int pwr_disabled;

    // Do nothing if low-power operation is disabled entirely
    if (!sysconf.sleep) return;

    // Do nothing if sleeping is prevented by a subsystem
    if (system_sleep_lock) return;

    if (system_stop_lock) {
        // If Stop mode is prevented by a subsystem, enter the low-power sleep
        // mode only.
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    } else {
        // Enter the low-power Stop mode

        system_before_stop();

        pwr_disabled = __HAL_RCC_PWR_IS_CLK_DISABLED();
        if (pwr_disabled) __HAL_RCC_PWR_CLK_ENABLE();
        SET_BIT(PWR->CR, PWR_CR_CWUF);
        HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
        if (pwr_disabled) __HAL_RCC_PWR_CLK_DISABLE();

        // We configured the MCU to wake up from Stop with HSI16 enabled, thus
        // there is no need to reenable the HSI oscillator and disable the MSI
        // oscillator here.
        while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) == RESET) continue;

        __HAL_RCC_PLL_ENABLE();
        while (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) == RESET) continue;

        __HAL_RCC_SYSCLK_CONFIG(RCC_SYSCLKSOURCE_PLLCLK);
        while (__HAL_RCC_GET_SYSCLK_SOURCE() != RCC_SYSCLKSOURCE_STATUS_PLLCLK)
            continue;

        system_after_stop();
    }
}


static void init_flash(void)
{
    // Enable prefetch
    FLASH->ACR |= FLASH_ACR_PRFTEN;

    // One wait state is used to read word from NVM
    FLASH->ACR |= FLASH_ACR_LATENCY;
}


static void init_gpio(void)
{
    // Configure all GPIOs as analog to reduce power consumption by unused ports
    __GPIOA_CLK_ENABLE();
    __GPIOB_CLK_ENABLE();
    __GPIOC_CLK_ENABLE();
    __GPIOH_CLK_ENABLE();

    // gpio.Mode = GPIO_MODE_ANALOG;
    // gpio.Pull = GPIO_NOPULL;
    // /* All GPIOs except debug pins (SWCLK and SWD) */
    // gpio.Pin = GPIO_PIN_All & (~(GPIO_PIN_13 | GPIO_PIN_14));
    // HAL_GPIO_Init(GPIOA, &gpio);

    GPIOA->PUPDR   = 0x24002040;
    GPIOA->AFR[0]  = 0x00006600;
    GPIOA->AFR[1]  = 0x00000040;
    GPIOA->OTYPER  = 0x00000000;
    GPIOA->OSPEEDR = 0xcc0cc0f0;
    GPIOA->ODR     = 0x00008000;
    GPIOA->MODER   = 0x69fbafaf;

    // /* All other GPIOs */
    // gpio.Pin = GPIO_PIN_All;
    // HAL_GPIO_Init(GPIOB, &gpio);
    // HAL_GPIO_Init(GPIOC, &gpio);
    // HAL_GPIO_Init(GPIOH, &gpio);

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

    // Disable all GPIO clocks again
    __GPIOA_CLK_DISABLE();
    __GPIOB_CLK_DISABLE();
    __GPIOC_CLK_DISABLE();
    __GPIOH_CLK_DISABLE();
}


#if defined(DEBUG)
static void init_dbgmcu(void)
{
    // Note: This function is mutually-exclusive with init_facnew_gpio (they
    // share the same GPIO pin).

    // Enable the GPIO B clock
    __GPIOB_CLK_ENABLE();

    // Configure debugging GPIO pins
    GPIO_InitTypeDef gpio = {
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_HIGH,
        .Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15
    };
    HAL_GPIO_Init(GPIOB, &gpio);

    // Reset debugging pins
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);

    __DBGMCU_CLK_ENABLE();
    HAL_DBGMCU_EnableDBGSleepMode();
    HAL_DBGMCU_EnableDBGStopMode();
    HAL_DBGMCU_EnableDBGStandbyMode();
}
#endif


#if defined(RELEASE)
static void disable_swd(void)
{
    // init_gpio called before this function does not touch GPIO A 13 & 14 (SWD)
    // to keep the SWD port operational. In release mode, we re-configure the
    // two pins in analog mode to minimize power consumption.

    GPIO_InitTypeDef gpio = {
        .Mode = GPIO_MODE_ANALOG,
        .Pull = GPIO_NOPULL,
        .Pin = GPIO_PIN_13 | GPIO_PIN_14
    };

    __GPIOA_CLK_ENABLE();
    HAL_GPIO_Init(GPIOA, &gpio);
    __GPIOA_CLK_DISABLE();

    __DBGMCU_CLK_ENABLE();
    HAL_DBGMCU_DisableDBGSleepMode();
    HAL_DBGMCU_DisableDBGStopMode();
    HAL_DBGMCU_DisableDBGStandbyMode();
    __DBGMCU_CLK_DISABLE();
}


#if defined(ENABLE_FACTORY_RESET_PIN)
static Gpio_t facnew_pin = {
    .port     = GPIOB,
    .pinIndex = GPIO_PIN_15
};


static void facnew_isr(void *ctx)
{
    (void)ctx;
    static bool old = 1;
    static TimerTime_t start;

    bool new = gpio_read(facnew_pin.port, facnew_pin.pinIndex);
    TimerTime_t now = rtc_tick2ms(rtc_get_timer_value());

    if (old && !new) {
        // Falling edge. The factory reset pin was pulled down. Record the
        // timestamp of the event so that we can calculate how long the pin was
        // held down.
        start = now;
    } else if (!old && new) {
        // Rising edge. Measure how long the pin was held down. It it was held
        // down for more than five seconds, invoke factory reset.
        if (now - start > 5000)
            lrw_factory_reset(false, false);
    }

    old = new;
}


static void init_facnew_gpio(void)
{
    // Note: This function is mutually-exclusive with init_dbgmcu (they share
    // the same GPIO pin).

    __GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {
        .Mode  = GPIO_MODE_IT_RISING_FALLING,
        .Pull  = GPIO_PULLUP,
        .Speed = GPIO_SPEED_HIGH,
    };
    gpio_init(facnew_pin.port, facnew_pin.pinIndex, &gpio);
    gpio_set_irq(facnew_pin.port, facnew_pin.pinIndex, 0, facnew_isr);
}
#endif // ENABLE_FACTORY_RESET_PIN
#endif // RELEASE


static void init_clock(void)
{
    RCC_OscInitTypeDef osc = { 0 };
    RCC_ClkInitTypeDef clk = { 0 };

    // We run the modem with the system clock derived from PLL(HSI) and the RTC
    // clock derived from LSE
    osc.OscillatorType =         \
        RCC_OSCILLATORTYPE_HSE | \
        RCC_OSCILLATORTYPE_LSE | \
        RCC_OSCILLATORTYPE_HSI | \
        RCC_OSCILLATORTYPE_LSI;
    osc.HSEState = RCC_HSE_OFF;
    osc.LSEState = RCC_LSE_ON;
    osc.HSIState = RCC_HSI_ON;
    osc.LSIState = RCC_LSI_OFF;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;

    // Activate the PLL and select the HSI as its clock source
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLMUL = RCC_PLLMUL_6;
    osc.PLL.PLLDIV = RCC_PLLDIV_3;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        halt("Error while enabling HSI16 oscillator");

    // Set voltage scale1 as the MCU will run at 32MHz
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOS) != RESET);
    __HAL_RCC_PWR_CLK_DISABLE();

    // Configure the MCU to wake up from Stop mode with the HCI16 oscillator
    // enabled instead of the default MCI oscillator
    SET_BIT(RCC->CFGR, RCC_CFGR_STOPWUCK);

    // Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2
    // clocks dividers
    clk.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK)
        halt("Error while initializing system clock");

    // Now that we use PLL(HCI) as system clock, disable the MSI oscillator
    osc.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    osc.MSIState = RCC_MSI_OFF;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        halt("Error while enabling MSI oscillator");
}


void system_init(void)
{
    HAL_Init();
    init_flash();
    init_gpio();
#if defined(RELEASE)
    disable_swd();
#if defined(ENABLE_FACTORY_RESET_PIN)
    init_facnew_gpio();
#endif
#endif
#if defined(DEBUG)
    init_dbgmcu();
#endif
    init_clock();
    rtc_init();
}


void SysTick_Handler(void)
{
    HAL_IncTick();
}

__weak void system_before_stop(void)
{
}

__weak void system_after_stop(void)
{
}

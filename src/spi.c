#include "spi.h"
#include "gpio.h"
#include "error.h"
#include "io.h"

static SPI_HandleTypeDef hspi;

static uint32_t _spi_calc_divisor_for_frequency(uint32_t hz);

void spi_init(uint32_t speed)
{
    hspi.Instance = SPI1;

    hspi.Init.BaudRatePrescaler = _spi_calc_divisor_for_frequency(speed);
    hspi.Init.Direction = SPI_DIRECTION_2LINES;
    hspi.Init.Mode = SPI_MODE_MASTER;
    hspi.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi.Init.NSS = SPI_NSS_SOFT;
    hspi.Init.TIMode = SPI_TIMODE_DISABLE;

    __HAL_RCC_SPI1_CLK_ENABLE();

    if (HAL_SPI_Init(&hspi) != HAL_OK)
    {
        error_handler();
    }

    spi_io_init();
}

void spi_deinit(void)
{
    HAL_SPI_DeInit(&hspi);

    // Reset peripherals
    __HAL_RCC_SPI1_FORCE_RESET();
    __HAL_RCC_SPI1_RELEASE_RESET();

    spi_io_deinit();
}

void spi_io_init(void)
{
    GPIO_InitTypeDef initStruct = {0};

    initStruct.Mode = GPIO_MODE_AF_PP;
    initStruct.Pull = GPIO_NOPULL;
    initStruct.Speed = GPIO_SPEED_HIGH;
    initStruct.Alternate = GPIO_AF0_SPI1;

    gpio_init(RADIO_SCLK_PORT, RADIO_SCLK_PIN, &initStruct);
    gpio_init(RADIO_MOSI_PORT, RADIO_MOSI_PIN, &initStruct);

    initStruct.Pull = GPIO_PULLDOWN;
    gpio_init(RADIO_MISO_PORT, RADIO_MISO_PIN, &initStruct);

    initStruct.Mode = GPIO_MODE_OUTPUT_PP;
    initStruct.Pull = GPIO_NOPULL;

    gpio_init(RADIO_NSS_PORT, RADIO_NSS_PIN, &initStruct);
    gpio_write(RADIO_NSS_PORT, RADIO_NSS_PIN, 1);
}

void spi_io_deinit(void)
{
    GPIO_InitTypeDef initStruct = {0};

    // initStruct.Mode = GPIO_MODE_ANALOG;
    // initStruct.Pull = GPIO_NOPULL;
    // gpio_init(RADIO_MOSI_PORT, RADIO_MOSI_PIN, &initStruct);
    // gpio_init(RADIO_MISO_PORT, RADIO_MISO_PIN, &initStruct);
    // gpio_init(RADIO_SCLK_PORT, RADIO_SCLK_PIN, &initStruct);
    // // gpio_init(RADIO_NSS_PORT, RADIO_NSS_PIN, &initStruct);
    // gpio_write(RADIO_NSS_PORT, RADIO_NSS_PIN, 1);

    initStruct.Mode = GPIO_MODE_OUTPUT_PP;

    initStruct.Pull = GPIO_NOPULL;
    gpio_init(RADIO_MOSI_PORT, RADIO_MOSI_PIN, &initStruct);
    gpio_write(RADIO_MOSI_PORT, RADIO_MOSI_PIN, 0);

    initStruct.Pull = GPIO_NOPULL;
    gpio_init(RADIO_SCLK_PORT, RADIO_SCLK_PIN, &initStruct);
    gpio_write(RADIO_SCLK_PORT, RADIO_SCLK_PIN, 0);

    initStruct.Pull = GPIO_NOPULL;
    gpio_init(RADIO_NSS_PORT, RADIO_NSS_PIN, &initStruct);
    gpio_write(RADIO_NSS_PORT, RADIO_NSS_PIN, 1);

    initStruct.Mode = GPIO_MODE_INPUT;
    initStruct.Pull = GPIO_PULLDOWN;
    gpio_write(RADIO_MISO_PORT, RADIO_MISO_PIN, 0);
    gpio_init(RADIO_MISO_PORT, RADIO_MISO_PIN, &initStruct);

}

uint8_t spi_transfer(uint8_t tx)
{
    uint8_t rx;

    HAL_SPI_TransmitReceive(&hspi, &tx, &rx, 1, HAL_MAX_DELAY);

    return rx;
}

static uint32_t _spi_calc_divisor_for_frequency(uint32_t hz)
{
    uint32_t divisor = 0;
    uint32_t SysClkTmp = SystemCoreClock;
    uint32_t baudRate;

    while (SysClkTmp > hz)
    {
        divisor++;
        SysClkTmp = (SysClkTmp >> 1);

        if (divisor >= 7)
        {
            break;
        }
    }

    baudRate = (((divisor & 0x4) == 0) ? 0x0 : SPI_CR1_BR_2) |
               (((divisor & 0x2) == 0) ? 0x0 : SPI_CR1_BR_1) |
               (((divisor & 0x1) == 0) ? 0x0 : SPI_CR1_BR_0);

    return baudRate;
}

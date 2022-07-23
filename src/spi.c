#include "spi.h"
#include "halt.h"


static uint32_t calc_divisor_for_frequency(uint32_t hz)
{
    uint32_t divisor = 0;
    uint32_t SysClkTmp = SystemCoreClock;
    uint32_t baudRate;

    while (SysClkTmp > hz) {
        divisor++;
        SysClkTmp = (SysClkTmp >> 1);
        if (divisor >= 7) break;
    }

    baudRate = (((divisor & 0x4) == 0) ? 0x0 : SPI_CR1_BR_2) |
               (((divisor & 0x2) == 0) ? 0x0 : SPI_CR1_BR_1) |
               (((divisor & 0x1) == 0) ? 0x0 : SPI_CR1_BR_0);

    return baudRate;
}


void spi_init(Spi_t *spi, uint32_t speed)
{
    spi->hspi.Instance = SPI1;

    spi->hspi.Init.BaudRatePrescaler = calc_divisor_for_frequency(speed);
    spi->hspi.Init.Direction = SPI_DIRECTION_2LINES;
    spi->hspi.Init.Mode = SPI_MODE_MASTER;
    spi->hspi.Init.CLKPolarity = SPI_POLARITY_LOW;
    spi->hspi.Init.CLKPhase = SPI_PHASE_1EDGE;
    spi->hspi.Init.DataSize = SPI_DATASIZE_8BIT;
    spi->hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    spi->hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    spi->hspi.Init.NSS = SPI_NSS_SOFT;
    spi->hspi.Init.TIMode = SPI_TIMODE_DISABLE;

    spi->Nss.port = GPIOA;
    spi->Nss.pinIndex = GPIO_PIN_15;

    spi->miso.port = GPIOA;
    spi->miso.pinIndex = GPIO_PIN_6;

    spi->mosi.port = GPIOA;
    spi->mosi.pinIndex = GPIO_PIN_7;

    spi->sclk.port = GPIOB;
    spi->sclk.pinIndex = GPIO_PIN_3;

    __HAL_RCC_SPI1_CLK_ENABLE();

    if (HAL_SPI_Init(&spi->hspi) != HAL_OK)
        halt("Error while initializing SPI subsystem");

    spi_io_init(spi);

}


void spi_deinit(Spi_t *spi)
{
    HAL_SPI_DeInit(&spi->hspi);

    // Reset peripherals
    __HAL_RCC_SPI1_FORCE_RESET();
    __HAL_RCC_SPI1_RELEASE_RESET();

    spi_io_deinit(spi);
}


void spi_io_init(Spi_t *spi)
{
    GPIO_InitTypeDef cfg = {
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_HIGH,
        .Alternate = GPIO_AF0_SPI1
    };

    gpio_init(spi->sclk.port, spi->sclk.pinIndex, &cfg);
    gpio_init(spi->mosi.port, spi->mosi.pinIndex, &cfg);

    cfg.Pull = GPIO_PULLDOWN;
    gpio_init(spi->miso.port, spi->miso.pinIndex, &cfg);

    cfg.Mode = GPIO_MODE_OUTPUT_PP;
    cfg.Pull = GPIO_NOPULL;
    gpio_init(spi->Nss.port, spi->Nss.pinIndex, &cfg);
    gpio_write(spi->Nss.port, spi->Nss.pinIndex, 1);
}


void spi_io_deinit(Spi_t *spi)
{
    GPIO_InitTypeDef cfg = {
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL
    };

    gpio_init(spi->mosi.port, spi->mosi.pinIndex, &cfg);
    gpio_write(spi->mosi.port, spi->mosi.pinIndex, 0);

    gpio_init(spi->sclk.port, spi->sclk.pinIndex, &cfg);
    gpio_write(spi->sclk.port, spi->sclk.pinIndex, 0);

    gpio_init(spi->Nss.port, spi->Nss.pinIndex, &cfg);
    gpio_write(spi->Nss.port, spi->Nss.pinIndex, 1);

    cfg.Mode = GPIO_MODE_INPUT;
    cfg.Pull = GPIO_PULLDOWN;
    gpio_write(spi->miso.port, spi->miso.pinIndex, 0);
    gpio_init(spi->miso.port, spi->miso.pinIndex, &cfg);
}


uint16_t SpiInOut(Spi_t *obj, uint16_t outData)
{
    uint8_t rx, tx = outData;
    HAL_SPI_TransmitReceive(&obj->hspi, &tx, &rx, 1, HAL_MAX_DELAY);
    return rx;
}

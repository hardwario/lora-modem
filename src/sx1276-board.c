#include "sx1276-board.h"
#include <stdlib.h>
#include <loramac-node/src/radio/radio.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal.h>
#include "radio.h"


void GpioWrite(Gpio_t *obj, uint32_t value)
{
    (void)obj;
    gpio_write(RADIO_NSS_PORT, RADIO_NSS_PIN, value);
}

uint16_t SpiInOut(Spi_t *obj, uint16_t outData)
{
    (void)obj;
    return spi_transfer(outData);
}

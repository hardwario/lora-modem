#include "eeprom.h"
#include <LoRaWAN/Utilities/timeServer.h>
#include <stm/STM32L0xx_HAL_Driver/Inc/stm32l0xx_hal_flash.h>
#include "irq.h"

#define _EEPROM_BASE DATA_EEPROM_BASE
#define _EEPROM_END  DATA_EEPROM_BANK2_END
#define _EEPROM_IS_BUSY() ((FLASH->SR & FLASH_SR_BSY) != 0UL)

static bool _eeprom_is_busy(TimerTime_t timeout);
static void _eeprom_unlock(void);
static void _eeprom_lock(void);
static bool _eeprom_write(uint32_t address, size_t *i, uint8_t *buffer, size_t length);

bool eeprom_write(uint32_t address, const void *buffer, size_t length)
{
    // Add EEPROM base offset to address
    address += _EEPROM_BASE;

    // If user attempts to write outside EEPROM area...
    if ((address + length) > (_EEPROM_END + 1))
    {
        // Indicate failure
        return false;
    }

    if (_eeprom_is_busy(50))
    {
        return false;
    }

    _eeprom_unlock();

    size_t i = 0;

    while (i < length)
    {
        _eeprom_write(address, &i, (uint8_t *) buffer, length);
    }

    _eeprom_lock();

    // If we do not read what we wrote...
    if (memcmp(buffer, (void *) address, length) != 0UL)
    {
        // Indicate failure
        return false;
    }

    // Indicate success
    return true;
}

bool eeprom_read(uint32_t address, void *buffer, size_t length)
{
    // Add EEPROM base offset to address
    address += _EEPROM_BASE;

    // If user attempts to read outside of EEPROM boundary...
    if ((address + length) > (_EEPROM_END + 1))
    {
        // Indicate failure
        return false;
    }

    // Read from EEPROM memory to buffer
    memcpy(buffer, (void *) address, length);

    // Indicate success
    return true;
}

size_t eeprom_get_size(void)
{
    // Return EEPROM memory size
    return _EEPROM_END - _EEPROM_BASE + 1;
}

static bool _eeprom_is_busy(TimerTime_t timeout)
{
    timeout += TimerGetCurrentTime();

    while (_EEPROM_IS_BUSY())
    {
        if (timeout > TimerGetCurrentTime())
        {
            return true;
        }
    }

    return false;
}

static void _eeprom_unlock(void)
{
    irq_disable();

    // Unlock FLASH_PECR register
    if ((FLASH->PECR & FLASH_PECR_PELOCK) != 0)
    {
        FLASH->PEKEYR = FLASH_PEKEY1;
        FLASH->PEKEYR = FLASH_PEKEY2;
    }

    irq_enable();
}

static void _eeprom_lock(void)
{
    irq_disable();

    // Lock FLASH_PECR register
    FLASH->PECR |= FLASH_PECR_PELOCK;

    irq_enable();
}

static bool _eeprom_write(uint32_t address, size_t *i, uint8_t *buffer, size_t length)
{
    uint32_t addr = address + *i;

    uint8_t mod = addr % 4;

    bool write = false;

    if (mod == 0)
    {
        if (*i + 4 > length)
        {
            mod = (addr % 2) + 2;
        }
    }

    if (mod == 2)
    {
        if (*i + 2 > length)
        {
            mod = 1;
        }
    }

    if (mod == 0)
    {
        uint32_t value = ((uint32_t) buffer[*i + 3]) << 24 | ((uint32_t) buffer[*i + 2]) << 16 | ((uint32_t) buffer[*i + 1]) << 8 | buffer[*i];

        if (*((uint32_t *) addr) != value)
        {
            *((uint32_t *) addr) = value;

            write = true;
        }

        *i += 4;
    }
    else if (mod == 2)
    {
        uint16_t value = ((uint16_t) buffer[*i + 1]) << 8 | (uint16_t) buffer[*i];

        if (*((uint16_t *) addr) != value)
        {
            *((uint16_t *) addr) = value;

            write = true;
        }

        *i += 2;
    }
    else
    {
        uint8_t value = buffer[*i];

        if (*((uint8_t *) addr) != value)
        {
            *((uint8_t *) addr) = value;

            write = true;
        }

        *i += 1;
    }

    while (_EEPROM_IS_BUSY())
    {
        continue;
    }

    return write;
}


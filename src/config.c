
#include "config.h"
#include <LoRaWAN/Utilities/utilities.h>
#include "eeprom.h"
#include "halt.h"
#include "log.h"

#define _CONFIG_BANK_A (CONFIG_ADDRESS_START)
#define _CONFIG_BANK_B (_CONFIG_BANK_A + CONFIG_BANK_SIZE)
#define _CONFIG_BANK_C (_CONFIG_BANK_B + CONFIG_BANK_SIZE)
#define _CONFIG_BANK_D (_CONFIG_BANK_C + CONFIG_BANK_SIZE)
#define _CONFIG_BANK_E (_CONFIG_BANK_D + CONFIG_BANK_SIZE)

typedef struct
{
    void *config;
    size_t size;
    const void *init_config;

} config_t;

config_t _config;

#pragma pack(push, 1)

typedef struct
{
    uint64_t signature;
    uint16_t length;
    uint32_t crc;

} config_header_t;

#pragma pack(pop)

static void _config_eeprom_read(uint32_t address, void *buffer, size_t length);
static void _config_eeprom_write(uint32_t address, const void *buffer, size_t length);

void config_init(void *config, size_t size, const void *init_config)
{
    if (size > CONFIG_BANK_SIZE)
    {
        halt("Configuration too big for EEPROM");
    }

    _config.config = config;
    _config.size = size;
    _config.init_config = init_config;

    if (!config_load())
    {
        log_warning("Config reset");
        config_reset();
    }

    config_save();
}

void config_reset(void)
{
    if (!_config.init_config)
    {
        // Initialize to zero if init_config is not set
        memset(_config.config, 0, _config.size);
    }
    else
    {
        memcpy(_config.config, _config.init_config, _config.size);
    }
}

bool config_load(void)
{
    config_header_t header;

    _config_eeprom_read(0, &header, sizeof(config_header_t));

    if (header.signature != CONFIG_SIGNATURE || header.length != _config.size)
    {
        return false;
    }

    _config_eeprom_read(sizeof(config_header_t), _config.config, _config.size);

    if (header.crc != Crc32(_config.config, _config.size))
    {
        return false;
    }

    return true;
}

bool config_save(void)
{
    config_header_t header;

    header.signature = CONFIG_SIGNATURE;
    header.length = _config.size;
    header.crc = Crc32(_config.config, _config.size);

    _config_eeprom_write(0, &header, sizeof(config_header_t));
    _config_eeprom_write(sizeof(config_header_t), _config.config, _config.size);

    return true;
}

static void _config_eeprom_read(uint32_t address, void *buffer, size_t length)
{
    uint8_t *p = buffer;
    uint8_t a, b, c, d, e;

    for (size_t i = 0; i < length; i++)
    {

        if (!eeprom_read(_CONFIG_BANK_A + address + i, &a, 1) ||
            !eeprom_read(_CONFIG_BANK_B + address + i, &b, 1) ||
            !eeprom_read(_CONFIG_BANK_C + address + i, &c, 1) ||
            !eeprom_read(_CONFIG_BANK_D + address + i, &d, 1) ||
            !eeprom_read(_CONFIG_BANK_E + address + i, &e, 1))
        {
            halt("Error while reading EEPROM");
        }

        *p++ = (a & b) | (a & c) | (a & d) | (a & e) |
               (b & c) | (b & d) | (b & e) |
               (c & d) | (c & e) |
               (d & e);
    }
}

static void _config_eeprom_write(uint32_t address, const void *buffer, size_t length)
{
    if (!eeprom_write(_CONFIG_BANK_A + address, buffer, length) ||
        !eeprom_write(_CONFIG_BANK_B + address, buffer, length) ||
        !eeprom_write(_CONFIG_BANK_C + address, buffer, length) ||
        !eeprom_write(_CONFIG_BANK_D + address, buffer, length) ||
        !eeprom_write(_CONFIG_BANK_E + address, buffer, length))
    {
        halt("Error while writing EEPROM");
    }
}

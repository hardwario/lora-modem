#ifndef _CONFIG_H
#define _CONFIG_H

#include "common.h"

#define CONFIG_SIGNATURE 0xdeadbeef
#define CONFIG_ADDRESS_START 0
#define CONFIG_BANK_SIZE 1024

//! @brief Initialize and load the config from EEPROM
//! @param[in] config Pointer to configuration structure
//! @param[in] size Size of the configuration structure
//! @param[in] init_config Pointer to default configoration or null

void config_init(void *config, size_t size, const void *init_config);

//! @brief Reset EEPROM configuration to zeros or init_config

void config_reset(void);

//! @brief Load EEPROM configuration

bool config_load(void);

//! @brief Load EEPROM configuration

bool config_save(void);

#endif

#include "nvm.h"
#include <stm/include/stm32l072xx.h>
#include "log.h"
#include "part.h"
#include "eeprom.h"
#include "halt.h"
#include "utils.h"


// We currently store all non-volatile state in the EEPROM, so there is only one
// partitioned block that maps to the EEPROM on the STM32 platform. We export
// the variable representing the NVM block here so that subsystems like LoRaMac
// can create their own partitions in it.

part_block_t nvm = {
    .size = DATA_EEPROM_BANK2_END - DATA_EEPROM_BASE + 1,
    .mmap = eeprom_mmap,
    .write = eeprom_write
};

sysconf_t sysconf = {
    .uart_baudrate = DEFAULT_UART_BAUDRATE,
    .default_port = 1
};

static part_t sysconf_part;
bool sysconf_modified;


void nvm_init(void)
{
    // Format the EEPROM if it does not contain a partition table yet

    if (part_open_block(&nvm) != 0) {
        log_debug("Formatting EEPROM");
        if (part_format_block(&nvm, 8) != 0) halt("Could not format EEPROM");
        if (part_open_block(&nvm) != 0) halt("EEPROM I/O error");
    }

    // Create a partition for system configuration data

    if (part_find(&sysconf_part, &nvm, "sysconf") == 0) {
        size_t size;
        const uint8_t *p = part_mmap(&size, &sysconf_part);
        if (check_block_crc(p, size)) {
            log_debug("Restoring system configuration from NVM");
            memcpy(&sysconf, p, size);
        }
    } else {
        if (part_create(&sysconf_part, &nvm, "sysconf", sizeof(sysconf)))
            halt("Could not create EEPROM system config partition");
    }
}


int nvm_erase(void)
{
    // Erase the contents of the block (and all its parts) and close it
    // immediately so that further operations such as read and write would fail
    // until the block is opened and formatted again.
    int rc = part_erase_block(&nvm);
    part_close_block(&nvm);
    return rc;
}


void sysconf_process(void)
{
    if (!sysconf_modified) return;

    if (update_block_crc(&sysconf, sizeof(sysconf))) {
        log_debug("Saving system configuration to NVM");
        if (!part_write(&sysconf_part, 0, &sysconf, sizeof(sysconf)))
            log_error("Error while writing system configuration to NVM");
    }

    sysconf_modified = false;
}


#include "nvm.h"
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <stm/include/stm32l072xx.h>
#include <loramac-node/src/mac/LoRaMacTypes.h>
#include <loramac-node/src/mac/LoRaMac.h>
#include "log.h"
#include "part.h"
#include "eeprom.h"
#include "halt.h"
#include "part.h"
#include "utils.h"

#define NUMBER_OF_PARTS 9


/* The following partition sizes have been derived from the in-memory size of
 * the corresponding data structures in our fork of LoRaMac-node v4.7.0. The
 * sizes have been rounded up to leave some space for expansion in the future
 * versions. Whenever LoRaMac-node is modified, these sizes need to be
 * reevaluated to make sure that each part is large enough to hold its data
 * structure. Please note that the size of individual data structures depends,
 * among other things, on the regions compiled into the firmware.
 */
#define SYSCONF_PART_SIZE  128
#define CRYPTO_PART_SIZE   128
#define MAC1_PART_SIZE      64
#define MAC2_PART_SIZE     512
#define SE_PART_SIZE       512
#define REGION1_PART_SIZE   32
#define REGION2_PART_SIZE 1310
#define CLASSB_PART_SIZE    32
#define USER_NVM_PART_SIZE  72


// Make sure each data structure fits into its fixed-size partition
static_assert(sizeof(sysconf_t) <= SYSCONF_PART_SIZE, "system config NVM data too long");
static_assert(sizeof(LoRaMacCryptoNvmData_t) <= CRYPTO_PART_SIZE, "Crypto NVM data too long");
static_assert(sizeof(LoRaMacNvmDataGroup1_t) <= MAC1_PART_SIZE, "MacGroup1 NVM data too long");
static_assert(sizeof(LoRaMacNvmDataGroup2_t) <= MAC2_PART_SIZE, "MacGroup2 NVM data too long");
static_assert(sizeof(SecureElementNvmData_t) <= SE_PART_SIZE, "SecureElement NVM data too long");
static_assert(sizeof(RegionNvmDataGroup1_t) <= REGION1_PART_SIZE, "RegionGroup1 NVM data too long");
static_assert(sizeof(RegionNvmDataGroup2_t) <= REGION2_PART_SIZE, "RegionGroup2 NVM data too long");
static_assert(sizeof(LoRaMacClassBNvmData_t) <= CLASSB_PART_SIZE, "ClassB NVM data too long");
static_assert(sizeof(user_nvm_t) <= USER_NVM_PART_SIZE, "User NVM data too long");


// And also make sure that NVM data fits into the EEPROM twice. This is useful
// in case we wanted to implement atomic writes or data mirroring.
static_assert(
    SYSCONF_PART_SIZE +
    CRYPTO_PART_SIZE  +
    MAC1_PART_SIZE    +
    MAC2_PART_SIZE    +
    SE_PART_SIZE      +
    REGION1_PART_SIZE +
    REGION2_PART_SIZE +
    CLASSB_PART_SIZE  +
    USER_NVM_PART_SIZE
    <= (DATA_EEPROM_BANK2_END - DATA_EEPROM_BASE + 1 - PART_TABLE_SIZE(NUMBER_OF_PARTS)) / 2,
    "NVM data does not fit into a single EEPROM bank");


// We currently store all non-volatile state in the EEPROM, so there is only one
// partitioned block that maps to the EEPROM on the STM32 platform. We export
// the variable representing the NVM block here so that subsystems like LoRaMac
// can create their partitions in it.

static part_block_t nvm = {
    .size = DATA_EEPROM_BANK2_END - DATA_EEPROM_BASE + 1,
    .mmap = eeprom_mmap,
    .write = eeprom_write
};

struct nvm_parts nvm_parts;

user_nvm_t user_nvm = { 0 };

sysconf_t sysconf = {
    .uart_baudrate = DEFAULT_UART_BAUDRATE,
    .uart_timeout = 1000,
    .default_port = 2,
    .data_format = 0,
    .sleep = 1,
    .lock_keys = 0,
    .async_uart = 1,
    .device_class = CLASS_A,
    .unconfirmed_retransmissions = 1,
    .confirmed_retransmissions = 8
};

bool sysconf_modified;
uint16_t nvm_flags;


/*
 * Initialize system configuration NVM (EEPROM) partition. If necessary, the
 * function formats the EEPROM if the part is not found. If the part is found
 * but is of an invalid size; it is reformatted. If the part is found and has a
 * matching size, check the CRC32 checksum of the data before using it. If the
 * checksum does not match, use the defaults instead.
 */
void nvm_init(void)
{
    int erased = 0;

start:
    memset(&nvm_parts, 0, sizeof(nvm_parts));

    // Format the EEPROM if it does not contain a part table yet
    if (part_open_block(&nvm) != 0) {
        log_debug("Formatting EEPROM");
        if (part_format_block(&nvm, NUMBER_OF_PARTS) != 0) halt("Could not format EEPROM");
        if (part_open_block(&nvm) != 0) halt("EEPROM I/O error");
    }

    if ((part_find(&nvm_parts.sysconf, &nvm, "sysconf") &&
        part_create(&nvm_parts.sysconf, &nvm, "sysconf", SYSCONF_PART_SIZE)) ||
        nvm_parts.sysconf.dsc->size != SYSCONF_PART_SIZE)
        goto retry;

    if ((part_find(&nvm_parts.crypto, &nvm, "crypto") &&
        part_create(&nvm_parts.crypto, &nvm, "crypto", CRYPTO_PART_SIZE)) ||
        nvm_parts.crypto.dsc->size != CRYPTO_PART_SIZE)
        goto retry;

    if ((part_find(&nvm_parts.mac1, &nvm, "mac1") &&
        part_create(&nvm_parts.mac1, &nvm, "mac1", MAC1_PART_SIZE)) ||
        nvm_parts.mac1.dsc->size != MAC1_PART_SIZE)
        goto retry;

    if ((part_find(&nvm_parts.mac2, &nvm, "mac2") &&
        part_create(&nvm_parts.mac2, &nvm, "mac2", MAC2_PART_SIZE)) ||
        nvm_parts.mac2.dsc->size != MAC2_PART_SIZE)
        goto retry;

    if ((part_find(&nvm_parts.se, &nvm, "se") &&
        part_create(&nvm_parts.se, &nvm, "se", SE_PART_SIZE)) ||
        nvm_parts.se.dsc->size != SE_PART_SIZE)
        goto retry;

    if ((part_find(&nvm_parts.region1, &nvm, "region1") &&
        part_create(&nvm_parts.region1, &nvm, "region1", REGION1_PART_SIZE)) ||
        nvm_parts.region1.dsc->size != REGION1_PART_SIZE)
        goto retry;

    if ((part_find(&nvm_parts.region2, &nvm, "region2") &&
        part_create(&nvm_parts.region2, &nvm, "region2", REGION2_PART_SIZE)) ||
        nvm_parts.region2.dsc->size != REGION2_PART_SIZE)
        goto retry;

    if ((part_find(&nvm_parts.classb, &nvm, "classb") &&
        part_create(&nvm_parts.classb, &nvm, "classb", CLASSB_PART_SIZE)) ||
        nvm_parts.classb.dsc->size != CLASSB_PART_SIZE)
        goto retry;

    if ((part_find(&nvm_parts.user, &nvm, "user") &&
        part_create(&nvm_parts.user, &nvm, "user", USER_NVM_PART_SIZE)) ||
        nvm_parts.user.dsc->size != USER_NVM_PART_SIZE)
        goto retry;

    size_t size;
    const uint8_t *p = part_mmap(&size, &nvm_parts.sysconf);
    if (check_block_crc(p, sizeof(sysconf))) {
        log_debug("Restoring system configuration from NVM");
        memcpy(&sysconf, p, sizeof(sysconf));
    } else {
        log_debug("Invalid system configuration checksum, using defaults");
    }

    p = part_mmap(&size, &nvm_parts.user);
    if (check_block_crc(p, sizeof(user_nvm))) {
        log_debug("Restoring user data from NVM");
        memcpy(&user_nvm, p, sizeof(user_nvm));
    } else {
        log_debug("Invalid user data checksum, using defaults");
        user_nvm.magic = USER_NVM_MAGIC;
        bzero(user_nvm.values,USER_NVM_MAX_SIZE);
    }

    return;

retry:
    if (erased) halt("Could not initialize NVM");

    log_debug("NVM part(s) missing or invalid, erasing NVM");
    nvm_erase();
    erased = 1;

    goto start;
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
        if (!part_write(&nvm_parts.sysconf, 0, &sysconf, sizeof(sysconf)))
            log_error("Error while writing system configuration to NVM");
    }

    sysconf_modified = false;
}


void nvm_update_user_data(void)
{
    if (update_block_crc(&user_nvm, sizeof(user_nvm))) {
        log_debug("Saving user data to NVM");
        if (!part_write(&nvm_parts.user, 0, &user_nvm, sizeof(user_nvm)))
            log_error("Error while writing user data to NVM");
    }
}

#ifndef _EEPROM_H
#define _EEPROM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

//! @brief Write buffer to EEPROM area and verify it
//! @param[in] address EEPROM start address (starts at 0)
//! @param[in] buffer Pointer to source buffer
//! @param[in] length Number of bytes to be written
//! @return true On success
//! @return false On failure

bool eeprom_write(uint32_t address, const void *buffer, size_t length);

//! @brief Read buffer from EEPROM area
//! @param[in] address EEPROM start address (starts at 0)
//! @param[out] buffer Pointer to destination buffer
//! @param[in] length Number of bytes to be read
//! @return true On success
//! @return false On failure

bool eeprom_read(uint32_t address, void *buffer, size_t length);


//! @brief Return memory pointer for given EEPROM address
//! @param[in] address EEPROM start address (starts at 0)
//! @param[in] length Number of bytes to be read
//! @return Pointer to corresponding memory address
//! @return NULL On failure

const void *eeprom_mmap(uint32_t address, size_t length);


//! @brief Return size of EEPROM area
//! @return Size of EEPROM area in bytes

size_t eeprom_get_size(void);

#endif // _EEPROM_H

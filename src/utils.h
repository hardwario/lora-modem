#ifndef _UTILS_H_
#define _UTILS_H_

#include <stddef.h>
#include <stdbool.h>
#include <loramac-node/src/system/systime.h>

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

bool check_block_crc(const void *ptr, size_t size);

// Returns true if the CRC value changed
bool update_block_crc(const void *ptr, size_t size);

// Calculate the length of the string representation of the given number
unsigned int uint2strlen(uint32_t number);

// Calculate the time it takes to transmit the given number of bytes over UART
// with given baud rate
SysTime_t uart_tx_delay(unsigned speed, unsigned int bytes);

#endif // _UTILS_H_

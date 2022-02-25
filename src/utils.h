#ifndef _UTILS_H_
#define _UTILS_H_

#include <stddef.h>
#include <stdbool.h>

bool check_block_crc(const void *ptr, size_t size);

// Returns true if the CRC value changed
bool update_block_crc(const void *ptr, size_t size);

#endif // _UTILS_H_
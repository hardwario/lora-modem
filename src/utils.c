#include "utils.h"
#include <stdint.h>
#include <LoRaWAN/Utilities/utilities.h>


bool check_block_crc(const void *ptr, size_t size)
{
    uint32_t crc;
    size_t len;

    if (ptr == NULL || size < sizeof(crc)) return false;
    len = size - sizeof(crc);

    // Read the CRC value into a local variable in case the value in the buffer
    // isn't properly aligned.
    memcpy(&crc, (uint8_t *)ptr + len, sizeof(crc));

    uint32_t s = Crc32Init();

    for (unsigned int i = 0; i < len;) {
        // Crc32Update only accepts blocks up to UINT16_MAX
        s = Crc32Update(s, (uint8_t *)ptr + i, (len - i) > UINT16_MAX ? UINT16_MAX : len);
        i += (len - i) > UINT16_MAX ? UINT16_MAX : len;
    }

    return Crc32Finalize(s) == crc;
}


bool update_block_crc(const void *ptr, size_t size)
{
    uint32_t old, new;
    size_t len;

    if (ptr == NULL || size < sizeof(old)) return false;
    len = size - sizeof(old);

    memcpy(&old, (uint8_t *)ptr + len, sizeof(old));

    uint32_t s = Crc32Init();

    for (unsigned int i = 0; i < len;) {
        // Crc32Update only accepts blocks up to UINT16_MAX
        s = Crc32Update(s, (uint8_t *)ptr + i, (len - i) > UINT16_MAX ? UINT16_MAX : len);
        i += (len - i) > UINT16_MAX ? UINT16_MAX : len;
    }

    new = Crc32Finalize(s);

    if (old != new) {
        memcpy((uint8_t *)ptr + len, &new, sizeof(new));
        return true;
    }

    return false;
}
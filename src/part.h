#ifndef _PART_H_
#define _PART_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_LABEL_SIZE 16


typedef struct part_dsc {
    uint32_t start;
    uint32_t size;
    char label[MAX_LABEL_SIZE];
} part_dsc_t;


typedef struct part {
    const struct part_block *block;
    const part_dsc_t *dsc;
} part_t;


typedef struct part_table {
    uint32_t signature;  // Well known signature of the partition table
    size_t size;         // Size of the partition table including signature and the parts array that follows the partition table
    uint8_t num_parts;   // Number of partitions in the parts array
} part_table_t;


typedef struct part_block {
    const uint32_t start;       // The first memory address of the partitioned memory block
    const size_t size;          // The size of the partitioned memory block in bytes
    const part_table_t *table;  // A mmaped pointer to the partition table
    const part_dsc_t *parts;    // A mmaped pointer to the partition array
    bool (*write)(uint32_t address, const void *buffer, size_t length);
    const void *(*mmap)(uint32_t address, size_t length);
} part_block_t;


int part_format_block(part_block_t *block, unsigned int max_parts);
int part_open_block(part_block_t *block);
int part_erase_block(part_block_t *block);

int part_find(part_t *part, const part_block_t *block, const char *label);
int part_create(part_t *part, const part_block_t *block, const char *label, size_t size);

bool part_write(const part_t *part, uint32_t address, const void *buffer, size_t length);
const void *part_mmap(size_t *size, const part_t *part);

void part_dump_block(part_block_t *block);

#endif // _PART_H_
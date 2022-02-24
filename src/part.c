#include "part.h"
#include <string.h>
#include "log.h"

#define PART_BLOCK_SIGNATURE ((uint32_t)0x1ABE11ED)

#define ALIGNMENT 4
#define ALIGN(v) (((v) + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT)

#define FIXED_PART_TABLE_SIZE (ALIGN(sizeof(part_table_t)))

#define MAX_PARTS(t) (((t)->size - FIXED_PART_TABLE_SIZE) / sizeof(part_dsc_t))


int part_erase_block(part_block_t *block)
{
    if (block == NULL || block->size < FIXED_PART_TABLE_SIZE) return -1;

    log_debug("part: Erasing block %p (%d B)", (void *)block, block->size);
    uint32_t sig = 0xffffffff;
    block->write(block->start, &sig, sizeof(sig));
    return 0;
}


int part_format_block(part_block_t *block, unsigned int max_parts)
{
    if (block == NULL || block->mmap == NULL) return -1;
    if (block->size < FIXED_PART_TABLE_SIZE) return -2;

    const part_table_t *t = block->mmap(block->start, sizeof(*t));
    if (t == NULL) return -3;

    if (t->signature == PART_BLOCK_SIGNATURE)
        return -4; // The memory segment appears to contain a partition table already

    part_table_t nt = {
        .signature = PART_BLOCK_SIGNATURE,
        .size = FIXED_PART_TABLE_SIZE + sizeof(part_dsc_t) * max_parts,
        .num_parts = 0
    };

    log_debug("part: Formatting block %p (%d B), max parts: %d",
        (void *)block, block->size, MAX_PARTS(&nt));

    if (!block->write(block->start, &nt, sizeof(nt))) return -5;
    return 0;
}


int part_open_block(part_block_t *block)
{
    if (block == NULL || block->mmap == NULL) return -1;
    if (block->size < FIXED_PART_TABLE_SIZE) return -2;

    const part_table_t *t = block->mmap(block->start, sizeof(*t));
    if (t == NULL) return -3;

    // The partition table must have a known signature
    if (t->signature != PART_BLOCK_SIGNATURE)
        return -4; // Unrecognized partition table

    // Make sure the block is large enough to fit at least the fixed portion of
    // the partition table.
    if (t->size < FIXED_PART_TABLE_SIZE)
        return -5; // Invalid partition table

    // Mmap the entire partition table including the array of partitions that
    // follows the partition table
    t = block->mmap(block->start, t->size);
    if (t == NULL) return -6;

    if (t->num_parts > MAX_PARTS(t))
        return -6; // The partition table appears to be corrupted

    block->table = (part_table_t *)t;
    block->parts = (part_dsc_t *)((char *)t + FIXED_PART_TABLE_SIZE);

    log_debug("part: Opened block %p (%d B), %d parts of %d",
        (void *)block, block->size, t->num_parts, MAX_PARTS(t));
    return 0;
}


int part_find(part_t *part, const part_block_t *block, const char *label)
{
    if (part == NULL || block == NULL || label == NULL ||
        block->table == NULL || block->parts == NULL)
        return -1;

    size_t len = strlen(label);
    if (len >= MAX_LABEL_SIZE) return -2;

    for (int i = 0; i < block->table->num_parts; i++) {
        if (memcmp(block->parts[i].label, label, len)) continue;
        part->block = block;
        part->dsc = block->parts + i;
        return 0;
    }

    return 1;
}


int part_create(part_t *part, const part_block_t *block, const char *label, size_t size)
{
    // Make sure the block object has all the fields that we are going to need
    if (part == NULL || block == NULL || label == NULL || block->write == NULL ||
        block->table == NULL || block->parts == NULL)
        return -1;

    // Check that all the attributes in the partition table have sane values
    size_t len = strlen(label);
    if (len >= MAX_LABEL_SIZE) return -2;

    if (block->table->num_parts >= MAX_PARTS(block->table))
        return -3;

    // Calculate the offset of the first aligned byte where a new partition can
    // start. The new partition will only be created following the current last
    // partition.

    uint32_t first_aligned_byte;

    if (block->table->num_parts > 0) {
        const part_dsc_t *last_part = &block->parts[block->table->num_parts - 1];
        first_aligned_byte = ALIGN(last_part->start + last_part->size);
    } else {
        first_aligned_byte = ALIGN(block->table->size);
    }

    // Make sure that there is enough space in the block for the new partition
    if (first_aligned_byte + size > block->size)
        return -4;

    // Create a new partition record structure and write it into the
    // corresponding place in the array of partitions.
    part_dsc_t p = {
        .start = first_aligned_byte,
        .size = size
    };
    memcpy(p.label, label, len + 1);
    if (!block->write(block->start + FIXED_PART_TABLE_SIZE + block->table->num_parts * sizeof(part_dsc_t),
        &p, sizeof(p)))
        return -5;

    part_table_t table;
    memcpy(&table, block->table, sizeof(part_table_t));
    table.num_parts++;
    if (!block->write(block->start, &table, sizeof(table)))
        return -6;

    log_debug("part: Created part '%s' in block %p starting at offset %ld (%d B)",
        label, (void *)block, first_aligned_byte, size);

    part->block = block;
    part->dsc = block->parts + table.num_parts - 1;
    return 0;
}


void part_dump_block(part_block_t *block)
{
    log_debug("part: Block %p (%d B), %d parts of %d", (void *)block, block->size,
        block->table->num_parts, MAX_PARTS(block->table));

    for (int i = 0; i < block->table->num_parts; i++) {
        log_debug("part:   Part '%s' at offset %ld (%ld B)", block->parts[i].label,
            block->parts[i].start, block->parts[i].size);
    }
}


bool part_write(const part_t *part, uint32_t address, const void *buffer, size_t length)
{
    if (address + length > part->dsc->size) return false;
    return part->block->write(part->dsc->start + address, buffer, length);
}


const void *part_mmap(size_t *size, const part_t *part)
{
    *size = part->dsc->size;
    return part->block->mmap(part->dsc->start, part->dsc->size);
}

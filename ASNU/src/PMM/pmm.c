#include "pmm.h"

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
} BitmapFile;

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t flags;
    uint32_t reserved;
    uint64_t bitmap_base;
    uint64_t bitmap_length;
    BitmapFile *bitmap_file;
} PmmFile;

PmmFile pmm_file_ins;
PmmFile* pmm_file = &pmm_file_ins;

BitmapFile bitmap_file_ins;
BitmapFile* bitmap_file = &bitmap_file_ins;

void pmm_init(uint64_t largest_base, uint64_t largest_length, uint64_t second_largest_base, uint64_t second_largest_length, uint64_t total_memory, uint64_t total_usable_memory, uint64_t total_reserved_memory) {
    pmm_file->base = largest_base;
    pmm_file->length = largest_length;
    pmm_file->type = 0;
    pmm_file->flags = 0;
    pmm_file->reserved = 0;
    pmm_file->bitmap_base = second_largest_base;
    pmm_file->bitmap_length = second_largest_length;
    bitmap_file->base = second_largest_base;
    bitmap_file->length = second_largest_length;
    bitmap_file->type = 0;
    pmm_file->bitmap_file = bitmap_file;

    uint64_t bitmap_size = (largest_length / 4096) / 8;
    pmm_file->bitmap_file->length = bitmap_size;
    pmm_file->bitmap_length = bitmap_size;

    memset((void*)pmm_file->bitmap_base, 0, bitmap_size);
}

void BitmapSetBit(uint64_t index) {
    if (index >= pmm_file->bitmap_length * 8) {
        return; // Out of bounds
    }
    uint64_t byte_index = index / 8;
    uint8_t bit_index = index % 8;
    ((uint8_t*)pmm_file->bitmap_base)[byte_index] |= (1 << bit_index);
}

void BitmapClearBit(uint64_t index) {
    if (index >= pmm_file->bitmap_length * 8) {
        return; // Out of bounds
    }
    uint64_t byte_index = index / 8;
    uint8_t bit_index = index % 8;
    ((uint8_t*)pmm_file->bitmap_base)[byte_index] &= ~(1 << bit_index);
}

void* kalloc(size_t size) {
    if (size == 0 || size > pmm_file->bitmap_length * 8) {
        return NULL; // Invalid size
    }

    for (uint64_t i = 0; i < pmm_file->bitmap_length * 8; i++) {
        if (!((uint8_t*)pmm_file->bitmap_base)[i / 8] & (1 << (i % 8))) {
            BitmapSetBit(i);
            return (void*)(pmm_file->base + (i * 4096));
        }
    }
    return NULL; // No free memory found
}

void kfree(void* ptr) {
    if (ptr < (void*)pmm_file->base || ptr >= (void*)(pmm_file->base + pmm_file->length)) {
        return; // Pointer out of bounds
    }

    uint64_t offset = (uint64_t)ptr - pmm_file->base;
    uint64_t index = offset / 4096;

    BitmapClearBit(index);
}
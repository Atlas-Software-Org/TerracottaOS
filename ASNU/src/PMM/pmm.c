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

#define HHDM_BASE 0xFFFF800000000000ULL
#define HHDM_LIMIT 0xFFFFFFFFFFFFFFFFULL

static inline void* PhysToVirt(uint64_t paddr) {
    if (paddr >= HHDM_BASE && paddr <= HHDM_LIMIT)
        return (void*)paddr;

    return (void*)(HHDM_BASE + paddr);
}

static inline uint64_t VirtToPhys(const void* vaddr) {
    uint64_t addr = (uint64_t)vaddr;

    if (addr < HHDM_BASE)
        return addr;

    if (addr >= HHDM_BASE && addr <= HHDM_LIMIT)
        return addr - HHDM_BASE;

    return 0;
}

PmmFile pmm_file_ins;
PmmFile* pmm_file = &pmm_file_ins;

BitmapFile bitmap_file_ins;
BitmapFile* bitmap_file = &bitmap_file_ins;

void pmm_init(int pmm_virt_mem, uint64_t largest_base, uint64_t largest_length, uint64_t second_largest_base, uint64_t second_largest_length, uint64_t total_memory, uint64_t total_usable_memory, uint64_t total_reserved_memory) {
    serial_fwrite("Initializing Physical Memory Manager with the following parameters:");
    serial_fwrite("PMM Virtual Memory: %d", pmm_virt_mem);
    serial_fwrite("Largest Base: %p", (void*)largest_base);
    serial_fwrite("Largest Length: %llu", largest_length);
    serial_fwrite("Second Largest Base: %p", (void*)second_largest_base);
    serial_fwrite("Second Largest Length: %llu", second_largest_length);
    serial_fwrite("Total Memory: %llu", total_memory);
    serial_fwrite("Total Usable Memory: %llu", total_usable_memory);
    serial_fwrite("Total Reserved Memory: %llu", total_reserved_memory);
    serial_fwrite("Initializing PMM structures");
    uint64_t p_base = pmm_virt_mem ? VirtToPhys((void*)largest_base) : largest_base;
    uint64_t b_base = pmm_virt_mem ? VirtToPhys((void*)second_largest_base) : second_largest_base;
    serial_fwrite("Set p_base and b_pase");

    serial_fwrite("Setting up PMM file structure");
    pmm_file->base = p_base;
    pmm_file->length = largest_length;
    pmm_file->type = 0;
    pmm_file->flags = 0;
    pmm_file->reserved = 0;
    serial_fwrite("PMM file structure set");

    serial_fwrite("Setting up bitmap file structure");
    bitmap_file->base = b_base;
    bitmap_file->length = second_largest_length;
    bitmap_file->type = 0;
    pmm_file->bitmap_file = bitmap_file;
    serial_fwrite("Bitmap file structure set");

    serial_fwrite("Calculating bitmap size");
    uint64_t bitmap_size = ((largest_length / 4096) + 7) / 8;
    pmm_file->bitmap_length = bitmap_size; // in bytes
    memset((void*)PhysToVirt(pmm_file->bitmap_base), 0, bitmap_size);
    pmm_file->bitmap_base = b_base;
    pmm_file->bitmap_file->length = bitmap_size;
    serial_fwrite("Bitmap size calculated: %llu bytes", bitmap_size);

    memset(second_largest_base, 0, second_largest_length);
    memset(PhysToVirt(pmm_file->bitmap_base), 0, pmm_file->bitmap_length);
}

void BitmapSetBit(uint64_t index) {
    if (index >= pmm_file->bitmap_length * 8) return;
    uint64_t byte_index = index / 8;
    uint8_t bit_index = index % 8;
    ((uint8_t*)PhysToVirt(pmm_file->bitmap_base))[byte_index] |= (uint8_t)(1u << bit_index);
}

void BitmapClearBit(uint64_t index) {
    if (index >= pmm_file->bitmap_length * 8) return;
    uint64_t byte_index = index / 8;
    uint8_t bit_index = index % 8;
    ((uint8_t*)PhysToVirt(pmm_file->bitmap_base))[byte_index] &= (uint8_t)~(1u << bit_index);
}

void* kalloc(size_t size) {
    if (size == 0) return NULL;
    uint64_t pages_needed = (size + 4095) / 4096;

    uint8_t* bm = (uint8_t*)PhysToVirt(pmm_file->bitmap_base);
    for (uint64_t i = 0; i < pmm_file->bitmap_length * 8; i++) {
        uint64_t byte_index = i / 8;
        uint8_t bit_index = i % 8;
        if ((bm[byte_index] & (uint8_t)(1u << bit_index)) == 0) {
            bm[byte_index] |= (uint8_t)(1u << bit_index);
            return PhysToVirt(pmm_file->base + (i * 4096));
        }
    }
    return NULL;
}

void* palloc() {
    uint64_t index = UINT64_MAX;
    uint8_t* bm = (uint8_t*)PhysToVirt(pmm_file->bitmap_base);
    for (uint64_t i = 0; i < pmm_file->bitmap_length * 8; i++) {
        uint64_t byte_index = i / 8;
        uint8_t bit_index = i % 8;
        if ((bm[byte_index] & (uint8_t)(1u << bit_index)) == 0) {
            bm[byte_index] |= (uint8_t)(1u << bit_index);
            index = i;
            break;
        }
    }

    if (index == UINT64_MAX) return NULL; // No free pages found
    return PhysToVirt(pmm_file->base + (index * 4096)); // use index, not i
}

void kfree(void* ptr) {
    uint64_t phys = VirtToPhys(ptr);
    if (phys < pmm_file->base || phys >= pmm_file->base + pmm_file->length) return;
    uint64_t index = (phys - pmm_file->base) / 4096;
    BitmapClearBit(index);
}

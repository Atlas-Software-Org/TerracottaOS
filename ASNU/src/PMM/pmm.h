#ifndef PMM_H
#define PMM_H 1

#include <stdint.h>
#include <stddef.h>
#include <KiSimple.h>
#include <Serial/serial.h>
#include <string.h>

void pmm_init(uint64_t largest_base, uint64_t largest_length, uint64_t second_largest_base, uint64_t second_largest_length, uint64_t total_memory, uint64_t total_usable_memory, uint64_t total_reserved_memory);
void* kalloc(size_t size);
void kfree(void* ptr);

#endif /* PMM_H */
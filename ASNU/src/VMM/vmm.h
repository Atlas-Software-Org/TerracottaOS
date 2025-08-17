#ifndef VMM_H
#define VMM_H 1

#include <stdint.h>
#include <stdbool.h>
#include <PMM/pmm.h>

void vmm_init();

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_USER    0x4

void mmap(void* vaddr, void* paddr, uint64_t flags);
void unmap(void* vaddr);

#endif /* VMM_H */
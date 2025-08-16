#ifndef VMM_H
#define VMM_H 1

#include <stdint.h>
#include <stdbool.h>
#include <PMM/pmm.h>

typedef struct {
    uint64_t Entries[512];
} PageTable;

typedef enum {
    VMM_SUCCESS = 0,
    VMM_ERR_NULL = 1,
    VMM_ERR_NO_MEM = 2,
    VMM_ERR_INVALID_PML4 = 3,
    VMM_ERR_INVALID_PDPT = 4,
    VMM_ERR_INVALID_PD = 5,
    VMM_ERR_ALREADY_MAPPED = 6,
    VMM_ERR_NOT_MAPPED = 7
} VmmResult;

#define PAGE_PRESENT     0x001
#define PAGE_RW          0x002
#define PAGE_USER        0x004
#define PAGE_PSE         0x080
#define PAGE_ADDR_MASK   0xFFFFFFFFF000ULL

#define MMAP_PRESENT    (1ULL << 0)   // Page is present
#define MMAP_RW         (1ULL << 1)   // Read/write (1 = writeable)
#define MMAP_USER       (1ULL << 2)   // User-accessible page

#define MMAP_PWT        (1ULL << 3)   // Page Write-Through
#define MMAP_PCD        (1ULL << 4)   // Page Cache Disable

#define MMAP_ACCESSED   (1ULL << 5)   // Set by CPU when accessed
#define MMAP_DIRTY      (1ULL << 6)   // Set by CPU when written (PTE only)

#define MMAP_HUGE       (1ULL << 7)   // Set on PDE for 2MiB/1GiB pages

#define MMAP_GLOBAL     (1ULL << 8)   // Global mapping (not flushed on CR3 switch)

#define MMAP_SOFT0      (1ULL << 9)
#define MMAP_SOFT1      (1ULL << 10)
#define MMAP_SOFT2      (1ULL << 11)

#define MMAP_NX         (1ULL << 63)  // No-execute

VmmResult mmap(void* vaddr, void* paddr, uint64_t attr);
VmmResult unmap(void* vaddr);

#endif /* VMM_H */
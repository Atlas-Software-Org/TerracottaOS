#include "vmm.h"

#define HHDM_BASE 0xFFFF800000000000ULL
#define HHDM_LIMIT 0xFFFFFFFFFFFFFFFFULL

#define HHDM_BASE 0xFFFF800000000000ULL
#define HHDM_LIMIT 0xFFFFFFFFFFFFFFFFULL

static inline void* PHYS_TO_VIRT(uint64_t paddr) {
    if (paddr >= HHDM_BASE && paddr <= HHDM_LIMIT)
        return (void*)paddr;

    return (void*)(HHDM_BASE + paddr);
}

static inline uint64_t VIRT_TO_PHYS(const void* vaddr) {
    uint64_t addr = (uint64_t)vaddr;

    if (addr < HHDM_BASE)
        return addr;

    if (addr >= HHDM_BASE && addr <= HHDM_LIMIT)
        return addr - HHDM_BASE;

    return 0;
}

static inline PageTable* vmm_get_or_create_table(uint64_t entry, bool* created) {
    if (!(entry & PAGE_PRESENT)) {
        uint64_t new_page = palloc();
        if (created) *created = true;
        return (PageTable*)PHYS_TO_VIRT(new_page);
    }
    return (PageTable*)PHYS_TO_VIRT(entry & PAGE_ADDR_MASK);
}

VmmResult mmap(void* vaddr, void* paddr, uint64_t attr) {
    if (!vaddr || !paddr) return VMM_ERR_NULL;

    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    PageTable* pml4 = (PageTable*)PHYS_TO_VIRT(cr3 & PAGE_ADDR_MASK);
    if (!pml4) return VMM_ERR_INVALID_PML4;

    uint64_t vaddrU64 = (uint64_t)vaddr;
    uint64_t paddrU64 = (uint64_t)paddr;

    uint16_t pml4_index = (vaddrU64 >> 39) & 0x1FF;
    uint16_t pdpt_index = (vaddrU64 >> 30) & 0x1FF;
    uint16_t pd_index   = (vaddrU64 >> 21) & 0x1FF;
    uint16_t pt_index   = (vaddrU64 >> 12) & 0x1FF;

    bool created = false;

    PageTable* pdpt = vmm_get_or_create_table(pml4->Entries[pml4_index], &created);
    if (!pdpt) return VMM_ERR_NO_MEM;
    if (created) pml4->Entries[pml4_index] = VIRT_TO_PHYS(pdpt) | PAGE_PRESENT | PAGE_RW;

    PageTable* pd = vmm_get_or_create_table(pdpt->Entries[pdpt_index], &created);
    if (!pd) return VMM_ERR_NO_MEM;
    if (created) pdpt->Entries[pdpt_index] = VIRT_TO_PHYS(pd) | PAGE_PRESENT | PAGE_RW;

    PageTable* pt = vmm_get_or_create_table(pd->Entries[pd_index], &created);
    if (!pt) return VMM_ERR_NO_MEM;
    if (created) pd->Entries[pd_index] = VIRT_TO_PHYS(pt) | PAGE_PRESENT | PAGE_RW;

    if (pt->Entries[pt_index] & PAGE_PRESENT)
        return VMM_ERR_ALREADY_MAPPED;

    pt->Entries[pt_index] = (paddrU64 & PAGE_ADDR_MASK) | (attr & 0xFFF0000000000FFFULL) | PAGE_PRESENT | PAGE_RW;
    asm volatile ("invlpg (%0)" :: "r" (vaddr) : "memory");

    return VMM_SUCCESS;
}

VmmResult unmap(void* vaddr) {
    if (!vaddr) return VMM_ERR_NULL;

    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    PageTable* pml4 = (PageTable*)PHYS_TO_VIRT(cr3 & PAGE_ADDR_MASK);
    if (!pml4) return VMM_ERR_INVALID_PML4;

    uint64_t _vaddr = (uint64_t)vaddr;
    uint16_t pml4_index = (_vaddr >> 39) & 0x1FF;
    uint16_t pdpt_index = (_vaddr >> 30) & 0x1FF;
    uint16_t pd_index   = (_vaddr >> 21) & 0x1FF;
    uint16_t pt_index   = (_vaddr >> 12) & 0x1FF;

    if (!(pml4->Entries[pml4_index] & PAGE_PRESENT)) return VMM_ERR_INVALID_PML4;
    PageTable* pdpt = (PageTable*)PHYS_TO_VIRT(pml4->Entries[pml4_index] & PAGE_ADDR_MASK);

    if (!(pdpt->Entries[pdpt_index] & PAGE_PRESENT)) return VMM_ERR_INVALID_PDPT;
    PageTable* pd = (PageTable*)PHYS_TO_VIRT(pdpt->Entries[pdpt_index] & PAGE_ADDR_MASK);

    if (!(pd->Entries[pd_index] & PAGE_PRESENT)) return VMM_ERR_INVALID_PD;
    PageTable* pt = (PageTable*)PHYS_TO_VIRT(pd->Entries[pd_index] & PAGE_ADDR_MASK);

    if (!(pt->Entries[pt_index] & PAGE_PRESENT)) return VMM_ERR_NOT_MAPPED;
    pt->Entries[pt_index] &= ~PAGE_PRESENT;

    asm volatile ("invlpg (%0)" :: "r" (vaddr) : "memory");
    return VMM_SUCCESS;
}
#include "vmm.h"
#include <KiSimple.h>
#include <PMM/pmm.h>
#include <string.h>

void* PML4;

uint64_t cr3_phys;
uint64_t cr3_virt;

void vmm_init() {
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_phys));
    cr3_virt = (uint64_t)PA2VA(cr3_phys);

    PML4 = palloc();
    memcpy(PML4, (void*)cr3_virt, 4096);

    __asm__ volatile("mov %0, %%cr3" : : "r"(VA2PA(PML4)));
}

void mmap(void* vaddr, void* paddr, uint64_t flags) {
    uint64_t va = (uint64_t)vaddr;
    uint64_t pa = (uint64_t)paddr;

    uint64_t* pml4 = (uint64_t*)PML4;

    uint64_t pml4_index = (va >> 39) & 0x1FF;
    uint64_t pdpt_index = (va >> 30) & 0x1FF;
    uint64_t pd_index   = (va >> 21) & 0x1FF;
    uint64_t pt_index   = (va >> 12) & 0x1FF;

    uint64_t* pdpt;
    if (!(pml4[pml4_index] & PAGE_PRESENT)) {
        pdpt = (uint64_t*)palloc();
        memset(pdpt, 0, 4096);
        pml4[pml4_index] = VA2PAu64(pdpt) | PAGE_PRESENT | PAGE_RW;
    } else {
        pdpt = (uint64_t*)PA2VAu64(pml4[pml4_index] & ~0xFFF);
    }

    uint64_t* pd;
    if (!(pdpt[pdpt_index] & PAGE_PRESENT)) {
        pd = (uint64_t*)palloc();
        memset(pd, 0, 4096);
        pdpt[pdpt_index] = VA2PAu64(pd) | PAGE_PRESENT | PAGE_RW;
    } else {
        pd = (uint64_t*)PA2VAu64(pdpt[pdpt_index] & ~0xFFF);
    }

    uint64_t* pt;
    if (!(pd[pd_index] & PAGE_PRESENT)) {
        pt = (uint64_t*)palloc();
        memset(pt, 0, 4096);
        pd[pd_index] = VA2PAu64(pt) | PAGE_PRESENT | PAGE_RW;
    } else {
        pt = (uint64_t*)PA2VAu64(pd[pd_index] & ~0xFFF);
    }

    pt[pt_index] = 0;

    pt[pt_index] = (pa & ~0xFFFUL) | (flags & 0xFFF) | PAGE_PRESENT;
}

void unmap(void* vaddr) {
    uint64_t va = (uint64_t)vaddr;

    uint64_t* pml4 = (uint64_t*)PML4;
    uint64_t pml4_index = (va >> 39) & 0x1FF;
    uint64_t pdpt_index = (va >> 30) & 0x1FF;
    uint64_t pd_index   = (va >> 21) & 0x1FF;
    uint64_t pt_index   = (va >> 12) & 0x1FF;

    if (!(pml4[pml4_index] & PAGE_PRESENT)) return;
    uint64_t* pdpt = (uint64_t*)PA2VAu64(pml4[pml4_index] & ~0xFFF);
    if (!(pdpt[pdpt_index] & PAGE_PRESENT)) return;
    uint64_t* pd = (uint64_t*)PA2VAu64(pdpt[pdpt_index] & ~0xFFF);
    if (!(pd[pd_index] & PAGE_PRESENT)) return;
    uint64_t* pt = (uint64_t*)PA2VAu64(pd[pd_index] & ~0xFFF);

    pt[pt_index] = 0;

    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

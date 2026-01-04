#include <mem/mem.hpp>
#include <cstdio>

uint64_t original_PML4 = 0;
uint64_t default_PML4 = 0;
uint64_t current_PML4 = 0;

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_USER    0x4

namespace mem::vmm {

static inline void invlpg(uint64_t va) {
    asm volatile("invlpg (%0)" :: "r"(va) : "memory");
}

static inline uint64_t get_pml4_index(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline uint64_t get_pdpt_index(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline uint64_t get_pd_index(uint64_t va)   { return (va >> 21) & 0x1FF; }
static inline uint64_t get_pt_index(uint64_t va)   { return (va >> 12) & 0x1FF; }

static uint64_t* ensure_table_exists(uint64_t* parent, uint64_t index, bool user) {
    if (parent[index] & PAGE_PRESENT) {
        uint64_t* table = reinterpret_cast<uint64_t*>(pa_to_va(parent[index] & ~0xFFF));
        if (user && !(parent[index] & PAGE_USER)) {
            parent[index] |= PAGE_USER;
        }
        return table;
    }
    
    uint64_t* new_table = reinterpret_cast<uint64_t*>(valloc(1));
    mem::memset(new_table, 0, 0x1000);
    
    uint64_t flags = PAGE_PRESENT | PAGE_RW;
    if (user) {
        flags |= PAGE_USER;
    }
    
    parent[index] = va_to_pa(reinterpret_cast<uint64_t>(new_table)) | flags;
    
    return new_table;
}

void* create_pagetable() {
    void* page = mem::pmm::palloc(1);
    if (!page) return nullptr;

    uint64_t va_page = pa_to_va(reinterpret_cast<uint64_t>(page));
    mem::memset(reinterpret_cast<void*>(va_page), 0, 0x1000);

    uint64_t* new_pml4 = reinterpret_cast<uint64_t*>(va_page);
    uint64_t* orig_pml4 = reinterpret_cast<uint64_t*>(original_PML4);

    for (int i = 256; i < 512; i++) {
        new_pml4[i] = orig_pml4[i];
    }

    return page;
}

void destroy_pagetable(void* pml4_ptr) {
    if (current_PML4 == reinterpret_cast<uint64_t>(pml4_ptr)) {
        reset_pagetable();
    }
    mem::pmm::free(pml4_ptr, 1);
}

uint64_t fetch_default_pagetable() {
    return default_PML4;
}

uint64_t pa_to_va(uint64_t pa) {
    return (pa >= 0xFFFF800000000000) ? pa : pa + 0xFFFF800000000000;
}

uint64_t va_to_pa(uint64_t va) {
    return (va < 0xFFFF800000000000) ? va : va - 0xFFFF800000000000;
}

void initialise() {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    
    original_PML4 = pa_to_va(cr3);
    default_PML4 = original_PML4;
    current_PML4 = original_PML4;
}

void print_mem() {}

void* valloc(size_t npages) {
    void* page = mem::pmm::palloc(npages);
    return reinterpret_cast<void*>(pa_to_va(reinterpret_cast<uint64_t>(page)));
}

void free(void* ptr, size_t npages) {
    uint64_t phys = va_to_pa(reinterpret_cast<uint64_t>(ptr));
    mem::pmm::free(reinterpret_cast<void*>(phys), npages);
}

bool is_mapped(void* vaddr) {
    uint64_t va = reinterpret_cast<uint64_t>(vaddr);
    uint64_t* pml4 = reinterpret_cast<uint64_t*>(current_PML4);
    
    uint64_t pml4_entry = pml4[get_pml4_index(va)];
    if (!(pml4_entry & PAGE_PRESENT)) return false;
    
    uint64_t* pdpt = reinterpret_cast<uint64_t*>(pa_to_va(pml4_entry & ~0xFFF));
    uint64_t pdpt_entry = pdpt[get_pdpt_index(va)];
    if (!(pdpt_entry & PAGE_PRESENT)) return false;
    
    uint64_t* pd = reinterpret_cast<uint64_t*>(pa_to_va(pdpt_entry & ~0xFFF));
    uint64_t pd_entry = pd[get_pd_index(va)];
    if (!(pd_entry & PAGE_PRESENT)) return false;
    
    uint64_t* pt = reinterpret_cast<uint64_t*>(pa_to_va(pd_entry & ~0xFFF));
    return (pt[get_pt_index(va)] & PAGE_PRESENT) != 0;
}

uint64_t mmap(void* paddr, void* vaddr, size_t npages, uint64_t attributes) {
    printf("mmap(%p, %p, %zu, %zu);\n\r", paddr, vaddr, npages, attributes);
    uint64_t va = reinterpret_cast<uint64_t>(vaddr);
    uint64_t pa = reinterpret_cast<uint64_t>(paddr);
    uint64_t first_entry = 0;

    bool is_user = (va < 0x0000800000000000ULL);

    for (size_t i = 0; i < npages; i++, va += 0x1000, pa += 0x1000) {
        uint64_t* pml4 = reinterpret_cast<uint64_t*>(current_PML4);
        uint64_t pml4_idx = get_pml4_index(va);

        uint64_t* pdpt = ensure_table_exists(pml4, pml4_idx, is_user);
        uint64_t* pd   = ensure_table_exists(pdpt, get_pdpt_index(va), is_user);
        uint64_t* pt   = ensure_table_exists(pd, get_pd_index(va), is_user);

        uint64_t flags = PAGE_PRESENT | PAGE_RW;
        if (is_user) {
            flags |= PAGE_USER;
        }

        pt[get_pt_index(va)] = (pa & ~0xFFF) | flags;

        if (i == 0) first_entry = pt[get_pt_index(va)];

        invlpg(va);
    }

    return first_entry;
}

void munmap(void* vaddr, size_t npages) {
    uint64_t va = reinterpret_cast<uint64_t>(vaddr);

    for (size_t i = 0; i < npages; i++, va += 0x1000) {
        uint64_t* pml4 = reinterpret_cast<uint64_t*>(current_PML4);
        uint64_t pml4_entry = pml4[get_pml4_index(va)];
        if (!(pml4_entry & PAGE_PRESENT)) continue;

        uint64_t* pdpt = reinterpret_cast<uint64_t*>(pa_to_va(pml4_entry & ~0xFFF));
        uint64_t pdpt_entry = pdpt[get_pdpt_index(va)];
        if (!(pdpt_entry & PAGE_PRESENT)) continue;

        uint64_t* pd = reinterpret_cast<uint64_t*>(pa_to_va(pdpt_entry & ~0xFFF));
        uint64_t pd_entry = pd[get_pd_index(va)];
        if (!(pd_entry & PAGE_PRESENT)) continue;

        uint64_t* pt = reinterpret_cast<uint64_t*>(pa_to_va(pd_entry & ~0xFFF));
        pt[get_pt_index(va)] = 0;
        
        invlpg(va);
    }
}

void switch_pagetable(uint64_t ptr) {
    uint64_t phys = va_to_pa(ptr);
    asm volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
    current_PML4 = ptr;
}

void reset_pagetable() {
    switch_pagetable(default_PML4);
}

}

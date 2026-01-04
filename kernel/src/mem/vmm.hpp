#ifndef VMM_HPP
#define VMM_HPP 1

#include <mem/mem.hpp>

#include <cstddef>

namespace mem::vmm {

void* create_pagetable();
void destroy_pagetable(void* pml4_ptr);
void switch_pagetable(uint64_t ptr);
void reset_pagetable();
uint64_t fetch_default_pagetable();

uint64_t pa_to_va(uint64_t pa);
uint64_t va_to_pa(uint64_t va);
		
void initialise();
void print_mem();
void* valloc(size_t npages);
void free(void* ptr, size_t npages);
uint64_t mmap(void* paddr, void* vaddr, size_t npages, uint64_t attributes);
void munmap(void* vaddr, size_t npages);

bool is_mapped(void* vaddr);

}

#endif /* VMM_HPP */

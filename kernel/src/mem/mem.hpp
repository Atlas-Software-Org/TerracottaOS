#ifndef MEM_HPP
#define MEM_HPP 1

#include <cstdint>
#include <cstddef>

#include <mem/pmm.hpp>
#include <mem/vmm.hpp>
#include <mem/heap.hpp>

enum PageAttributes {
	PAGE_PRESENT = 0x1,
	PAGE_RW = 0x2,
	PAGE_USER = 0x4,
	PAGE_PCD = 0x10,
	PAGE_SIZE_2MB = 0x80,
};

namespace mem {
	namespace pmm {
		void initialise();
		void* palloc(size_t npages);
		void free(void* ptr, size_t npages);
	}

	namespace vmm {
		uint64_t pa_to_va(uint64_t pa);
		uint64_t va_to_pa(uint64_t va);
		
		void initialise();
		void* valloc(size_t npages);
		void free(void* ptr, size_t npages);
		uint64_t mmap(void* paddr, void* vaddr, size_t npages, uint64_t attributes);
		void munmap(void* vaddr, size_t npages);

		void* create_pagetable();
		void destroy_pagetable(void* pml4_ptr);
		uint64_t fetch_default_pagetable();

		bool is_mapped(void* vaddr);
	}

	namespace heap {
		void initialise();

		void* malloc(size_t n);
		void* malloc_aligned(size_t n, size_t alignment); // preffered to use vmm::valloc instead
		void* realloc(void* ptr, size_t n);
		void* calloc(size_t n, size_t size);

		void free(void* ptr);

		void* reserve_heap(size_t npages);
	}

	void* memset(void* dest, int value, size_t count);
	void* memcpy(void* dest, const void* src, size_t count);	
	void* memmove(void* dest, const void* src, size_t count);
	int memcmp(const void* ptr1, const void* ptr2, size_t count);
}

#endif

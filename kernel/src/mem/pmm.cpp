#include <mem/mem.hpp>
#include <cstdio>
#include <limine.h>

__attribute__((section(".limine_requests")))
volatile limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST_ID,
	.revision = 0,
	.response = nullptr,
};

#define PAGE_SIZE 4096
#define MAX_SEGMENTS 8
#define MIN_ALLOC_ADDR 0x100000

struct segment {
	uint64_t base;
	uint64_t length;
	bool is_bitmap;
	uint64_t remaining_bytes;
};

static segment segments[MAX_SEGMENTS];
static uint8_t* bitmap = nullptr;
static uint64_t bitmap_size = 0;
static uint64_t total_pages = 0;

uint64_t total_addrspace;
uint64_t total_mem;
uint64_t used_mem;
uint64_t free_mem;
uint64_t allocation_count;
uint64_t failed_allocation_count;
uint64_t free_count;
uint64_t failed_free_count;

inline void bitmap_set(uint64_t index) { bitmap[index / 8] |= (1 << (index % 8)); }
inline void bitmap_clear(uint64_t index) { bitmap[index / 8] &= ~(1 << (index % 8)); }
inline bool bitmap_test(uint64_t index) { return bitmap[index / 8] & (1 << (index % 8)); }

static inline void membulkset(void* ptr, uint64_t value, size_t bytes) {
	uint8_t* p = (uint8_t*)ptr;

	while (((uintptr_t)p & 7) && bytes) {
		*p++ = (uint8_t)value;
		bytes--;
	}

	uint64_t* p64 = (uint64_t*)p;
	while (bytes >= 8) {
		*p64++ = value;
		bytes -= 8;
	}

	p = (uint8_t*)p64;
	while (bytes--) {
		*p++ = (uint8_t)value;
	}
}

static void prepare_bitmap(uint64_t mem) {
	bitmap_size = mem / PAGE_SIZE / 8;

	segment* best = nullptr;
	for (int i = 0; i < MAX_SEGMENTS; i++) {
		if (!segments[i].is_bitmap && segments[i].length >= bitmap_size) {
			if (!best || segments[i].length < best->length)
				best = &segments[i];
		}
	}

	if (!best) {
		Log::errf("PMM: No suitable segment for bitmap");
		return;
	}

	best->is_bitmap = true;
	bitmap = reinterpret_cast<uint8_t*>(best->base);
	membulkset(bitmap, 0, bitmap_size);

	uint64_t bitmap_pages = (bitmap_size + (PAGE_SIZE - 1)) / PAGE_SIZE;
	uint64_t page_offset = 0;

	for (int i = 0; i < MAX_SEGMENTS; i++) {
		if (&segments[i] == best) {
			for (uint64_t j = 0; j < bitmap_pages; j++) {
				bitmap_set(page_offset + j);
			}
			best->remaining_bytes -= bitmap_pages * PAGE_SIZE;
			used_mem += bitmap_pages * PAGE_SIZE;
			free_mem -= bitmap_pages * PAGE_SIZE;
			break;
		}
		if (!segments[i].is_bitmap && segments[i].length > 0) {
			page_offset += segments[i].length / PAGE_SIZE;
		}
	}
}

namespace mem::pmm {

uint64_t stat_free() { return free_mem; }
uint64_t stat_used() { return used_mem; }
uint64_t stat_total_mem() { return total_mem; }

uint64_t stat_get_status(uint8_t type) {
	switch (type) {
		case 0: return total_mem;
		case 1: return used_mem;
		case 2: return free_mem;
		case 3: return allocation_count;
		case 4: return failed_allocation_count;
		case 5: return free_count;
		case 6: return failed_free_count;
		default: return 0xBADBADBADBADBAD0;
	}
}

void stat_print() {
	Log::infof("PMM: total=%llu used=%llu free=%llu", total_mem, used_mem, free_mem);
}

void initialise() {
	if (!memmap_request.response || memmap_request.response->entry_count < 1) {
		Log::errf("PMM: Failed to obtain memory map");
		return;
	}

	total_mem = used_mem = free_mem = 0;
	allocation_count = failed_allocation_count = 0;
	free_count = failed_free_count = 0;
	total_addrspace = 0;

	int created_segments = 0;

	for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
		limine_memmap_entry* e = memmap_request.response->entries[i];
		total_addrspace += e->length;

		if (e->type != LIMINE_MEMMAP_USABLE || created_segments >= MAX_SEGMENTS)
			continue;

		uint64_t seg_base = e->base;
		uint64_t seg_len = e->length;

		if (seg_base < MIN_ALLOC_ADDR) {
			if (seg_base + seg_len <= MIN_ALLOC_ADDR) {
				continue;
			}
			seg_len -= (MIN_ALLOC_ADDR - seg_base);
			seg_base = MIN_ALLOC_ADDR;
		}

		uint64_t align_offset = seg_base & (PAGE_SIZE - 1);
		if (align_offset) {
			seg_base += PAGE_SIZE - align_offset;
			if (seg_len > PAGE_SIZE - align_offset) {
				seg_len -= PAGE_SIZE - align_offset;
			} else {
				continue;
			}
		}
		seg_len = (seg_len / PAGE_SIZE) * PAGE_SIZE;

		if (seg_len == 0) continue;

		segment* s = &segments[created_segments++];
		s->base = seg_base;
		s->length = seg_len;
		s->is_bitmap = false;
		s->remaining_bytes = seg_len;

		total_mem += seg_len;
	}

	free_mem = total_mem;
	prepare_bitmap(total_mem);
	total_pages = total_mem / PAGE_SIZE;
}

void* palloc(size_t npages) {
	if (!bitmap || npages == 0) {
		failed_allocation_count++;
		return nullptr;
	}

	uint64_t needed = npages;
	uint64_t page_offset = 0;

	for (int segid = 0; segid < MAX_SEGMENTS; segid++) {
		segment* seg = &segments[segid];
		if (seg->length == 0 || seg->is_bitmap) {
			if (!seg->is_bitmap && seg->length > 0) {
				page_offset += seg->length / PAGE_SIZE;
			}
			continue;
		}

		uint64_t seg_pages = seg->length / PAGE_SIZE;
		uint64_t found = 0;
		uint64_t start = 0;

		for (uint64_t j = 0; j < seg_pages; j++) {
			uint64_t global_index = page_offset + j;

			if (!bitmap_test(global_index)) {
				if (found == 0) start = j;
				found++;
				if (found == needed) {
					uint64_t start_global = page_offset + start;
					for (uint64_t k = 0; k < needed; k++) {
						bitmap_set(start_global + k);
					}

					uint64_t alloc_bytes = needed * PAGE_SIZE;
					used_mem += alloc_bytes;
					free_mem -= alloc_bytes;
					seg->remaining_bytes -= alloc_bytes;
					allocation_count++;

					uint64_t phys_addr = seg->base + start * PAGE_SIZE;
					return reinterpret_cast<void*>(phys_addr);
				}
			} else {
				found = 0;
			}
		}

		page_offset += seg_pages;
	}

	failed_allocation_count++;
	return nullptr;
}

void free(void* ptr, size_t npages) {
	if (!bitmap || !ptr || npages == 0) {
		failed_free_count++;
		return;
	}

	uint64_t addr = reinterpret_cast<uint64_t>(ptr);

	if (addr < MIN_ALLOC_ADDR) {
		Log::errf("PMM: Attempt to free memory below 1 MiB (%p)", ptr);
		failed_free_count++;
		return;
	}

	uint64_t page_offset = 0;
	segment* seg = nullptr;

	for (int i = 0; i < MAX_SEGMENTS; i++) {
		if (segments[i].length == 0) continue;
		
		uint64_t seg_start = segments[i].base;
		uint64_t seg_end = seg_start + segments[i].length;

		if (addr >= seg_start && addr < seg_end) {
			if (segments[i].is_bitmap) {
				Log::errf("PMM: Attempt to free bitmap segment");
				failed_free_count++;
				return;
			}
			seg = &segments[i];
			break;
		}

		if (!segments[i].is_bitmap) {
			page_offset += segments[i].length / PAGE_SIZE;
		}
	}

	if (!seg) {
		Log::errf("PMM: Free failed, pointer %p outside segments", ptr);
		failed_free_count++;
		return;
	}

	uint64_t seg_page_index = (addr - seg->base) / PAGE_SIZE;
	uint64_t global_index = page_offset + seg_page_index;
	uint64_t actually_freed = 0;

	for (uint64_t i = 0; i < npages; i++) {
		uint64_t idx = global_index + i;
		if (bitmap_test(idx)) {
			bitmap_clear(idx);
			actually_freed += PAGE_SIZE;
		} else {
			Log::warnf("PMM: Attempt to free already-free page at index %llu", idx);
			failed_free_count++;
		}
	}

	if (actually_freed > 0) {
		if (used_mem >= actually_freed) {
			used_mem -= actually_freed;
		} else {
			used_mem = 0;
		}

		free_mem += actually_freed;
		seg->remaining_bytes += actually_freed;
		free_count++;
	}
}

void* reserve_heap(size_t npages) {
	return palloc(npages);
}

}

#include "elf.hpp"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <mem/vmm.hpp>
#include <mem/pmm.hpp>
#include <arch/arch.hpp>
#include <cstdio>

constexpr size_t PAGE_SIZE = 0x1000;
constexpr size_t PAGE_MASK = PAGE_SIZE - 1;

constexpr uint16_t ET_EXEC = 2;
constexpr uint16_t ET_DYN  = 3;

constexpr uint32_t PT_LOAD    = 1;
constexpr uint32_t PT_DYNAMIC = 2;

constexpr uint32_t PF_X = 0x1;
constexpr uint32_t PF_W = 0x2;
constexpr uint32_t PF_R = 0x4;

constexpr int64_t DT_NULL    = 0;
constexpr int64_t DT_RELA    = 7;
constexpr int64_t DT_RELASZ  = 8;
constexpr int64_t DT_RELAENT = 9;

constexpr uint64_t R_X86_64_RELATIVE = 8;

static inline uint64_t align_down(uint64_t addr, uint64_t alignment) {
    return addr & ~(alignment - 1);
}

static inline uint64_t align_up(uint64_t addr, uint64_t alignment) {
    return (addr + alignment - 1) & ~(alignment - 1);
}

static inline bool is_valid_elf_magic(const Elf64_Ehdr* ehdr) {
    return ehdr->e_ident[0] == 0x7F &&
           ehdr->e_ident[1] == 'E' &&
           ehdr->e_ident[2] == 'L' &&
           ehdr->e_ident[3] == 'F';
}

static inline uint64_t elf_reloc_type(uint64_t info) {
    return info & 0xFFFFFFFF;
}

static inline uint64_t convert_flags_to_page_flags(uint32_t elf_flags, bool user_mode) {
    uint64_t flags = PAGE_PRESENT;
    
    if (elf_flags & PF_W) {
        flags |= PAGE_RW;
    }
    
    if (user_mode) {
        flags |= PAGE_USER;
    }
    
    return flags;
}

static Elf64_Dyn* find_dynamic_section(uint64_t load_base, Elf64_Phdr* phdrs, uint16_t phnum) {
    for (uint16_t i = 0; i < phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            return reinterpret_cast<Elf64_Dyn*>(load_base + phdrs[i].p_vaddr);
        }
    }
    return nullptr;
}

static void process_relocations(uint64_t load_base, Elf64_Phdr* phdrs, uint16_t phnum) {
    Elf64_Dyn* dynamic_section = find_dynamic_section(load_base, phdrs, phnum);
    if (!dynamic_section) {
        return;
    }

    Elf64_Rela* rela_table = nullptr;
    size_t rela_total_size = 0;
    size_t rela_entry_size = sizeof(Elf64_Rela);

    for (Elf64_Dyn* entry = dynamic_section; entry->d_tag != DT_NULL; entry++) {
        switch (entry->d_tag) {
            case DT_RELA:
                rela_table = reinterpret_cast<Elf64_Rela*>(load_base + entry->d_val);
                break;
            case DT_RELASZ:
                rela_total_size = entry->d_val;
                break;
            case DT_RELAENT:
                rela_entry_size = entry->d_val;
                break;
        }
    }

    if (!rela_table || rela_total_size == 0) {
        return;
    }

    size_t num_relocations = rela_total_size / rela_entry_size;
    
    for (size_t i = 0; i < num_relocations; i++) {
        Elf64_Rela* reloc = &rela_table[i];
        uint64_t reloc_type = elf_reloc_type(reloc->r_info);
        
        if (reloc_type == R_X86_64_RELATIVE) {
            uint64_t* target = reinterpret_cast<uint64_t*>(load_base + reloc->r_offset);
            *target = load_base + reloc->r_addend;
        }
    }
}

static bool map_segment_pages(uint64_t segment_base, size_t segment_size, uint64_t page_flags) {
    uint64_t page_start = align_down(segment_base, PAGE_SIZE);
    uint64_t page_end = align_up(segment_base + segment_size, PAGE_SIZE);
    size_t num_pages = (page_end - page_start) / PAGE_SIZE;

    for (size_t i = 0; i < num_pages; i++) {
        void* virtual_addr = reinterpret_cast<void*>(page_start + i * PAGE_SIZE);
        
        void* physical_page = mem::pmm::palloc(1);
        if (!physical_page) {
        	Log::errf("Failed to allocate phys memory");
            return false;
        }
        
        uint64_t phys_as_va = mem::vmm::pa_to_va(reinterpret_cast<uint64_t>(physical_page));
        mem::memset(reinterpret_cast<void*>(phys_as_va), 0, PAGE_SIZE);
        
        uint64_t result = mem::vmm::mmap(physical_page, physical_page, 1, page_flags | PAGE_USER);
        if (result == 0) {
        	Log::errf("Failed to map page");
            return false;
        }
    }
    
    return true;
}

static bool load_elf_segment(const Elf64_Phdr* phdr, uint64_t load_base, 
                             const uint8_t* elf_data, bool user_mode) {
    uint64_t segment_vaddr = (load_base != 0) 
                            ? load_base + phdr->p_vaddr 
                            : phdr->p_vaddr;
    
    uint64_t page_flags = convert_flags_to_page_flags(phdr->p_flags, user_mode);
    
    if (!map_segment_pages(segment_vaddr, phdr->p_memsz, page_flags)) {
    	Log::errf("Failed to map segment");
        return false;
    }
    
    if (phdr->p_filesz > 0) {
        mem::memcpy(reinterpret_cast<void*>(segment_vaddr),
                   elf_data + phdr->p_offset,
                   phdr->p_filesz);
    }
    
    if (phdr->p_memsz > phdr->p_filesz) {
        size_t bss_size = phdr->p_memsz - phdr->p_filesz;
        mem::memset(reinterpret_cast<void*>(segment_vaddr + phdr->p_filesz), 
                   0, bss_size);
    }
    
    return true;
}

static void* allocate_user_stack(size_t num_pages) {
    if (num_pages == 0) {
        num_pages = 2;
    }
    
    void* last_page_phys = nullptr;
    
    for (size_t i = 0; i < num_pages; i++) {
        void* phys_page = mem::pmm::palloc(1);
        if (!phys_page) {
            return nullptr;
        }
        
        uint64_t phys_va = mem::vmm::pa_to_va(reinterpret_cast<uint64_t>(phys_page));
        uint64_t result = mem::vmm::mmap(phys_page, reinterpret_cast<void*>(phys_va), 1, 
                                        PAGE_PRESENT | PAGE_RW | PAGE_USER);
        if (result == 0) {
            return nullptr;
        }
        
        last_page_phys = phys_page;
    }
    
    return reinterpret_cast<void*>(
        mem::vmm::pa_to_va(reinterpret_cast<uint64_t>(last_page_phys)) + PAGE_SIZE
    );
}

void run_elf(void* elf_base, size_t elf_file_size, bool user_mode) {
    auto* ehdr = reinterpret_cast<Elf64_Ehdr*>(elf_base);
    
    if (!is_valid_elf_magic(ehdr)) {
    	Log::errf("Executable header is invalid");
        return;
    }
    
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
    	Log::errf("Executable is neither ET_EXEC nor ET_DYN");
        return;
    }
    
    const uint8_t* elf_data = reinterpret_cast<const uint8_t*>(elf_base);
    Elf64_Phdr* phdrs = (Elf64_Phdr*)(elf_data + ehdr->e_phoff);
    
    uint64_t load_base = 0;
    bool is_pie = (ehdr->e_type == ET_DYN);
    
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            if (!load_elf_segment(&phdrs[i], load_base, elf_data, user_mode)) {
            	Log::errf("Failed to load segments");
                return;
            }
        }
    }
    
    if (is_pie) {
        process_relocations(load_base, phdrs, ehdr->e_phnum);
    }
    
    uint64_t entry_point = is_pie ? (load_base + ehdr->e_entry) : ehdr->e_entry;
    
    void* stack_top = allocate_user_stack(2);
    if (!stack_top) {
    	Log::errf("Failed to allocate stack!");
        return;
    }
    
    if (user_mode) {
        arch::x86_64::ringctl::execute_ring3(
            reinterpret_cast<void(*)()>(entry_point),
            reinterpret_cast<uint8_t*>(stack_top)
        );
    } else {
        uint64_t saved_rsp;
        asm volatile("mov %%rsp, %0" : "=r"(saved_rsp));
        
        asm volatile(
            "mov %0, %%rsp\n"
            "call *%1\n"
            "mov %2, %%rsp"
            :
            : "r"(reinterpret_cast<uint64_t>(stack_top)),
              "r"(entry_point),
              "r"(saved_rsp)
            : "memory"
        );
    }
}

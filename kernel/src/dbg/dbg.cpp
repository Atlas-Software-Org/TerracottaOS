#include "dbg.hpp"
#include <mem/mem.hpp>
#include <drivers/tty/ldisc/ldisc.hpp>
#include <cstdio>
#include <cinttypes>
#include <Zydis/Zydis.h>
#include <cstring>
#include <limine.h>
#include <exec/elf.hpp>
#include <cctype>

__attribute__((section(".limine_requests")))
volatile limine_executable_file_request executable_file_request = {
	.id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
	.revision = 0,
	.response = nullptr // shut up gcc
};

const char* find_symbol(uint64_t addr, uint64_t* offset = nullptr) {
    limine_file* exe = executable_file_request.response->executable_file;
    if (!exe) {
        return "??";
    }

    Elf64_Ehdr* hdr = (Elf64_Ehdr*)exe->address;
    Elf64_Shdr* shdr = (Elf64_Shdr*)((uint64_t)exe->address + hdr->e_shoff);

    Elf64_Shdr* shstrtab_hdr = &shdr[hdr->e_shstrndx];
    const char* shstrtab = (const char*)exe->address + shstrtab_hdr->sh_offset;

    Elf64_Shdr* symtab_hdr = nullptr;
    Elf64_Shdr* strtab_hdr = nullptr;

    for (int i = 0; i < hdr->e_shnum; i++) {
        const char* name = shstrtab + shdr[i].sh_name;
        if (strcmp(name, ".symtab") == 0) {
            symtab_hdr = &shdr[i];
        } else if (strcmp(name, ".strtab") == 0) {
            strtab_hdr = &shdr[i];
        }
    }

    if (!symtab_hdr || !strtab_hdr) {
        return "??";
    }

    Elf64_Sym* symtab = (Elf64_Sym*)((uint64_t)exe->address + symtab_hdr->sh_offset);
    const char* strtab = (const char*)exe->address + strtab_hdr->sh_offset;

    int count = symtab_hdr->sh_size / sizeof(Elf64_Sym);

    for (int i = 0; i < count; i++, symtab++) {
        uint64_t start = symtab->st_value;
        uint64_t end   = symtab->st_value + symtab->st_size;

        if (addr >= start && addr < end) {
            if (offset) *offset = addr - start;
            const char* name = strtab + symtab->st_name;
            return name;
        }
    }

    return "??";
}

uint64_t hexstr_to_u64(const char* str) {
    if (!str) return 0;

    while (*str && isspace(*str)) str++;

    if (*str == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;

    uint64_t result = 0;
    while (*str) {
        char c = *str++;
        uint8_t value = 0;

        if (c >= '0' && c <= '9') value = c - '0';
        else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
        else break;

        result = (result << 4) | value;
    }

    return result;
}

namespace dbg {
namespace memview {

void print_memory_contents_at(uint64_t addr, uint64_t len, uint64_t paging_len) {
    if (paging_len == 0 || paging_len % 16 != 0)
        paging_len = 256;

    for (uint64_t a = 0; a < len; a++) {
        if (a % paging_len == 0 && a != 0) {
            printf("Press any key to print the next page\n\r");
            char c;
            drivers::tty::ldisc::read(false, &c, 1);
            printf("\033[2J\033[H");
        }
        
        if (a % paging_len == 0) {
            printf(" ADDR             | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n\r");
            printf(" -----------------+------------------------------------------------\n\r");
        }
        
        if (a % 16 == 0) {
            printf(" %016llX | ", (unsigned long long)(addr + a));
        }
        
        if (mem::vmm::is_mapped((void*)(addr + a))) {
            printf("%02X ", *(uint8_t*)(addr + a));
        } else {
            printf("?? ");
        }
        
        if ((a + 1) % 16 == 0)
            printf("\n\r");
    }
    
    if (len % 16 != 0)
        printf("\n\r");
}

}

namespace disasm {

void disasm_at_memory(uint64_t addr, uint64_t len, uint64_t paging_len) {
    if (paging_len == 0 || paging_len % 16 != 0)
        paging_len = 256;

    ZyanU8* data = (ZyanU8*)addr;
    ZyanU64 runtime_address = addr;
    ZyanUSize offset = 0;
    ZydisDisassembledInstruction instruction;
    ZyanUSize instructions_printed = 0;

    printf(" ----==== DISASSEMBLY ====---- ------------------------------------\n\r");

    while (offset < len && ZYAN_SUCCESS(ZydisDisassembleIntel(
        ZYDIS_MACHINE_MODE_LONG_64,
        runtime_address,
        data + offset,
        len - offset,
        &instruction
    ))) {
        if (instructions_printed > 0 && instructions_printed % paging_len == 0) {
            printf("Press any key to continue...\n\r");
            char c;
            drivers::tty::ldisc::read(false, &c, 1);
            printf("\033[2J\033[H");
        }

        printf("%016llX  %s\n\r", (unsigned long long)runtime_address, instruction.text);

        if (instruction.text[0] == 'j' || instruction.text[0] == 'J' ||
			strncmp(instruction.text, "call", 4) == 0 || strncmp(instruction.text, "CALL", 4) == 0
        ) {
        	uint64_t offset = 0;
        	uint64_t target = hexstr_to_u64(strchr(instruction.text, ' ') + 1);
            const char* func = find_symbol(target, &offset);
                    
            printf(" -> %s + 0x%llX\n\r", func, (unsigned long long)offset);
        }
        
        offset += instruction.info.length;
        runtime_address += instruction.info.length;
        instructions_printed++;
    }
}

}

namespace stacktrace {

static inline bool is_canonical(uint64_t addr) {
    return ((int64_t)addr) == (int64_t)(addr << 16 >> 16);
}

void stacktrace(uint64_t addr, uint64_t len, uint64_t paging_len) {
    if (paging_len == 0 || paging_len % 16 != 0)
        paging_len = 16;

    uint64_t rbp = addr;
    int frame = 0;

    printf(" ----==== STACKTRACE ====---- -------------------------------------\n\r");

    while (rbp != 0 && frame < len) {
        uint64_t ret_addr = *((uint64_t*)(rbp + 8));

        uint64_t offset = 0;
        const char* func = find_symbol(ret_addr, &offset);

        printf("#%d: <%s+0x%llX> (RIP=%016llX)\n", 
               frame, func, (unsigned long long)offset, (unsigned long long)ret_addr);

        rbp = *((uint64_t*)rbp);
        frame++;

        if (frame % paging_len == 0 && frame != 0) {
            printf("Press any key to continue stacktrace\n\r");
            char c;
            drivers::tty::ldisc::read(false, &c, 1);
            printf("\033[2J\033[H");
        }
        
        if (!is_canonical(rbp)) break;;
    }
}

}

}

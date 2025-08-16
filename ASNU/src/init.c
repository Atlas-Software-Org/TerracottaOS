#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <string.h>
#include <KiSimple.h>
#include <TerminalDriver/flanterm.h>
#include <TerminalDriver/flanterm_backends/fb.h>
#include <Serial/serial.h>
#include <PMM/pmm.h>
#include <VMM/vmm.h>

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

static void hcf(void) {
    for (;;) {
#if defined (__x86_64__)
        asm ("hlt");
#elif defined (__aarch64__) || defined (__riscv)
        asm ("wfi");
#elif defined (__loongarch64)
        asm ("idle 0");
#endif
    }
}

void kmain(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    if (memmap_request.response == NULL
     || memmap_request.response->entry_count < 1) {
        hcf();
    }

    if (hhdm_request.response == NULL
     || hhdm_request.response->offset == 0) {
        hcf();
    }

    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    struct flanterm_context *ft_ctx = flanterm_fb_init(
        NULL,
        NULL,
        framebuffer->address, framebuffer->width, framebuffer->height, framebuffer->pitch,
        framebuffer->red_mask_size, framebuffer->red_mask_shift,
        framebuffer->green_mask_size, framebuffer->green_mask_shift,
        framebuffer->blue_mask_size, framebuffer->blue_mask_shift,
        NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, 0, 0, 1,
        0, 0,
        0
    );

    SetGlobalFtCtx(ft_ctx);

    serial_fwrite("ASNU Booted");

    serial_fwrite("HHDM Offset: %p", (void*)hhdm_request.response->offset);

    /* Enumerate memory map entries and log them */
    serial_fwrite("Enumerating memory map entries");

    const char* MemMapTypes[] = {
        "LIMINE_MEMMAP_USABLE",
        "LIMINE_MEMMAP_RESERVED",
        "LIMINE_MEMMAP_ACPI_RECLAIMABLE",
        "LIMINE_MEMMAP_ACPI_NVS",
        "LIMINE_MEMMAP_BAD_MEMORY",
        "LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE",
        "LIMINE_MEMMAP_KERNEL_AND_MODULES",
        "LIMINE_MEMMAP_FRAMEBUFFER"
    };

    int LargestEntry = -1;
    int SecondLargestEntry = -1;

    uint64_t TotalMemory = 0;
    uint64_t TotalUsableMemory = 0;
    uint64_t TotalReservedMemory = 0;
    uint64_t TotalPmmMemory = 0;

    for (int i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry *memmap_entry = memmap_request.response->entries[i];

        if (LargestEntry == -1 || memmap_entry->length > memmap_request.response->entries[LargestEntry]->length) {
            if (memmap_entry->type == LIMINE_MEMMAP_USABLE) {
                SecondLargestEntry = LargestEntry;
                LargestEntry = i;
                TotalPmmMemory = memmap_entry->length;
            }
        }

        TotalMemory += memmap_entry->length;
        if (memmap_entry->type == LIMINE_MEMMAP_USABLE) {
            TotalUsableMemory += memmap_entry->length;
        }
    }

    TotalReservedMemory = TotalMemory - TotalUsableMemory;

    extern uint8_t TPAMStart[];
    extern uint8_t TPAMEnd[];

    void* tpam_base = (void*)TPAMStart;
    size_t tpam_size = TPAMEnd - TPAMStart;
    pmm_init(1, tpam_base, tpam_size, (void*)((uint64_t)tpam_base + 4096*16), (tpam_size/0x1000)/8, TotalMemory, TotalUsableMemory, TotalReservedMemory);
    
    void* bitmap_base = (void*)((uint64_t)tpam_base + 16*0x1000);
    memset(bitmap_base, 0, 4096);

    int result = mmap(0x3000, 0x3000, MMAP_PRESENT | MMAP_RW);

    const char* VmmResultStrings[] = {
        "VMM_SUCCESS",
        "VMM_ERR_NULL",
        "VMM_ERR_NO_MEM",
        "VMM_ERR_INVALID_PML4",
        "VMM_ERR_INVALID_PDPT",
        "VMM_ERR_INVALID_PD",
        "VMM_ERR_ALREADY_MAPPED",
        "VMM_ERR_NOT_MAPPED"
    };

    serial_fwrite("Result of mmap: %s", VmmResultStrings[result]);

    *(uint8_t*)0x3000 = 0xAA;
    serial_fwrite("Value at 0x3000: %02X", *(uint8_t*)0x3000);

    hcf();
}

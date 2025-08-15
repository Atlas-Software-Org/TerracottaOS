#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <string.h>
#include <KiSimple.h>
#include <TerminalDriver/flanterm.h>
#include <TerminalDriver/flanterm_backends/fb.h>
#include <Serial/serial.h>

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

    for (int i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry *memmap_entry = memmap_request.response->entries[i];

        if (LargestEntry == -1 || memmap_entry->length > memmap_request.response->entries[LargestEntry]->length) {
            if (memmap_entry->type == LIMINE_MEMMAP_USABLE) {
                SecondLargestEntry = LargestEntry;
                LargestEntry = i;
            }
        }

        TotalMemory += memmap_entry->length;
        if (memmap_entry->type == LIMINE_MEMMAP_USABLE) {
            TotalUsableMemory += memmap_entry->length;
        }

        serial_fwrite("Detected Entry At Offset %d\n\r\tBase: %p\n\r\tLength: %llu\n\r\tType: %s\n\r", i, memmap_entry->base, memmap_entry->length, MemMapTypes[memmap_entry->type]);
    }

    TotalReservedMemory = TotalMemory - TotalUsableMemory;

    serial_fwrite("Finished enumerating memory map entries");

    serial_fwrite("Largest memory map entry is at index %d with length %llu", LargestEntry, memmap_request.response->entries[LargestEntry]->length);
    serial_fwrite("Second largest memory map entry is at index %d with length %llu", SecondLargestEntry, memmap_request.response->entries[SecondLargestEntry]->length);

    serial_fwrite("Total memory: %llu bytes", TotalMemory);
    serial_fwrite("Total usable memory: %llu bytes", TotalUsableMemory);

    hcf();

    pmm_init(memmap_request.response->entries[LargestEntry]->base, memmap_request.response->entries[LargestEntry]->length, memmap_request.response->entries[SecondLargestEntry]->base, memmap_request.response->entries[SecondLargestEntry]->length, TotalMemory, TotalUsableMemory, TotalReservedMemory);

    serial_fwrite("Physical Memory Manager initialized");

    serial_fwrite("Allocating 4KB of memory");
    void* allocated_memory = kalloc(4096);
    if (allocated_memory != NULL) {
        serial_fwrite("Allocated 4KB of memory at address %p", allocated_memory);
    } else {
        serial_fwrite("Failed to allocate 4KB of memory");
    }
    serial_fwrite("Freeing allocated memory");
    kfree(allocated_memory);
    serial_fwrite("Memory freed");

    hcf();
}

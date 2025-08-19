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
#include <GDT/GDT.h>
#include <IDT/idt.h>
#include <Drivers/PS2Keyboard.h>
#include <sched/scheduler.h>

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
    void* BaseLargestEntry = NULL;
    void* BaseSecondLargestEntry = NULL;

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
                BaseSecondLargestEntry = BaseLargestEntry;
                BaseLargestEntry = (void*)memmap_entry->base;
            }
        }

        TotalMemory += memmap_entry->length;
        if (memmap_entry->type == LIMINE_MEMMAP_USABLE) {
            TotalUsableMemory += memmap_entry->length;
        }

        if (memmap_entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            serial_fwrite("Detected a bootloader reclaimable memory region at %p with length %llu bytes", (void*)memmap_entry->base, memmap_entry->length);
        }
    }

    BaseLargestEntry = (void*)((uint64_t)BaseLargestEntry + hhdm_request.response->offset);
    BaseSecondLargestEntry = (void*)((uint64_t)BaseSecondLargestEntry + hhdm_request.response->offset);

    pmm_init(1, (uint64_t)BaseLargestEntry, memmap_request.response->entries[LargestEntry]->length, (uint64_t)BaseSecondLargestEntry, memmap_request.response->entries[SecondLargestEntry]->length, TotalMemory, TotalUsableMemory, TotalReservedMemory);

    vmm_init();

    gdt_init();

    pit_init(100);

    idt_init();

    scheduler_init();

    void test_sched();
    test_sched();

    hcf();
}

void proc0() {
    for (int i = 0; i < 10000000; i++) {
        if (i % 1000 == 0) {
            serial_fwrite("Proc0 counted to: %d\n\r", i);
        }
    }
    serial_fwrite("Proc0 finished counting.\n\r");
}

void proc1() {
    for (int i = 0; i < 10000000; i++) {
        if (i % 1000 == 0) {
            serial_fwrite("Proc1 counted to: %d\n\r", i);
        }
    }
    serial_fwrite("Proc1 finished counting.\n\r");
}

void test_sched() {
    uint64_t stack0_base = (uint64_t)palloc();
    uint64_t stack0_size = 4096;
    uint64_t heap0_base = (uint64_t)palloc();
    uint64_t heap0_size = 4096;
    uint64_t stack1_base = (uint64_t)palloc();
    uint64_t stack1_size = 4096;
    uint64_t heap1_base = (uint64_t)palloc();
    uint64_t heap1_size = 4096;
    Procedure* proc0_ptr = create_proc(proc0, 0, 0, 0, 0, stack0_base, stack0_size, heap0_base, heap0_size);
    Procedure* proc1_ptr = create_proc(proc1, 0, 0, 0, 0, stack1_base, stack1_size, heap1_base, heap1_size);

    register_proc(proc0_ptr);
    register_proc(proc1_ptr);
}

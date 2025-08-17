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
#include <Drivers/AHCI.h>

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

    idt_init();

    PciDevice_t ahci_dev = PciFindDeviceByClass(PCI_CLASS_MASS_STORAGE, 0x06);
    if (ahci_dev.vendor_id != 0xFFFF && ahci_dev.device_id != 0xFFFF) {
        serial_fwrite("Found AHCI device: %s (%04X:%04X)", PciGetDeviceName(&ahci_dev), ahci_dev.vendor_id, ahci_dev.device_id);
        PciGetDeviceMMIORegion(&ahci_dev);
        ahci_init(&ahci_dev);
        if (ahci_dev.MMIOBase != NULL) {
            serial_fwrite("AHCI MMIO Base: %p", ahci_dev.MMIOBase);
            serial_fwrite("AHCI MMIO Size: %u bytes", ahci_dev.MMIOSize);
            serial_fwrite("AHCI MMIO Bar Index: %u", ahci_dev.MMIOBarIndex);
            serial_fwrite("AHCI Device Class: %s", PciGetDeviceManufacturer(&ahci_dev));
            serial_fwrite("AHCI Device Name:    %s", PciGetDeviceName(&ahci_dev));
        } else {
            serial_fwrite("AHCI MMIO Base is NULL, cannot initialize AHCI.");
        }
    } else {
        serial_fwrite("No AHCI device found.");
    }

    uint8_t secbuf[512];
    ahci_read_sector(0, secbuf);

    printk("ADDR | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n\r");
    for (int i = 0; i < 512; i += 16) {
        printk("%04X | ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < 512) {
                printk("%02X ", secbuf[i + j]);
            } else {
                printk("   ");
            }
        }
        printk(" | ");
        for (int j = 0; j < 16; j++) {
            if (i + j < 512) {
                if (secbuf[i + j] >= 32 && secbuf[i + j] <= 126) {
                    printk("%c", secbuf[i + j]);
                } else {
                    printk(".");
                }
            } else {
                printk(" ");
            }
        }
        printk("\n\r");
    }
    hcf();
}

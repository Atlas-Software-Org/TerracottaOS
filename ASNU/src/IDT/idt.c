#include "idt.h"
#include <Serial/serial.h>

typedef struct {
	uint16_t    isr_low;
	uint16_t    kernel_cs;
	uint8_t	    ist;
	uint8_t     attributes;
	uint16_t    isr_mid;
	uint32_t    isr_high;
	uint32_t    reserved;
} __attribute__((packed)) idt_entry_t;

__attribute__((aligned(0x1000))) 
static idt_entry_t idt[256];

static idtr_t idtr;

const char* PageFaultErrorCodeStrings[] = {
    "Present violation",
    "Write access",
    "User-mode access",
    "Reserved bit violation",
    "Instruction fetch",
    "Protection-key violation",
    [15] = "SGX access violation"
};

static void PrintPageFaultError(uint64_t err) {
    for (int bit = 0; bit < 64; bit++) {
        if (err & (1ULL << bit) && bit < sizeof(PageFaultErrorCodeStrings) / sizeof(char*)) {
            const char* msg = PageFaultErrorCodeStrings[bit];
            if (msg)
                printk("  - %s\n\r", msg);
        }
    }
}

char error_codes[32][128] = {
    "Division by 0", "Reserved1", "NMI Interrupt", "Breakpoint (INT3)", "Overflow (INTO)", "Bounds range exceeded (BOUND)", "Invalid opcode (UD2)",
    "Device not available (WAIT/FWAIT)", "Double fault", "Coprocessor segment overrun", "Invalid TSS", "Segment not present", "Stack-segment fault",
    "General protection fault (GPFault)", "Page fault", "Reserved2", "x87 FPU error", "Alignment check", "SIMD Floating-Point Exception", "Reserved3",
};

void exception_handler(int exception, uint64_t err_code) {
    if (exception < 0 || exception > 31) {
        printk("\x1b[1;91m{ PANIC }\tIDT Exception occurred\n\r\t\tUnknown exception (%d) - Assuming software interrupt\n\r\x1b[0m", exception);
        asm volatile ("cli; hlt");
        while (1);
    }

    serial_fwrite("\x1b[1;91m{ PANIC }\tIDT Exception occurred\n\r\t\t%s\n\r\x1b[0m", error_codes[exception]);

    if (exception == 14) {
        serial_fwrite("Exception is a #PAGEFAULT\n\r");

		uint64_t CR2 = 0;
        asm volatile (
            ".intel_syntax noprefix;"
            "mov %0, cr2;"
            ".att_syntax prefix;"
            : "=r"(CR2)
            :
            : "memory"
        );

		serial_fwrite("CR2: 0x%016X\n\r", CR2);
		
        PrintPageFaultError(err_code);

        if (CR2 == 0 || (CR2 >= 0x0000000000000000 && CR2 < 0x1000)) {
            serial_fwrite("CR2 is null or too low. Cannot recover.\n\rHalting...\n\r");
        } else {
            serial_fwrite("Attempting to identity-map faulting address: %p\n\r", (void*)CR2);
            mmap((void*)CR2, (void*)CR2, PAGE_PRESENT | PAGE_RW);
            serial_fwrite("Mapped. Returning to continue execution.\n\r");
            return;
        }
    }

    asm volatile ("cli; hlt");
    while (1);
}

void idt_set_desc(uint8_t vector, void* isr, uint8_t flags) {
    idt_entry_t* descriptor = &idt[vector];

    descriptor->isr_low        = (uint64_t)isr & 0xFFFF;
    descriptor->kernel_cs      = GDT_OFFSET_KERNEL_CODE;
    descriptor->ist            = 0;
    descriptor->attributes     = flags;
    descriptor->isr_mid        = ((uint64_t)isr >> 16) & 0xFFFF;
    descriptor->isr_high       = ((uint64_t)isr >> 32) & 0xFFFFFFFF;
    descriptor->reserved       = 0;
}

static bool vectors[IDT_MAX_DESCRIPTORS];

extern void* isr_stub_table[];

void idt_init_exceptions() {
    for (uint8_t vector = 0; vector < 32; vector++) {
        idt_set_desc(vector, isr_stub_table[vector], 0x8E);
        vectors[vector] = true;
    }
}

__attribute__((interrupt)) void idt_keyboard_handler(int* __unused) {
    (void)__unused;
    uint8_t sc = inb(0x60);
    KeyboardDriverMain(sc);
    idt_pic_send_eoi(1);
    outb(0x20, 0x20);
}

idtr_t idt_init() {
    idtr.base = (uintptr_t)&idt[0];
    idtr.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_MAX_DESCRIPTORS - 1);

    idt_init_exceptions();

    idt_pic_remap(0x20, 0x28);

    idt_set_desc(0x20, (void*)&pit_handler, 0x8E);
    idt_set_desc(0x21, (void*)&idt_keyboard_handler, 0x8E);

    outb(PIC1_DATA, 0b11111100);
    outb(PIC2_DATA, 0b11111111);
    idt_irq_clear_mask(0);
    idt_irq_clear_mask(1);

    asm volatile ("cli");

    __asm__ volatile ("lidt %0" : : "m"(idtr));

    asm volatile ("sti");

    return idtr;
}

#define PIC_EOI		0x20		/* End-of-interrupt command code */

void idt_pic_send_eoi(uint8_t irq)
{
	if(irq >= 8)
		outb(PIC2_COMMAND,PIC_EOI);
	
	outb(PIC1_COMMAND,PIC_EOI);
}

#define ICW1_ICW4	0x01
#define ICW1_SINGLE	0x02
#define ICW1_INTERVAL4	0x04
#define ICW1_LEVEL	0x08
#define ICW1_INIT	0x10

#define ICW4_8086	0x01
#define ICW4_AUTO	0x02
#define ICW4_BUF_SLAVE	0x08
#define ICW4_BUF_MASTER	0x0C
#define ICW4_SFNM	0x10

void idt_pic_remap(int offset1, int offset2) {
	outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
	IOWait();
	outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
	IOWait();
	outb(PIC1_DATA, offset1);
	IOWait();
	outb(PIC2_DATA, offset2);
	IOWait();
	outb(PIC1_DATA, 4);
	IOWait();
	outb(PIC2_DATA, 2);
	IOWait();
	
	outb(PIC1_DATA, ICW4_8086);
	IOWait();
	outb(PIC2_DATA, ICW4_8086);
	IOWait();

	outb(PIC1_DATA, 0);
	outb(PIC2_DATA, 0);
}

void KiIrqSetMask(uint8_t IRQline) {
    uint16_t port;
    uint8_t value;

    if(IRQline < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        IRQline -= 8;
    }
    value = inb(port) | (1 << IRQline);
    outb(port, value);        
}

void idt_irq_clear_mask(uint8_t IRQline) {
    uint16_t port;
    uint8_t value;

    if(IRQline < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        IRQline -= 8;
    }
    value = inb(port) & ~(1 << IRQline);
    outb(port, value);        
}

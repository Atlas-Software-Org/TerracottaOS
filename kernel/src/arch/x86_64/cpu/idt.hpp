#ifndef IDT_HPP
#define IDT_HPP 1

#include <cstdint>

struct exception_frame {
    uint64_t rax, rbx, rcx, rdx, rbp, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t exception_vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;

    uint64_t cr0, cr2, cr3, cr4;
    uint16_t ds, es, fs, gs;
} __attribute__((packed));

typedef struct {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed)) idtr_t;

typedef struct {
	uint16_t isr_offset_low;
	uint16_t gdt_selector;
	uint8_t ist;
	uint8_t flags; /* usually 0x8E */
	uint16_t isr_offset_middle;
	uint32_t isr_offset_high;
	uint32_t always_zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
	idt_entry_t entries[512];
} __attribute__((packed)) idt_t;

extern "C" void idt_load(const idtr_t* idtr);

namespace arch::x86_64::cpu::idt {

void load_idt();
void initialise();
void set_descriptor(uint8_t vector, uint64_t isr, uint8_t flags);
void clear_descriptor(uint8_t vector);

void irq_clear_mask(uint8_t irq);
void irq_set_mask(uint8_t irq);

void send_eoi(uint8_t irq);

}

#endif /* IDT_HPP */

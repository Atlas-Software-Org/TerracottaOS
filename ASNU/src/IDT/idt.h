#ifndef IDT_H
#define IDT_H 1

#include <stdint.h>
#include <stdbool.h>
#include <KiSimple.h>

#include <VMM/vmm.h>
#include <Drivers/PS2Keyboard.h>
#include <Drivers/PIT.h>

#define IDT_MAX_DESCRIPTORS 256
#define GDT_OFFSET_KERNEL_CODE 0x08

typedef struct {
	uint16_t	limit;
	uint64_t	base;
} __attribute__((packed)) idtr_t;

void idt_set_desc(uint8_t vector, void* isr, uint8_t flags);
idtr_t idt_init(void);
void idt_init_exceptions(void);

#define PIC1		0x20
#define PIC2		0xA0
#define PIC1_COMMAND	PIC1
#define PIC1_DATA	(PIC1+1)
#define PIC2_COMMAND	PIC2
#define PIC2_DATA	(PIC2+1)

void idt_pic_send_eoi(uint8_t irq);
void idt_pic_remap(int offset1, int offset2);
void idt_irq_set_mask(uint8_t IRQline);
void idt_irq_clear_mask(uint8_t IRQline);

#endif /* IDT_H */

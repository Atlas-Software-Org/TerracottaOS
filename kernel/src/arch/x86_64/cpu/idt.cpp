#include <arch/arch.hpp>
#include <arch/x86_64/cpu/idt.hpp>
#include <cstdio>
#include <mem/mem.hpp>
#include <dbg/dbg.hpp>
#include <config.hpp>

bool idt_set_vectors[256] = {false};

const char* exception_names[] = {
	"#DE", "#DB", "#NMI", "#BP", "#OF", "#BR",
	"#UD", "#NM", "#DF", "N/A", "#TS", "#NP",
	"#SS", "#GP", "#PF", "N/A", "#MF", "#AC",
	"#MC", "#XM", "#VE", "#CP", "N/A", "N/A",
	"N/A", "N/A", "N/A", "N/A", "N/A", "N/A",
	"N/A", "N/A",
};

bool is_solvable(int excvec) {
	return false; // for now lets solve problems
	switch (excvec) {
		case 0:
		case 3:
		case 14:
			return true;
		default:
			return false;
	}
}

void fix_exceptions(
	int excvec,
	uint64_t error_code,
	uint64_t rip,
	uint64_t cr2,
	uint64_t cr3
) {
	switch (excvec) {
		case 0: {
			Log::infof("Divide By Zero Exception handled");
			break;
		}
		case 3: {
			Log::infof("Breakpoint Exception handled");
			printf("Breakpoint at RIP: 0x%llX\n", rip);
			while (1) {
				if (arch::x86_64::io::inb(0x3F8) & 1) break;
			}
			break;
		}
		case 14: {
			Log::infof("Page Fault Exception handled");
			printf("Page Fault at CR2: 0x%llX\n", cr2);
			printf("Error Code: %d\n", error_code);

			if (cr2 & ~0xFFF == 0) {
				Log::errf("Null pointer dereference detected");
				asm volatile ("cli;hlt;");
			}

			if (cr2 < 0x00007FFFFFFFFFFF) {
				Log::errf("Invalid user space address access detected");
				asm volatile ("cli;hlt;");
			}

			if (error_code & 0x1) {
				Log::infof("Page fault caused by protection violation");
				asm volatile ("cli;hlt;");
			} else {
				Log::infof("Page fault caused by non-present page");
				mem::vmm::mmap(
					(void*)(cr2 & ~0xFFF),
					(void*)(cr2 & ~0xFFF),
					1,
					PAGE_PRESENT |
					PAGE_RW |
					PAGE_USER
				);
				Log::infof("Mapped page for address 0x%llX", cr2);
			}

			break;
		}
		default:
			Log::errf("No handler for exception vector %d", excvec);
			break;
	}
}

void decode_pf_err(uint64_t err) {
    const char *cause;
    const char *access;
    const char *mode;

    if (err & (1 << 0))
        cause = "a protection violation (page was present)";
    else
        cause = "a non-present page";

    if (err & (1 << 4))
        access = "an instruction fetch";
    else if (err & (1 << 1))
        access = "a write";
    else
        access = "a read";

    if (err & (1 << 2))
        mode = "user mode";
    else
        mode = "supervisor mode";

    printf("Page Fault: caused by %s during %s in %s.\n",
           cause, access, mode);

    if (err & (1 << 3))
        printf("  - Reserved bit violation\n");

    if (err & (1 << 5))
        printf("  - Protection key violation\n");

    if (err & (1 << 6))
        printf("  - Shadow stack violation\n");

    if (err & (1 << 15))
        printf("  - SGX access control violation\n");
}

#include <drivers/input/ps2k/ps2k.hpp>
#include <drivers/input/ps2k/ps2k_key_event.hpp>
#include <drivers/input/ps2k/ps2k_keycodes.hpp>

void clr() {
	printf("\033[2J\033[H");
}

uint64_t __rip = 0;

void debugger_event_handler(key_event& ev, void*) {
	switch (ev.keycode) {
		case KEY_F1:
			clr();
			dbg::memview::print_memory_contents_at(__rip, 100, 0);
			break;
		case KEY_F2:
			clr();
			dbg::disasm::disasm_at_memory(__rip, 100, 0);
			break;
		case KEY_F3:
			clr();
			dbg::stacktrace::stacktrace(__rip, 5, 0);
			break;
		default:
			clr();
			dbg::disasm::disasm_at_memory(__rip, 100, 0);
			break;
	}
}

void debugger(uint64_t rip) {
	__rip = rip;

	drivers::input::ps2k::clear_event_callback();
	drivers::input::ps2k::set_event_callback((event_callback_fn)debugger_event_handler, nullptr);

	clr();
	dbg::disasm::disasm_at_memory(__rip, 100, 0);

	while (true) drivers::input::ps2k::user_ps2k_poll();
}

uint64_t nesting_table[31] = {0};

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
} __attribute__((packed));

extern "C" void exception_handler(exception_frame* frame) {
    uint64_t cr0, cr2, cr3, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    
    uint16_t ds, es, fs, gs;
    asm volatile("mov %%ds, %0" : "=r"(ds));
    asm volatile("mov %%es, %0" : "=r"(es));
    asm volatile("mov %%fs, %0" : "=r"(fs));
    asm volatile("mov %%gs, %0" : "=r"(gs));
    
    uint8_t vec = frame->exception_vector;
    uint64_t err = frame->error_code;
    
    printf("\n\033[91m========== EXCEPTION! ==========\033[0m\n");
    printf("check_exception v=%02x e=%04llx i=0 cpl=%d IP=%04llx:%016llx pc=%016llx CR2=%016llx\n",
           vec, err, (uint32_t)(frame->cs & 3), frame->cs, frame->rip, frame->rip, cr2);
    
    printf("RAX= %016llx RBX= %016llx RCX= %016llx RDX= %016llx\n",
           frame->rax, frame->rbx, frame->rcx, frame->rdx);
    printf("RSI= %016llx RDI= %016llx RBP= %016llx RSP= %016llx\n",
           frame->rsi, frame->rdi, frame->rbp, frame->rsp);
    printf("R8=  %016llx R9=  %016llx R10= %016llx R11= %016llx\n",
           frame->r8, frame->r9, frame->r10, frame->r11);
    printf("R12= %016llx R13= %016llx R14= %016llx R15= %016llx\n",
           frame->r12, frame->r13, frame->r14, frame->r15);
    
    printf("RIP= %016llx RFL= %08llx [", frame->rip, frame->rflags);
    printf("%c", (frame->rflags & (1 << 11)) ? 'O' : '-');
    printf("%c", (frame->rflags & (1 << 10)) ? 'D' : '-');
    printf("%c", (frame->rflags & (1 << 9))  ? 'I' : '-');
    printf("%c", (frame->rflags & (1 << 7))  ? 'S' : '-');
    printf("%c", (frame->rflags & (1 << 6))  ? 'Z' : '-');
    printf("%c", (frame->rflags & (1 << 4))  ? 'A' : '-');
    printf("%c", (frame->rflags & (1 << 2))  ? 'P' : '-');
    printf("%c", (frame->rflags & (1 << 0))  ? 'C' : '-');
    printf("] CPL=%d\n", (uint32_t)(frame->cs & 3));
    
    printf("ES=  %04x DS=  %04x SS=  %04llx CS=  %04llx FS=  %04x GS=  %04x\n",
           es, ds, frame->ss, frame->cs, fs, gs);
    
    printf("CR0= %08llx CR2= %016llx CR3= %016llx CR4= %08llx\n",
           cr0, cr2, cr3, cr4);
    
    printf("Exception: %s (vector=%d, error=%llx)\n", 
           exception_names[vec], vec, err);

	nesting_table[vec]++;
	if (nesting_table[vec] == EXCEPTIONS_CFG_NESTING_THRESHOLD) {
		decode_pf_err(err);
		printf("\033[91mNesting threshold reached at %d %s exceptions...\033[0m", EXCEPTIONS_CFG_NESTING_THRESHOLD, exception_names[vec]);
		asm ("cli;hlt");
	}

	if (vec == 0xE) {
		decode_pf_err(err);
#ifdef EXCEPTIONS_CFG_RUN_DEBUGGER
		debugger(frame->rip);
#endif
	}

	nesting_table[vec]--;
    
    printf("\033[91m========== END OF MSG ==========\033[0m\n");

    if (!is_solvable(vec)) {
        asm volatile ("cli; hlt");
    }

    Log::infof("Exception is solvable");
    fix_exceptions(vec, err, frame->rip, cr2, cr3);
}

#define PIC1		0x20
#define PIC2		0xA0
#define PIC1_COMMAND	PIC1
#define PIC1_DATA	(PIC1+1)
#define PIC2_COMMAND	PIC2
#define PIC2_DATA	(PIC2+1)
#define PIC_EOI		0x20

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

#define CASCADE_IRQ 2

namespace arch::x86_64::cpu::idt {

alignas(16) static idt_t idt;
alignas(16) static idtr_t idtr;

void load_idt() {
	idt_load(&idtr);
}

extern "C" uint64_t exception_stub_table[];

static void pic_remap(int offset1, int offset2) {
	arch::x86_64::io::outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
	arch::x86_64::io::io_wait();
	arch::x86_64::io::outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
	arch::x86_64::io::io_wait();
	arch::x86_64::io::outb(PIC1_DATA, offset1);
	arch::x86_64::io::io_wait();
	arch::x86_64::io::outb(PIC2_DATA, offset2);
	arch::x86_64::io::io_wait();
	arch::x86_64::io::outb(PIC1_DATA, 1 << CASCADE_IRQ);
	arch::x86_64::io::io_wait();
	arch::x86_64::io::outb(PIC2_DATA, 2);
	arch::x86_64::io::io_wait();
	arch::x86_64::io::outb(PIC1_DATA, ICW4_8086);
	arch::x86_64::io::io_wait();
	arch::x86_64::io::outb(PIC2_DATA, ICW4_8086);
	arch::x86_64::io::io_wait();
	arch::x86_64::io::outb(PIC1_DATA, 0xff);
	arch::x86_64::io::outb(PIC2_DATA, 0xff);
}

void irq_clear_mask(uint8_t irq) {
	uint16_t port;
	if (irq < 8) port = PIC1_DATA;
	else { port = PIC2_DATA; irq -= 8; }
	uint8_t value = arch::x86_64::io::inb(port) & ~(1 << irq);
	arch::x86_64::io::outb(port, value);
}

void irq_set_mask(uint8_t irq) {
	uint16_t port;
	if (irq < 8) port = PIC1_DATA;
	else { port = PIC2_DATA; irq -= 8; }
	uint8_t value = arch::x86_64::io::inb(port) | (1 << irq);
	arch::x86_64::io::outb(port, value);
}

__attribute__((interrupt)) void int80_handler(void*) {
	printf("INT 80h");
}

void initialise() {
	for (int i = 0; i < 0x1F; i++) {
		set_descriptor(i, exception_stub_table[i], 0x8E);
	}

	for (int i = 0; i < 16; i++) {
		irq_set_mask(i);
	}

	idtr.limit = sizeof(idt) - 1;
	idtr.base = (uint64_t)&idt;
	
	pic_remap(0x20, 0x28);

	irq_clear_mask(0);

	set_descriptor(0x80, (uint64_t)int80_handler, 0xEE);

	load_idt();
}	

void set_descriptor(uint8_t vector, uint64_t isr, uint8_t flags) {
	idt_entry_t *e = &idt.entries[vector];
	e->isr_offset_low = (isr & 0xFFFF);
	e->gdt_selector = 0x08;
	e->ist = 1;
	e->flags = flags;
	e->isr_offset_middle = (isr >> 16) & 0xFFFF;
	e->isr_offset_high = (isr >> 32) & 0xFFFFFFFF;
	e->always_zero = 0;

	if (0x20 <= vector && vector < 0x30) {
		if (vector < 0x28) irq_clear_mask(vector - 0x20);
		else irq_clear_mask(vector - 0x28 + 8);
	}

	idt_set_vectors[vector] = true;
}

void clear_descriptor(uint8_t vector) {
	if (0x20 <= vector && vector < 0x30) {
		if (vector < 0x28) irq_set_mask(vector - 0x20);
		else irq_set_mask(vector - 0x28 + 8);
	}

	idt_set_vectors[vector] = false;
	set_descriptor(vector, 0, 0);
}

void send_eoi(uint8_t irq) {
	if (irq >= 8) arch::x86_64::io::outb(PIC2_COMMAND, PIC_EOI);
	arch::x86_64::io::outb(PIC1_COMMAND, PIC_EOI);
}

}


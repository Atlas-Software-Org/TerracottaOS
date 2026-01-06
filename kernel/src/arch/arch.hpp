#ifndef ARCH_HPP
#define ARCH_HPP 1

#include <cstdint>

struct interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

namespace arch {
namespace x86_64 {
	namespace io {
		void outb(uint16_t port, uint8_t value);
		uint8_t inb(uint16_t port);
		void outw(uint16_t port, uint16_t value);
		uint16_t inw(uint16_t port);
		void outl(uint16_t port, uint32_t value);
		uint32_t inl(uint16_t port);

		void io_wait();
	}
namespace cpu {
	namespace gdt {
		void load_tss();
		void load_gdt();
		void initialise();

		void update_stack(uint64_t new_rsp = 0);
	}

	namespace idt {
		void load_idt();
		void initialise();
		void set_descriptor(uint8_t vector, uint64_t isr, uint8_t flags);
		void clear_descriptor(uint8_t vector);
		void irq_clear_mask(uint8_t irq);
		void irq_set_mask(uint8_t irq);
		void send_eoi(uint8_t irq);
	}
}
namespace ringctl {
	extern "C" void execute_ring3(void (*entry)(), void* stack_base);
}
namespace syscall {
	void initialise();
}
}
}

#endif /* ARCH_HPP */

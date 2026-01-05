#include "pit.hpp"
#include <cstdio>
#include <config.hpp>

static uint64_t ticks = 0;

#define CHx_DATA(ch) (0x40 + (ch))
#define CHx_MODE_CMD_REG(ch) (0x43)

extern "C" void pit_interrupt_handler();
extern "C" void* pit_c_handler(void* ctx) {
    ticks++;

    arch::x86_64::io::outb(0x20, 0x20);

    return ctx;
}

namespace drivers::timers::pit {

void initialise() {
    using namespace arch::x86_64::io;

#if CONFIG_PIT_FREQUENCY == 0
#	error "CONFIG_PIT_FREQUENCY cannot be 0"
#endif

    outb(CHx_MODE_CMD_REG(0), 0x34);

    uint16_t divisor = 1193180 / CONFIG_PIT_FREQUENCY;
    outb(CHx_DATA(0), static_cast<uint8_t>(divisor & 0xFF));
    io_wait();
    outb(CHx_DATA(0), static_cast<uint8_t>((divisor >> 8) & 0xFF));

    arch::x86_64::cpu::idt::set_descriptor(0x20, (uint64_t)pit_interrupt_handler, 0x8E);
}

void sleep_ms(uint64_t ms) {
    if (CONFIG_PIT_FREQUENCY == 0) return;

    uint64_t current_ticks = ticks;
    uint64_t blocking_ticks = (ms * CONFIG_PIT_FREQUENCY) / 1000;
    uint64_t final_ticks = current_ticks + blocking_ticks;

    while (ticks < final_ticks) {
        asm volatile("hlt");
    }
}

uint64_t ns_elapsed_time() {
    return ticks * 10000000;
}

}

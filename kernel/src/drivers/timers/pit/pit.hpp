#ifndef PIT_HPP
#define PIT_HPP 1

#include <cstdint>
#include <arch/arch.hpp>

namespace drivers::timers::pit {
    void initialise();
    void sleep_ms(uint64_t ms);
    uint64_t ns_elapsed_time();
    void attach_periodic_interrupt(void (*handler)(), uint64_t frequency_divisor);
}

#endif

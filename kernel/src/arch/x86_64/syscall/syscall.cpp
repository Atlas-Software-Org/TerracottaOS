#include "syscall.hpp"
#include <cstdio>
#include "syscalls/handlers.hpp"

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = value & 0xFFFFFFFF;
    uint32_t hi = value >> 32;
    asm volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

#define IA32_EFER 0xC0000080
#define IA32_STAR 0xC0000081
#define IA32_LSTAR 0xC0000082
#define IA32_FMASK 0xC0000084

extern "C" void syscall_func();
extern "C" uint64_t syscall_handler(uint64_t rax, uint64_t rdi, uint64_t rsi,
						 uint64_t rdx, uint64_t r10, uint64_t r8, uint64_t r9) {
	return handle_syscall(rax, rdi, rsi, rdx, r10, r9, r8);
}


#define HANDLER(handler) ((uint64_t*)handler)

namespace arch::x86_64::syscall {

void initialise() {
	uint64_t star = ((uint64_t)0x08 << 32)
    				| ((uint64_t)0x10 << 48);
	
	uint64_t efer = rdmsr(IA32_EFER);
	efer |= 1; // syscalls enabled
	wrmsr(IA32_EFER, efer);
	wrmsr(IA32_STAR, star);
	wrmsr(IA32_LSTAR, (uint64_t)&syscall_func);
	wrmsr(IA32_FMASK, 0);
}

}

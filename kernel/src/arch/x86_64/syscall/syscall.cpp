#include "syscall.hpp"
#include <cstdio>
#include "syscalls/handlers.hpp"

syscall_entry syscalls[512];
int num_syscalls = 0;

void add_syscall(int num, uint64_t* handler) {
	if (num_syscalls >= 512) return;
	syscalls[num_syscalls].num = num;
	syscalls[num_syscalls].func = handler;
	num_syscalls++;
}

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

extern uint64_t _tss_rsp, _tss_rbp;

extern "C" void syscall_func();
extern "C" uint64_t syscall_handler(uint64_t rax, uint64_t rdi, uint64_t rsi,
						 uint64_t rdx, uint64_t r10, uint64_t r8, uint64_t r9) {
	printf("Got syscall\n\r");
	return (uint64_t)-1;
}


#define HANDLER(handler) ((uint64_t*)handler)

namespace arch::x86_64::syscall {

void initialise() {
	uint64_t star = ((uint64_t)0x08 << 32)
    				| ((uint64_t)0x10 << 48)
    				| ((uint64_t)0x23 << 0)
    				| ((uint64_t)0x1B << 16);

	uint64_t efer = rdmsr(IA32_EFER);
	efer |= 1; // syscalls enabled
	wrmsr(IA32_EFER, efer);
	wrmsr(IA32_STAR, star);
	wrmsr(IA32_LSTAR, (uint64_t)&syscall_func);
	wrmsr(IA32_FMASK, 0);

	add_syscall(2, HANDLER(sys_open));
	add_syscall(3, HANDLER(sys_close));
	add_syscall(0, HANDLER(sys_read));
	add_syscall(1, HANDLER(sys_write));
	add_syscall(17, HANDLER(sys_pread));
	add_syscall(18, HANDLER(sys_pwrite));
	add_syscall(8, HANDLER(sys_lseek));
	add_syscall(5, HANDLER(sys_stat));
	add_syscall(90, HANDLER(sys_chmod));
	add_syscall(92, HANDLER(sys_chown));
	add_syscall(77, HANDLER(sys_truncate));
	add_syscall(74, HANDLER(sys_sync));
	add_syscall(75, HANDLER(sys_datasync));
	add_syscall(83, HANDLER(sys_mkdir));
	add_syscall(80, HANDLER(sys_chdir));
	add_syscall(86, HANDLER(sys_link));
	add_syscall(87, HANDLER(sys_unlink));
	add_syscall(82, HANDLER(sys_rename));
	add_syscall(88, HANDLER(sys_symlink));
	add_syscall(89, HANDLER(sys_readlink));
	add_syscall(84, HANDLER(sys_rmdir));
	add_syscall(78, HANDLER(sys_getdents));
}

}

#include "handlers.hpp"
#include "proc/spinlocks.hpp"
#include <cstring>
#include <cstdint>
#include <cstdio>

#define NUM_SYSCALLS 10

size_t registered_syscalls = 0;

struct syscall_entry {
    union {
        uint64_t (*handler0)(void);
        uint64_t (*handler1)(uint64_t);
        uint64_t (*handler2)(uint64_t, uint64_t);
        uint64_t (*handler3)(uint64_t, uint64_t, uint64_t);
        uint64_t (*handler4)(uint64_t, uint64_t, uint64_t, uint64_t);
        uint64_t (*handler5)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
        uint64_t (*handler6)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
        void* raw;
    } handler;
    uint8_t num_args;
    char name[96];
    char cpp_pretty_func[96];
    bool allocated = false;
};

struct {
    syscall_entry entry[NUM_SYSCALLS];
    spinlock* lock;
} syscall_table;

size_t initialise_syscall_handlers() {
    syscall_table.lock = new_spinlock("SC_TABLE.LOCK");
    initialise_syscalls();
    return registered_syscalls;
}

inline uint64_t call_syscall(uint8_t argc, uint64_t nr,
                             uint64_t a, uint64_t b, uint64_t c,
                             uint64_t d, uint64_t e, uint64_t f) {
    switch(argc) {
        case 0: return syscall_table.entry[nr].handler.handler0();
        case 1: return syscall_table.entry[nr].handler.handler1(a);
        case 2: return syscall_table.entry[nr].handler.handler2(a,b);
        case 3: return syscall_table.entry[nr].handler.handler3(a,b,c);
        case 4: return syscall_table.entry[nr].handler.handler4(a,b,c,d);
        case 5: return syscall_table.entry[nr].handler.handler5(a,b,c,d,e);
        case 6: return syscall_table.entry[nr].handler.handler6(a,b,c,d,e,f);
        default: return (uint64_t)-1;
    }
}

uint64_t handle_syscall(uint64_t rax, uint64_t rdi, uint64_t rsi,
                        uint64_t rdx, uint64_t r10, uint64_t r9,
                        uint64_t r8) {
    if (!(0 <= rax && rax < NUM_SYSCALLS)) {
    	return (uint64_t)-1;
    } else {
    	if (!syscall_table.entry[rax].allocated) return (uint64_t)-1;
    }

    uint8_t argc = syscall_table.entry[rax].num_args;

    return call_syscall(argc, rax, rdi, rsi, rdx, r10, r9, r8);
}

void register_syscall(uint64_t vector, void* handler, uint64_t num_args,
                      const char* name, const char* cpp_pretty_func) {
#ifdef CONFIG_ALLOW_SYSCALL_REGISTERATION_OVERRIDE
    if (vector >= NUM_SYSCALLS)
        return;
#else
    if (vector >= NUM_SYSCALLS || syscall_table.entry[vector].allocated)
        return;
#endif

    acquire_spinlock(syscall_table.lock);
    syscall_table.entry[vector].allocated = true;
    syscall_table.entry[vector].handler.raw = handler;
    syscall_table.entry[vector].num_args = static_cast<uint8_t>(num_args);

    strncpy(syscall_table.entry[vector].name, name,
            sizeof(syscall_table.entry[vector].name) - 1);
    syscall_table.entry[vector].name[sizeof(syscall_table.entry[vector].name)-1] = 0;

    strncpy(syscall_table.entry[vector].cpp_pretty_func, cpp_pretty_func,
            sizeof(syscall_table.entry[vector].cpp_pretty_func) - 1);
    syscall_table.entry[vector].cpp_pretty_func[sizeof(syscall_table.entry[vector].cpp_pretty_func)-1] = 0;

    registered_syscalls++;
    release_spinlock(syscall_table.lock);
}

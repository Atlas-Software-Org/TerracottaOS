#ifndef SCHEDULER_H
#define SCHEDULER_H 1

#include <stdint.h>
#include <stdbool.h>

extern uint32_t SchedTickFreq;

typedef enum {
    PROC_NEW = 0,
    PROC_READY = 1,
    PROC_RUNNING = 2,
    PROC_WAITING = 3,
    PROC_SLEEPING = 4,
    PROC_TERMINATED = 5,
    PROC_SUSPENDED = 6,
    PROC_IDLE = 7
} SchedulerState;

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cr3;
    uint16_t cs, ds, es, fs, gs, ss;
} __attribute__((packed)) RegisterState;

typedef struct {
    uint32_t Id;
    uint32_t ParentCpuId;
    SchedulerState State;
    uint64_t EntryPoint;
    uint64_t KernelStack;
    uint64_t UserStack;
    uint64_t PagesMapped;
    uint64_t TimeUsedNs;
    uint64_t TimeSliceNs;
    bool IsKernelProcedure;
    RegisterState Regs;
} __attribute__((packed)) CPUState;

typedef struct {
    uint32_t pid;
    SchedulerState proc_state;
    uint8_t privilege_level;
    uint32_t thread_id;
    CPUState state;
} __attribute__((packed)) Procedure;

void scheduler_tick(void);
void context_switch(void);
Procedure *scheduler_get_current(void);
Procedure *create_proc(uint64_t entry_point, int argc, char** argv, char** envp, uint8_t privilege_level, uint64_t stack_base, uint64_t stack_size,
                      uint64_t heap_base, uint64_t heap_size);
void register_proc(Procedure *proc);
void scheduler_init(void);

#endif /* SCHEDULER_H */
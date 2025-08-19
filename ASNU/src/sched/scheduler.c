#include "scheduler.h"
#include <string.h>
#include <PMM/pmm.h>
#include <Serial/serial.h>

#define MAX_PROCS 1024

uint32_t SchedTickFreq = 10;
static Procedure *proc_list[MAX_PROCS];
static size_t proc_count = 0;
static Procedure *current_proc = NULL;
static uint32_t next_pid = 1;

Procedure *scheduler_get_current(void) {
    return current_proc;
}

Procedure *create_proc(uint64_t entry_point, int argc, char** argv, char** envp, uint8_t privilege_level, uint64_t stack_base, uint64_t stack_size,
                      uint64_t heap_base, uint64_t heap_size) {
    Procedure *p = (Procedure*)kalloc(sizeof(Procedure));
    if (!p) return NULL;
    memset(p, 0, sizeof(Procedure));

    p->pid = next_pid++;
    p->proc_state = PROC_NEW;
    p->privilege_level = privilege_level & 0x3;
    p->thread_id = 0;
    p->state.Id = p->pid;
    p->state.EntryPoint = entry_point;
    p->state.KernelStack = stack_base;
    p->state.UserStack = stack_base + stack_size;
    p->state.PagesMapped = 0;
    p->state.TimeUsedNs = 0;
    p->state.TimeSliceNs = 0;
    p->state.IsKernelProcedure = (privilege_level == 0);

    // Initialize registers
    p->state.Regs.rip = entry_point;
    p->state.Regs.rflags = 0x202;

    // Prepare a safe initial stack for first run
    uint64_t *stack_top = (uint64_t*)p->state.UserStack;
    stack_top--;
    *stack_top = 0;  // fake return address
    p->state.Regs.rsp = (uint64_t)stack_top;

    return p;
}

void register_proc(Procedure *proc) {
    if (proc_count >= MAX_PROCS) return;
    proc_list[proc_count++] = proc;
    proc->proc_state = PROC_READY;
    if (!current_proc) {
        current_proc = proc;
        proc->proc_state = PROC_RUNNING;
    }
}

void scheduler_init(void) {
    proc_count = 0;
    current_proc = NULL;
    next_pid = 1;
}

void scheduler_tick(void) {
    static uint64_t sched_ticks = 0;
    sched_ticks++;

    serial_fwrite("Scheduler ticked");
    if (sched_ticks % SchedTickFreq == 0) {
        serial_fwrite("Scheduler tick frequency rule met");
        context_switch();
        serial_fwrite("Scheduler tick: %llu, current PID: %u\n", sched_ticks, current_proc ? current_proc->pid : 0);
    }
}

static Procedure *find_next_proc(void) {
    if (proc_count == 0) return NULL;
    size_t start = 0;
    if (current_proc) {
        for (size_t i = 0; i < proc_count; ++i) {
            if (proc_list[i] == current_proc) {
                start = (i + 1) % proc_count;
                break;
            }
        }
    }
    for (size_t i = 0; i < proc_count; ++i) {
        size_t idx = (start + i) % proc_count;
        Procedure *p = proc_list[idx];
        if (p->proc_state == PROC_READY || p->proc_state == PROC_IDLE)
            return p;
    }
    return NULL;
}

void context_switch(void) {
    Procedure *next = find_next_proc();
    if (!next) return;
    Procedure *prev = current_proc;
    if (prev == next) return;

    // Save current process registers
    if (prev) {
        asm volatile(
            "mov %%rax, %0\n\t" "mov %%rbx, %1\n\t" "mov %%rcx, %2\n\t" "mov %%rdx, %3\n\t"
            "mov %%rsi, %4\n\t" "mov %%rdi, %5\n\t" "mov %%rbp, %6\n\t" "mov %%rsp, %7\n\t"
            "mov %%r8, %8\n\t" "mov %%r9, %9\n\t" "mov %%r10, %10\n\t" "mov %%r11, %11\n\t"
            "mov %%r12, %12\n\t" "mov %%r13, %13\n\t" "mov %%r14, %14\n\t" "mov %%r15, %15\n\t"
            "pushfq\n\t" "popq %16\n\t"
            : "=m"(prev->state.Regs.rax), "=m"(prev->state.Regs.rbx), "=m"(prev->state.Regs.rcx),
              "=m"(prev->state.Regs.rdx), "=m"(prev->state.Regs.rsi), "=m"(prev->state.Regs.rdi),
              "=m"(prev->state.Regs.rbp), "=m"(prev->state.Regs.rsp), "=m"(prev->state.Regs.r8),
              "=m"(prev->state.Regs.r9), "=m"(prev->state.Regs.r10), "=m"(prev->state.Regs.r11),
              "=m"(prev->state.Regs.r12), "=m"(prev->state.Regs.r13), "=m"(prev->state.Regs.r14),
              "=m"(prev->state.Regs.r15), "=m"(prev->state.Regs.rflags)
            :
            : "memory"
        );
        prev->proc_state = PROC_READY;
    }

    next->proc_state = PROC_RUNNING;
    current_proc = next;

    // Restore next process registers and jump
    asm volatile(
        "mov %0, %%rax\n\t" "mov %1, %%rbx\n\t" "mov %2, %%rcx\n\t" "mov %3, %%rdx\n\t"
        "mov %4, %%rsi\n\t" "mov %5, %%rdi\n\t" "mov %6, %%rbp\n\t" "mov %7, %%rsp\n\t"
        "mov %8, %%r8\n\t" "mov %9, %%r9\n\t" "mov %10, %%r10\n\t" "mov %11, %%r11\n\t"
        "mov %12, %%r12\n\t" "mov %13, %%r13\n\t" "mov %14, %%r14\n\t" "mov %15, %%r15\n\t"
        "pushq %16\n\t" "popfq\n\t" "jmp *%17\n\t"
        :
        : "m"(next->state.Regs.rax), "m"(next->state.Regs.rbx), "m"(next->state.Regs.rcx),
          "m"(next->state.Regs.rdx), "m"(next->state.Regs.rsi), "m"(next->state.Regs.rdi),
          "m"(next->state.Regs.rbp), "m"(next->state.Regs.rsp), "m"(next->state.Regs.r8),
          "m"(next->state.Regs.r9), "m"(next->state.Regs.r10), "m"(next->state.Regs.r11),
          "m"(next->state.Regs.r12), "m"(next->state.Regs.r13), "m"(next->state.Regs.r14),
          "m"(next->state.Regs.r15), "m"(next->state.Regs.rflags),
          "m"(next->state.Regs.rip)
        : "memory"
    );
}

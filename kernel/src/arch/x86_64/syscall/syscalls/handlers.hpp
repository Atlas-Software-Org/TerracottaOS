#ifndef HANDLERS_HPP
#define HANDLERS_HPP 1

#include <cstdint>
#include <cstddef>

size_t initialise_syscall_handlers();
void initialise_syscalls();

uint64_t handle_syscall(uint64_t rax, uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t r10, uint64_t r8, uint64_t r9);
void register_syscall(uint64_t vector, void* handler, uint64_t num_args, const char* name, const char* cpp_pretty_func);

#endif

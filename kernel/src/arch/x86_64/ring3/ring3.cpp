#include <arch/arch.hpp>

extern "C" void exec_ring3_helper(uint64_t rsp) {
	arch::x86_64::cpu::gdt::update_stack(rsp);
}

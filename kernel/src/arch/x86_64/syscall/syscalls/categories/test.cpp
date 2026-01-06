#include "../handlers.hpp"
#include <cstdio>

uint64_t test(uint64_t num) {
	printf("Test %s! %zu\n\r", __PRETTY_FUNCTION__, num);

	return 0;
}

void initialise_syscalls() {
	register_syscall(0, (void*)test, 1, "test", "void test(uint64_t num)");
}

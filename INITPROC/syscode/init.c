#include <sys/syscalls.h>

void _start() {
	sys_test(123);

    while (1) {
    }
}

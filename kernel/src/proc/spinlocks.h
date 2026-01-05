#ifndef CSPINLOCKS
#define CSPINLOCKS 1

#include "stdint.h"

struct spinlock {
	char name[16];
	volatile uint8_t locked;
};

void c_acquire_spinlock(struct spinlock* lock);
void c_release_spinlock(struct spinlock* lock);

#endif

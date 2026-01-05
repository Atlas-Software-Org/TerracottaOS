#include "spinlocks.hpp"
#include <mem/mem.hpp>
#include <stdint.h>

struct opaque_filled_spinlock {
    char name[16];
    volatile uint8_t locked;
};

spinlock* new_spinlock(const char* name) {
    auto* lock = (opaque_filled_spinlock*)mem::heap::malloc(sizeof(opaque_filled_spinlock));

    for (int i = 0; i < 15 && name[i] != 0; i++)
        lock->name[i] = name[i];
    lock->name[15] = 0;

    lock->locked = 0;
    return (spinlock*)lock;
}

void delete_spinlock(spinlock* lock) {
    acquire_spinlock(lock); // ensure no one else is using it
    mem::heap::free(lock);
}

static inline uint8_t atomic_test_and_set(volatile uint8_t* ptr) {
    uint8_t old;
    asm volatile(
        "lock xchg %0, %1"
        : "=r"(old), "+m"(*ptr)
        : "0"(1)
        : "memory"
    );
    return old;
}

void acquire_spinlock(spinlock* lock) {
    auto* l = (opaque_filled_spinlock*)lock;
    while (atomic_test_and_set(&l->locked)) {
        asm volatile("pause");
    }
}

void release_spinlock(spinlock* lock) {
    auto* l = (opaque_filled_spinlock*)lock;
    asm volatile("" ::: "memory");
    l->locked = 0;
}

#ifndef SPINLOCKS_HPP
#define SPINLOCKS_HPP 1

struct spinlock;

spinlock* new_spinlock(const char* name = "???"); 
void delete_spinlock(spinlock* lock);

void acquire_spinlock(spinlock* lock);
void release_spinlock(spinlock* lock);

#endif

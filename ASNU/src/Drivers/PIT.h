#ifndef PIT_H
#define PIT_H 1

#include <KiSimple.h>
#include <IDT/idt.h>
#include <stdint.h>

#define PIT_FREQ			1193182
#define PIT_CMD_PORT		0x43
#define PIT_CHANNEL0_PORT	0x40

void pit_init(uint32_t Freq);
uint64_t pit_get_ticks();
uint64_t pit_wait_ticks(uint64_t Ticks);
void pit_wait_ms(uint64_t Ms);

__attribute__((interrupt)) void pit_handler(int *__unused);

#endif /* PIT_H */

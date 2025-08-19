#include "../PIT.h"
#include <sched/scheduler.h>

static volatile uint64_t PitTicks = 0;

__attribute__((interrupt)) void pit_handler(int *__unused) {
	(void)__unused;

	PitTicks++;

	scheduler_tick();

	idt_pic_send_eoi(0);
}

static volatile uint64_t PitTickFreq = 0;

void pit_init(uint32_t Freq) {
	uint32_t divisor = PIT_FREQ / Freq;
	outb(PIT_CMD_PORT, 0x36);
	outb(PIT_CHANNEL0_PORT, (uint8_t)(divisor & 0xFF));
	outb(PIT_CHANNEL0_PORT, (uint8_t)((divisor >> 8) & 0xFF));
	PitTickFreq = Freq;
}

uint64_t pit_get_ticks() {
	return PitTicks;
}

uint64_t pit_wait_ticks(uint64_t Ticks) {
	uint64_t start = pit_get_ticks();
	while ((pit_get_ticks() - start) < Ticks);
	return pit_get_ticks();
}

void pit_wait_ms(uint64_t Ms) {
	uint64_t ticks = (Ms * PitTickFreq) / 1000;
	pit_wait_ticks(ticks);
}

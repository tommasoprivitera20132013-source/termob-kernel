#include "../include/timer.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND       0x43
#define PIT_BASE_HZ       1193182U
#define TIMER_FREQUENCY_HZ 100U

static volatile uint32_t timer_ticks;

static inline void timer_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void timer_init(void) {
    uint16_t divisor;

    divisor = (uint16_t)(PIT_BASE_HZ / TIMER_FREQUENCY_HZ);
    timer_ticks = 0;

    timer_outb(PIT_COMMAND, 0x36);
    timer_outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    timer_outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
}

void timer_handle_tick(void) {
    timer_ticks++;
}

uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

uint32_t timer_get_frequency_hz(void) {
    return TIMER_FREQUENCY_HZ;
}

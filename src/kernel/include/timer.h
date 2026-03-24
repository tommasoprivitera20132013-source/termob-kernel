#ifndef TERMOB_TIMER_H
#define TERMOB_TIMER_H

#include <stdint.h>

void timer_init(void);
void timer_handle_tick(void);
uint32_t timer_get_ticks(void);
uint32_t timer_get_frequency_hz(void);

#endif

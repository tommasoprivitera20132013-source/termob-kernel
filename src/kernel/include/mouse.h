#ifndef TERMOB_MOUSE_H
#define TERMOB_MOUSE_H

#include <stdint.h>

void mouse_init(void);
int mouse_is_initialized(void);
int mouse_has_wheel(void);
void mouse_handle_irq(void);
void mouse_drain_pending(void);
void mouse_dump_to_terminal(void);
uint32_t mouse_irq_count(void);
uint32_t mouse_packet_count(void);
uint32_t mouse_scroll_up_count(void);
uint32_t mouse_scroll_down_count(void);

#endif

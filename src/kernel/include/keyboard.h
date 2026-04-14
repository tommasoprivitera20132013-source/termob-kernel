#ifndef TERMOB_KEYBOARD_H
#define TERMOB_KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
int keyboard_has_pending_scancode(void);
uint8_t keyboard_read_scancode(void);
void keyboard_enqueue_scancode(uint8_t scancode);
void keyboard_drain_pending(void);
void keyboard_process_scancode(uint8_t scancode);

#endif

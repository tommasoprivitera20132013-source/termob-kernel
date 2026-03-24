#ifndef TERMOB_KEYBOARD_H
#define TERMOB_KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
uint8_t keyboard_read_scancode(void);
void keyboard_process_scancode(uint8_t scancode);

#endif

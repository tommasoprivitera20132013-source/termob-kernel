#ifndef TERMOB_SERIAL_H
#define TERMOB_SERIAL_H

#include <stdint.h>

void serial_init(void);
int serial_is_enabled(void);
void serial_write_char(char c);
void serial_write(const char* text);
void serial_writeline(const char* text);
void serial_write_hex32(uint32_t value);

#endif

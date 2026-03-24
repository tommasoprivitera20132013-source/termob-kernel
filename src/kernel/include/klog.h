#ifndef TERMOB_KLOG_H
#define TERMOB_KLOG_H

#include <stddef.h>

void klog_init(void);
void klog_clear(void);
void klog_write(const char* text);
void klog_writeline(const char* text);
void klog_dump_to_terminal(void);
size_t klog_size(void);

#endif

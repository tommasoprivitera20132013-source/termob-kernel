#include "../include/klog.h"
#include "../include/serial.h"
#include "../include/terminal.h"

#define KLOG_CAPACITY 4096

static char klog_buffer[KLOG_CAPACITY];
static size_t klog_length;

static void klog_append_char(char c) {
    size_t i;

    if (klog_length < KLOG_CAPACITY - 1) {
        klog_buffer[klog_length++] = c;
        klog_buffer[klog_length] = '\0';
        return;
    }

    for (i = 1; i < KLOG_CAPACITY - 1; i++) {
        klog_buffer[i - 1] = klog_buffer[i];
    }

    klog_buffer[KLOG_CAPACITY - 2] = c;
    klog_buffer[KLOG_CAPACITY - 1] = '\0';
}

void klog_init(void) {
    klog_length = 0;
    klog_buffer[0] = '\0';
}

void klog_clear(void) {
    klog_init();
}

void klog_write(const char* text) {
    size_t i;

    i = 0;
    while (text[i] != '\0') {
        klog_append_char(text[i]);
        i++;
    }
}

void klog_writeline(const char* text) {
    klog_write(text);
    klog_append_char('\n');
}

void klog_dump_to_terminal(void) {
    terminal_write(klog_buffer);
    if (klog_length > 0 && klog_buffer[klog_length - 1] != '\n') {
        terminal_putchar('\n');
    }
}

size_t klog_size(void) {
    return klog_length;
}

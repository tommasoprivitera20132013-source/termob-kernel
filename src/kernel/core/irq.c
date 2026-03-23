#include "../include/terminal.h"

static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void irq1_handler() {
    unsigned char scancode = inb(0x60);

    terminal_setcolor(10, 0);
    terminal_write("KEY ");

    (void)scancode;

    __asm__ volatile ("outb %0, %1" : : "a"(0x20), "Nd"(0x20));
}

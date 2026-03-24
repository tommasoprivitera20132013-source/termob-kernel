#include "../include/serial.h"

#define COM1_BASE         0x3F8
#define COM_DATA          (COM1_BASE + 0)
#define COM_INTERRUPT_EN  (COM1_BASE + 1)
#define COM_FIFO_CONTROL  (COM1_BASE + 2)
#define COM_LINE_CONTROL  (COM1_BASE + 3)
#define COM_MODEM_CONTROL (COM1_BASE + 4)
#define COM_LINE_STATUS   (COM1_BASE + 5)

static int serial_enabled;

static inline void serial_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t serial_inb(uint16_t port) {
    uint8_t value;

    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_write_raw(char c) {
    while ((serial_inb(COM_LINE_STATUS) & 0x20) == 0) {
    }

    serial_outb(COM_DATA, (uint8_t)c);
}

void serial_init(void) {
    serial_enabled = 0;

    serial_outb(COM_INTERRUPT_EN, 0x00);
    serial_outb(COM_LINE_CONTROL, 0x80);
    serial_outb(COM_DATA, 0x03);
    serial_outb(COM_INTERRUPT_EN, 0x00);
    serial_outb(COM_LINE_CONTROL, 0x03);
    serial_outb(COM_FIFO_CONTROL, 0xC7);
    serial_outb(COM_MODEM_CONTROL, 0x1E);
    serial_outb(COM_DATA, 0xAE);

    if (serial_inb(COM_DATA) != 0xAE) {
        return;
    }

    serial_outb(COM_MODEM_CONTROL, 0x0F);
    serial_enabled = 1;
}

int serial_is_enabled(void) {
    return serial_enabled;
}

void serial_write_char(char c) {
    if (!serial_enabled) {
        return;
    }

    if (c == '\n') {
        serial_write_raw('\r');
    }

    serial_write_raw(c);
}

void serial_write(const char* text) {
    int i;

    if (!serial_enabled) {
        return;
    }

    i = 0;
    while (text[i] != '\0') {
        serial_write_char(text[i]);
        i++;
    }
}

void serial_writeline(const char* text) {
    serial_write(text);
    serial_write_char('\n');
}

void serial_write_hex32(uint32_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";
    int shift;

    if (!serial_enabled) {
        return;
    }

    serial_write("0x");

    for (shift = 28; shift >= 0; shift -= 4) {
        serial_write_char(hex_digits[(value >> shift) & 0x0F]);
    }
}

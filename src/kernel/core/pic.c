#include "../include/pic.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

static inline void pic_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t pic_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void pic_io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        pic_outb(PIC2_COMMAND, PIC_EOI);
    }

    pic_outb(PIC1_COMMAND, PIC_EOI);
}

void pic_init(void) {
    uint8_t master_mask;
    uint8_t slave_mask;

    master_mask = pic_inb(PIC1_DATA);
    slave_mask = pic_inb(PIC2_DATA);

    pic_outb(PIC1_COMMAND, 0x11);
    pic_io_wait();
    pic_outb(PIC2_COMMAND, 0x11);
    pic_io_wait();

    pic_outb(PIC1_DATA, 0x20);
    pic_io_wait();
    pic_outb(PIC2_DATA, 0x28);
    pic_io_wait();

    pic_outb(PIC1_DATA, 0x04);
    pic_io_wait();
    pic_outb(PIC2_DATA, 0x02);
    pic_io_wait();

    pic_outb(PIC1_DATA, 0x01);
    pic_io_wait();
    pic_outb(PIC2_DATA, 0x01);
    pic_io_wait();

    (void)master_mask;
    (void)slave_mask;

    pic_outb(PIC1_DATA, 0xFC);
    pic_outb(PIC2_DATA, 0xFF);
}

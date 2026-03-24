#include "../include/pic.h"
#include "../include/keyboard.h"
#include "../include/timer.h"

void irq0_handler_c(void) {
    timer_handle_tick();
    pic_send_eoi(0);
}

void irq1_handler_c(void) {
    uint8_t scancode;

    scancode = keyboard_read_scancode();
    keyboard_process_scancode(scancode);
    pic_send_eoi(1);
}

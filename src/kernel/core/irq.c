#include "../include/pic.h"
#include "../include/keyboard.h"
#include "../include/mouse.h"
#include "../include/timer.h"

void irq0_handler_c(void) {
    timer_handle_tick();
    pic_send_eoi(0);
}

void irq1_handler_c(void) {
    if (keyboard_has_pending_scancode()) {
        keyboard_enqueue_scancode(keyboard_read_scancode());
    }

    pic_send_eoi(1);
}

void irq12_handler_c(void) {
    mouse_handle_irq();
    pic_send_eoi(12);
}

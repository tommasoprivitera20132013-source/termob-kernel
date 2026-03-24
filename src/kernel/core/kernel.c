#include "../include/kernel.h"
#include "../include/terminal.h"
#include "../include/keyboard.h"
#include "../include/kernel_ui.h"
#include "../include/idt.h"
#include "../include/klog.h"
#include "../include/pic.h"
#include "../include/serial.h"
#include "../include/timer.h"

#define IRQ0_VECTOR 32
#define TERMOB_SHELL_TOP 8
#define TERMOB_SHELL_BOTTOM 22
#define IRQ1_VECTOR 33

extern void irq0_stub(void);
extern void irq1_stub(void);

static void kernel_ui_write_boot_line(size_t y, const char* message) {
    terminal_set_cursor(1, y);

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write("[    0.000000] ");

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeline(message);
}

static void kernel_ui_draw_header(void) {
    uint8_t bar_color;

    bar_color = terminal_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);

    terminal_initialize(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_draw_hline(0, ' ', bar_color);
    terminal_draw_hline(24, ' ', bar_color);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);
    terminal_writeat(TERMOB_KERNEL_NAME " " TERMOB_KERNEL_VERSION, 1, 0);

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    terminal_writeat("tty0", 75, 0);
}

static void kernel_ui_draw_boot_log(void) {
    kernel_ui_write_boot_line(2, TERMOB_KERNEL_NAME " booting on " TERMOB_KERNEL_ARCH);
    kernel_ui_write_boot_line(3, "console [tty0] enabled, VGA text mode 80x25");
    kernel_ui_write_boot_line(4, "irq: PIC remapped, PIT 100 Hz, keyboard IRQ1 active");
    kernel_ui_write_boot_line(5, "serial: COM1 debug channel online");
}

static void kernel_ui_draw_footer(void) {
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    terminal_writeat("root shell | pit 100Hz | irq0 irq1 | serial com1", 1, 24);
}

void kernel_draw_ui(void) {
    kernel_ui_draw_header();
    kernel_ui_draw_boot_log();
    kernel_ui_draw_footer();

    terminal_set_region(TERMOB_SHELL_TOP, TERMOB_SHELL_BOTTOM);
    terminal_set_cursor(1, TERMOB_SHELL_TOP);
    terminal_prompt();
}

void kernel_enter_shell(void) {
    terminal_set_region(TERMOB_SHELL_TOP, TERMOB_SHELL_BOTTOM);
    terminal_set_cursor(1, TERMOB_SHELL_TOP);
    terminal_prompt();
}

void kernel_main(void) {
    serial_init();
    klog_init();

    klog_writeline("TERMOB: serial online");
    klog_writeline("TERMOB: kernel boot start");
    serial_writeline("TERMOB: serial online");
    serial_writeline("TERMOB: kernel boot start");

    kernel_draw_ui();
    keyboard_init();

    idt_init();
    idt_set_gate(IRQ0_VECTOR, (uint32_t)irq0_stub);
    idt_set_gate(IRQ1_VECTOR, (uint32_t)irq1_stub);
    pic_init();
    timer_init();

    klog_writeline("TERMOB: idt pic pit keyboard ready");
    serial_writeline("TERMOB: idt pic pit keyboard ready");

    __asm__ volatile ("sti");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

#include "../include/kernel.h"
#include "../include/terminal.h"
#include "../include/keyboard.h"
#include "../include/kernel_ui.h"

#define TERMOB_SHELL_TOP 4
#define TERMOB_SHELL_BOTTOM 22

static void kernel_ui_draw_header(void) {
    uint8_t line_color;

    line_color = terminal_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);

    terminal_initialize(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_draw_hline(0, ' ', line_color);
    terminal_draw_hline(2, '-', line_color);
    terminal_draw_hline(23, '-', line_color);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_writeat(TERMOB_KERNEL_NAME, 1, 0);

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_writeat(TERMOB_KERNEL_VERSION, 63, 0);
}

static void kernel_ui_draw_boot_log(void) {
    terminal_set_region(1, 1);
    terminal_set_cursor(1, 1);

    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write("[OK] ");

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeline("Boot complete");
}

static void kernel_ui_draw_footer(void) {
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_writeat("status: running | input: polling | display: vga 80x25", 1, 24);
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
    kernel_draw_ui();
    keyboard_init();

    for (;;) {
        keyboard_handle();
    }
}

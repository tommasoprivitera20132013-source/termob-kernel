#ifndef TERMINAL_H
#define TERMINAL_H

#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

typedef enum {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW = 14,
    VGA_COLOR_WHITE = 15
} vga_color_t;

void terminal_initialize(vga_color_t fg, vga_color_t bg);
void terminal_clear(void);
void terminal_setcolor(vga_color_t fg, vga_color_t bg);
uint8_t terminal_make_color(vga_color_t fg, vga_color_t bg);

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y);
void terminal_putchar(char c);
void terminal_write(const char* data);
void terminal_writeline(const char* data);

void terminal_writeat(const char* data, size_t x, size_t y);
void terminal_writecenter(const char* data, size_t y);
void terminal_draw_hline(size_t y, char c, uint8_t color);
void terminal_draw_box(size_t x, size_t y, size_t w, size_t h, uint8_t color);
void terminal_fill_rect(size_t x, size_t y, size_t w, size_t h, uint8_t color);

void terminal_set_cursor(size_t x, size_t y);
size_t terminal_get_row(void);
size_t terminal_get_column(void);

void terminal_set_region(size_t top, size_t bottom);
void terminal_reset_region(void);

void terminal_log(const char* tag, const char* message, vga_color_t tag_fg);
void terminal_prompt(void);

#endif

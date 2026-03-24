#include "../include/terminal.h"

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static volatile uint16_t* const terminal_buffer = (volatile uint16_t*)0xB8000;

static size_t region_top = 0;
static size_t region_bottom = VGA_HEIGHT - 1;

static inline uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

static size_t kstrlen(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') len++;
    return len;
}

uint8_t terminal_make_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)(fg | bg << 4);
}

void terminal_setcolor(vga_color_t fg, vga_color_t bg) {
    terminal_color = terminal_make_color(fg, bg);
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    terminal_buffer[y * VGA_WIDTH + x] = vga_entry(c, color);
}

void terminal_fill_rect(size_t x, size_t y, size_t w, size_t h, uint8_t color) {
    size_t yy, xx;
    for (yy = y; yy < y + h && yy < VGA_HEIGHT; yy++) {
        for (xx = x; xx < x + w && xx < VGA_WIDTH; xx++) {
            terminal_putentryat(' ', color, xx, yy);
        }
    }
}

void terminal_clear(void) {
    terminal_fill_rect(0, 0, VGA_WIDTH, VGA_HEIGHT, terminal_color);
    terminal_row = 0;
    terminal_column = 0;
}

void terminal_initialize(vga_color_t fg, vga_color_t bg) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = terminal_make_color(fg, bg);
    region_top = 0;
    region_bottom = VGA_HEIGHT - 1;
    terminal_clear();
}

void terminal_set_cursor(size_t x, size_t y) {
    if (x < VGA_WIDTH) terminal_column = x;
    if (y < VGA_HEIGHT) terminal_row = y;
}

size_t terminal_get_row(void) {
    return terminal_row;
}

size_t terminal_get_column(void) {
    return terminal_column;
}

void terminal_set_region(size_t top, size_t bottom) {
    if (top >= VGA_HEIGHT) top = VGA_HEIGHT - 1;
    if (bottom >= VGA_HEIGHT) bottom = VGA_HEIGHT - 1;
    if (top > bottom) {
        top = 0;
        bottom = VGA_HEIGHT - 1;
    }

    region_top = top;
    region_bottom = bottom;

    if (terminal_row < region_top || terminal_row > region_bottom) {
        terminal_row = region_top;
        terminal_column = 0;
    }
}

void terminal_reset_region(void) {
    region_top = 0;
    region_bottom = VGA_HEIGHT - 1;
}

static void terminal_scroll(void) {
    size_t y, x;

    for (y = region_top + 1; y <= region_bottom; y++) {
        for (x = 0; x < VGA_WIDTH; x++) {
            terminal_buffer[(y - 1) * VGA_WIDTH + x] =
                terminal_buffer[y * VGA_WIDTH + x];
        }
    }

    for (x = 0; x < VGA_WIDTH; x++) {
        terminal_putentryat(' ', terminal_color, x, region_bottom);
    }

    terminal_row = region_bottom;
    terminal_column = 0;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
        if (terminal_row > region_bottom) terminal_scroll();
        return;
    }

    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    terminal_column++;

    if (terminal_column >= VGA_WIDTH) {
        terminal_column = 0;
        terminal_row++;
        if (terminal_row > region_bottom) terminal_scroll();
    }
}

void terminal_write(const char* data) {
    size_t i = 0;
    while (data[i] != '\0') {
        terminal_putchar(data[i]);
        i++;
    }
}

void terminal_writeline(const char* data) {
    terminal_write(data);
    terminal_putchar('\n');
}

void terminal_writeat(const char* data, size_t x, size_t y) {
    size_t i = 0;
    while (data[i] != '\0') {
        if (x + i >= VGA_WIDTH) break;
        terminal_putentryat(data[i], terminal_color, x + i, y);
        i++;
    }
}

void terminal_writecenter(const char* data, size_t y) {
    size_t len = kstrlen(data);
    size_t x = 0;
    if (len < VGA_WIDTH) x = (VGA_WIDTH - len) / 2;
    terminal_writeat(data, x, y);
}

void terminal_draw_hline(size_t y, char c, uint8_t color) {
    size_t x;
    if (y >= VGA_HEIGHT) return;
    for (x = 0; x < VGA_WIDTH; x++) {
        terminal_putentryat(c, color, x, y);
    }
}

void terminal_draw_box(size_t x, size_t y, size_t w, size_t h, uint8_t color) {
    size_t i;
    if (w < 2 || h < 2) return;
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    if (x + w > VGA_WIDTH) w = VGA_WIDTH - x;
    if (y + h > VGA_HEIGHT) h = VGA_HEIGHT - y;

    for (i = 0; i < w; i++) {
        terminal_putentryat('-', color, x + i, y);
        terminal_putentryat('-', color, x + i, y + h - 1);
    }

    for (i = 0; i < h; i++) {
        terminal_putentryat('|', color, x, y + i);
        terminal_putentryat('|', color, x + w - 1, y + i);
    }

    terminal_putentryat('+', color, x, y);
    terminal_putentryat('+', color, x + w - 1, y);
    terminal_putentryat('+', color, x, y + h - 1);
    terminal_putentryat('+', color, x + w - 1, y + h - 1);
}

void terminal_log(const char* tag, const char* message, vga_color_t tag_fg) {
    terminal_setcolor(tag_fg, VGA_COLOR_BLACK);
    terminal_write("[");
    terminal_write(tag);
    terminal_write("] ");

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeline(message);
}

void terminal_prompt(void) {
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write("root");

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write("@");

    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write("termob");

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write(":");

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_write("~");

    terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_write("# ");

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

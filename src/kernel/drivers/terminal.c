#include "../include/terminal.h"

#define TERMINAL_SCROLLBACK_LINES 1024U

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static volatile uint16_t* const terminal_buffer = (volatile uint16_t*)0xB8000;
static const size_t terminal_shell_prompt_width = 12U;

static size_t region_top = 0;
static size_t region_bottom = VGA_HEIGHT - 1;
static uint16_t terminal_scrollback_history[TERMINAL_SCROLLBACK_LINES][VGA_WIDTH];
static size_t terminal_scrollback_head;
static size_t terminal_scrollback_count;
static size_t terminal_scrollback_view_offset;
static uint8_t terminal_scrollback_syncing;

static inline uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

static size_t terminal_region_height(void) {
    return (region_bottom - region_top) + 1U;
}

static size_t terminal_scrollback_logical_to_physical(size_t logical_index) {
    return (terminal_scrollback_head + logical_index) % TERMINAL_SCROLLBACK_LINES;
}

static size_t terminal_scrollback_max_offset(void) {
    size_t region_height;

    region_height = terminal_region_height();
    if (terminal_scrollback_count <= region_height) {
        return 0U;
    }

    return terminal_scrollback_count - region_height;
}

static size_t terminal_scrollback_visible_top_logical(void) {
    size_t region_height;
    size_t max_offset;

    region_height = terminal_region_height();
    if (terminal_scrollback_count <= region_height) {
        return 0U;
    }

    max_offset = terminal_scrollback_max_offset();
    if (terminal_scrollback_view_offset > max_offset) {
        terminal_scrollback_view_offset = max_offset;
    }

    return (terminal_scrollback_count - region_height) - terminal_scrollback_view_offset;
}

static void terminal_scrollback_fill_blank_line(uint16_t* line) {
    size_t x;

    for (x = 0U; x < VGA_WIDTH; x++) {
        line[x] = vga_entry(' ', terminal_color);
    }
}

static uint16_t* terminal_scrollback_line_slot(size_t logical_index) {
    return terminal_scrollback_history[terminal_scrollback_logical_to_physical(logical_index)];
}

static uint16_t* terminal_scrollback_append_blank_line(void) {
    size_t physical_index;

    if (terminal_scrollback_count < TERMINAL_SCROLLBACK_LINES) {
        physical_index = terminal_scrollback_logical_to_physical(terminal_scrollback_count);
        terminal_scrollback_count++;
    } else {
        terminal_scrollback_head = (terminal_scrollback_head + 1U) % TERMINAL_SCROLLBACK_LINES;
        physical_index =
            terminal_scrollback_logical_to_physical(terminal_scrollback_count - 1U);
    }

    terminal_scrollback_fill_blank_line(terminal_scrollback_history[physical_index]);
    return terminal_scrollback_history[physical_index];
}

static void terminal_scrollback_reset_history(void) {
    size_t region_height;
    size_t y;
    size_t x;
    uint16_t* line;

    region_height = terminal_region_height();
    terminal_scrollback_head = 0U;
    terminal_scrollback_count = 0U;
    terminal_scrollback_view_offset = 0U;
    terminal_scrollback_syncing = 0U;

    for (y = 0U; y < region_height; y++) {
        line = terminal_scrollback_append_blank_line();
        for (x = 0U; x < VGA_WIDTH; x++) {
            line[x] = terminal_buffer[(region_top + y) * VGA_WIDTH + x];
        }
    }
}

static void terminal_scrollback_sync_region_from_history(void) {
    size_t region_height;
    size_t top_logical;
    size_t y;
    size_t x;
    uint16_t* line;

    region_height = terminal_region_height();
    top_logical = terminal_scrollback_visible_top_logical();

    terminal_scrollback_syncing = 1U;
    for (y = 0U; y < region_height; y++) {
        if (top_logical + y < terminal_scrollback_count) {
            line = terminal_scrollback_line_slot(top_logical + y);
        } else {
            line = 0;
        }

        for (x = 0U; x < VGA_WIDTH; x++) {
            terminal_buffer[(region_top + y) * VGA_WIDTH + x] =
                line != 0 ? line[x] : vga_entry(' ', terminal_color);
        }
    }
    terminal_scrollback_syncing = 0U;
}

static void terminal_scrollback_sync_view_after_scroll(void) {
    terminal_scrollback_sync_region_from_history();
    terminal_row = region_bottom;
    terminal_column = 0U;
}

static void terminal_scrollback_update_cell(size_t x, size_t y, uint16_t entry) {
    size_t logical_index;
    uint16_t* line;

    if (y < region_top || y > region_bottom || terminal_scrollback_count == 0U) {
        return;
    }

    logical_index = terminal_scrollback_visible_top_logical() + (y - region_top);
    if (logical_index >= terminal_scrollback_count) {
        return;
    }

    line = terminal_scrollback_line_slot(logical_index);
    line[x] = entry;
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
    if (!terminal_scrollback_syncing && y >= region_top && y <= region_bottom &&
        terminal_scrollback_view_offset != 0U) {
        terminal_scrollback_view_offset = 0U;
        terminal_scrollback_sync_region_from_history();
    }
    terminal_buffer[y * VGA_WIDTH + x] = vga_entry(c, color);
    if (!terminal_scrollback_syncing) {
        terminal_scrollback_update_cell(x, y, vga_entry(c, color));
    }
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
    terminal_scrollback_reset_history();
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

    terminal_scrollback_reset_history();
}

void terminal_reset_region(void) {
    region_top = 0;
    region_bottom = VGA_HEIGHT - 1;
    terminal_scrollback_reset_history();
}

static void terminal_scroll(void) {
    if (terminal_scrollback_view_offset != 0U) {
        terminal_scrollback_view_offset = 0U;
    }

    terminal_scrollback_append_blank_line();
    terminal_scrollback_sync_view_after_scroll();
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
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write("[");

    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write("termob");

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write(" ");

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_write("/");

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write("]");

    terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_write("# ");

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

size_t terminal_prompt_width(void) {
    return terminal_shell_prompt_width;
}

int terminal_scrollback_is_active(void) {
    return terminal_scrollback_view_offset != 0U;
}

void terminal_scrollback_follow(void) {
    if (terminal_scrollback_view_offset == 0U) {
        return;
    }

    terminal_scrollback_view_offset = 0U;
    terminal_scrollback_sync_region_from_history();
}

void terminal_scrollback_line_up(void) {
    size_t max_offset;

    max_offset = terminal_scrollback_max_offset();
    if (max_offset == 0U || terminal_scrollback_view_offset >= max_offset) {
        return;
    }

    terminal_scrollback_view_offset++;
    terminal_scrollback_sync_view_after_scroll();
}

void terminal_scrollback_line_down(void) {
    if (terminal_scrollback_view_offset == 0U) {
        return;
    }

    terminal_scrollback_view_offset--;
    terminal_scrollback_sync_view_after_scroll();
}

void terminal_scrollback_page_up(void) {
    size_t step;
    size_t max_offset;

    max_offset = terminal_scrollback_max_offset();
    if (max_offset == 0U || terminal_scrollback_view_offset >= max_offset) {
        return;
    }

    step = terminal_region_height();
    if (step > 1U) {
        step--;
    }

    terminal_scrollback_view_offset += step;
    if (terminal_scrollback_view_offset > max_offset) {
        terminal_scrollback_view_offset = max_offset;
    }

    terminal_scrollback_sync_view_after_scroll();
}

void terminal_scrollback_page_down(void) {
    size_t step;

    if (terminal_scrollback_view_offset == 0U) {
        return;
    }

    step = terminal_region_height();
    if (step > 1U) {
        step--;
    }

    if (terminal_scrollback_view_offset > step) {
        terminal_scrollback_view_offset -= step;
    } else {
        terminal_scrollback_view_offset = 0U;
    }

    terminal_scrollback_sync_view_after_scroll();
}

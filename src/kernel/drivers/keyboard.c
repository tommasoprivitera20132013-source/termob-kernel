#include "../include/block.h"
#include "../include/bootinfo.h"
#include "../include/audio.h"
#include "../include/device.h"
#include "../include/heap.h"
#include "../include/fat.h"
#include "../include/kernel.h"
#include "../include/klog.h"
#include "../include/keyboard.h"
#include "../include/kernel_ui.h"
#include "../include/mouse.h"
#include "../include/panic.h"
#include "../include/paging.h"
#include "../include/pci.h"
#include "../include/pmm.h"
#include "../include/serial.h"
#include "../include/scheduler.h"
#include "../include/sound.h"
#include "../include/terminal.h"
#include "../include/timer.h"
#include "../include/virtio.h"

#define KEYBOARD_MAX_INPUT 256
#define KEYBOARD_SCANCODE_QUEUE_SIZE 128
#define KEYBOARD_HELP_BOX_WIDTH 78U
#define KEYBOARD_HELP_LABEL_WIDTH 16U
#define KEYBOARD_BLOCK_PREVIEW_MAX_SECTORS 4U
#define KEYBOARD_BLOCK_SCAN_MAX_SECTORS 256U
#define KEYBOARD_FAT_CAT_CHUNK_BYTES 256U
#define KEYBOARD_FAT_CAT_MAX_BYTES 4096U
#define KEYBOARD_FAT_CAT_BINARY_THRESHOLD_PERCENT 10U
#define KEYBOARD_FAT_READ_MAX_BYTES 1024U
#define KEYBOARD_FAT_HEXDUMP_LINE_BYTES 16U
#define KEYBOARD_CTRL_SCANCODE 0x1D
#define KEYBOARD_LEFT_SHIFT_SCANCODE 0x2A
#define KEYBOARD_RIGHT_SHIFT_SCANCODE 0x36
#define KEYBOARD_CAPS_LOCK_SCANCODE 0x3A
#define KEYBOARD_EXTENDED_SCANCODE_PREFIX 0xE0
#define KEYBOARD_LEFT_ARROW_SCANCODE 0x4B
#define KEYBOARD_RIGHT_ARROW_SCANCODE 0x4D
#define KEYBOARD_PAGE_UP_SCANCODE 0x49
#define KEYBOARD_PAGE_DOWN_SCANCODE 0x51
#define KEYBOARD_DELETE_SCANCODE 0x53
#define KEYBOARD_C_SCANCODE 0x2E
#define KEYBOARD_L_SCANCODE 0x26
#define KEYBOARD_U_SCANCODE 0x16
#define KEYBOARD_V_SCANCODE 0x2F

static const char keyboard_scancode_table[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0
};

static const char keyboard_shift_scancode_table[128] = {
    0, 27, '!','"','#','$','%','&','/','(',')','=','?','^', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','{','}','\n',
    0,'a','s','d','f','g','h','j','k','l',':','@','~',
    0,'|','z','x','c','v','b','n','m',';',':','?',0,
    '*',0,' ',0
};

static char keyboard_input_buffer[KEYBOARD_MAX_INPUT];
static char keyboard_clipboard_buffer[KEYBOARD_MAX_INPUT];
static int keyboard_buffer_index;
static int keyboard_cursor_index;
static int keyboard_clipboard_length;
static uint8_t keyboard_last_pressed_scancode;
static uint8_t keyboard_control_down;
static uint8_t keyboard_left_shift_down;
static uint8_t keyboard_right_shift_down;
static uint8_t keyboard_caps_lock_on;
static uint8_t keyboard_extended_prefix_pending;
static uint8_t keyboard_scancode_queue[KEYBOARD_SCANCODE_QUEUE_SIZE];
static volatile uint32_t keyboard_scancode_head;
static volatile uint32_t keyboard_scancode_tail;
static volatile uint32_t keyboard_scancode_dropped;
static size_t keyboard_prompt_row;
static size_t keyboard_prompt_column;

typedef struct {
    const char* category;
    const char* summary;
} keyboard_help_entry_t;

typedef struct {
    const char* command;
    const char* usage;
} keyboard_usage_entry_t;

static const keyboard_help_entry_t keyboard_help_entries[] = {
    {"General", "help  clear  echo <text>"},
    {"", "beep  melody"},
    {"System", "dashboard  sysview  sched  info"},
    {"", "status  perf  uname  version"},
    {"", "about  uptime  ticks"},
    {"Memory", "meminfo  heapinfo  heapcheck  heaptest"},
    {"", "pmminfo  paging  memmap"},
    {"Drivers", "lspci  drivers  virtio  vblk  lsblk"},
    {"", "mouse  audio  ac97  ac97tone"},
    {"Storage raw", "blkread0  blkread  blkreadn"},
    {"", "blksig  blkfind  bootchk"},
    {"FAT", "fatinfo  fatcheck  fatentry  fatchain"},
    {"", "fatstat  fatls  fatlsroot  fatlsdir"},
    {"", "ls  fatlookup  fatlspath"},
    {"", "fatcat  fatread"},
    {"Logs", "dmesg  logsize  clearlog"},
    {"Control", "halt  reboot  panic"},
    {"Keys", "Ctrl+C copy  Ctrl+V paste"},
    {"", "Ctrl+U clear  Ctrl+L redraw"},
    {"", "Left/Right move  Del delete"},
    {"", "PgUp/PgDn scroll shell"}
};

static const keyboard_usage_entry_t keyboard_usage_entries[] = {
    {"echo", "echo <text>"},
    {"blkread", "blkread <lba>  or  blkread <device> <lba>"},
    {"blkreadn", "blkreadn <device> <lba> <count>"},
    {"blksig", "blksig <device> <lba>"},
    {"blkfind", "blkfind <device> <lba> <count>"},
    {"bootchk", "bootchk <device> <lba>"},
    {"fatinfo", "fatinfo <device> <lba>"},
    {"fatcheck", "fatcheck <device> <lba>"},
    {"fatentry", "fatentry <device> <boot_lba> <cluster>"},
    {"fatchain", "fatchain <device> <boot_lba> <cluster>"},
    {"fatstat", "fatstat <device> <boot_lba>"},
    {"fatlsroot", "fatlsroot <device> <boot_lba>"},
    {"ls", "ls <device> <boot_lba> [</dir>]"},
    {"fatls", "fatls <device> <boot_lba>"},
    {"fatlsdir", "fatlsdir <device> <boot_lba> <cluster>"},
    {"fatlookup", "fatlookup <device> <boot_lba> </path>"},
    {"fatlspath", "fatlspath <device> <boot_lba> </dir>"},
    {"fatcat", "fatcat <device> <boot_lba> </file>"},
    {"fatread", "fatread <device> <boot_lba> </file> <offset> <count>"}
};

static inline uint8_t keyboard_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void keyboard_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static int keyboard_streq(const char* a, const char* b) {
    int i;

    i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }

    return a[i] == '\0' && b[i] == '\0';
}

static int keyboard_startswith(const char* s, const char* prefix) {
    int i;

    i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) {
            return 0;
        }
        i++;
    }

    return 1;
}

static int keyboard_is_space(char c) {
    return c == ' ' || c == '\t';
}

static const char* keyboard_skip_spaces(const char* text) {
    while (*text != '\0' && keyboard_is_space(*text)) {
        text++;
    }

    return text;
}

static const char* keyboard_skip_spaces_backwards(const char* start, const char* end) {
    while (end > start && keyboard_is_space(*(end - 1))) {
        end--;
    }

    return end;
}

static const char* keyboard_find_token_start_backwards(const char* start, const char* end) {
    while (end > start && !keyboard_is_space(*(end - 1))) {
        end--;
    }

    return end;
}

static int keyboard_hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }

    return -1;
}

static int keyboard_parse_u32_token(const char* text,
                                    const char** next_text,
                                    uint32_t* out_value) {
    uint32_t value;
    uint32_t base;
    int digit_seen;

    if (text == 0 || next_text == 0 || out_value == 0) {
        return 0;
    }

    text = keyboard_skip_spaces(text);
    if (*text == '\0') {
        return 0;
    }

    base = 10U;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16U;
        text += 2;
    }

    value = 0U;
    digit_seen = 0;
    while (*text != '\0' && !keyboard_is_space(*text)) {
        int digit;

        digit = keyboard_hex_value(*text);
        if (digit < 0 || (uint32_t)digit >= base) {
            return 0;
        }

        value = (value * base) + (uint32_t)digit;
        digit_seen = 1;
        text++;
    }

    if (!digit_seen) {
        return 0;
    }

    *next_text = text;
    *out_value = value;
    return 1;
}

static size_t keyboard_strlen(const char* text) {
    size_t length;

    length = 0U;
    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static int keyboard_parse_u32_span(const char* start,
                                   const char* end,
                                   uint32_t* out_value) {
    char token[32];
    const char* next_text;
    size_t length;
    size_t index;

    if (start == 0 || end == 0 || out_value == 0 || end <= start) {
        return 0;
    }

    length = (size_t)(end - start);
    if (length == 0U || length >= sizeof(token)) {
        return 0;
    }

    for (index = 0U; index < length; index++) {
        token[index] = start[index];
    }
    token[length] = '\0';

    if (!keyboard_parse_u32_token(token, &next_text, out_value)) {
        return 0;
    }

    next_text = keyboard_skip_spaces(next_text);
    return *next_text == '\0';
}

static void keyboard_write_spaces(size_t count) {
    while (count > 0U) {
        terminal_putchar(' ');
        count--;
    }
}

static void keyboard_buffer_append_char(char* buffer, size_t* index, size_t capacity, char c) {
    if (*index + 1U >= capacity) {
        return;
    }

    buffer[*index] = c;
    (*index)++;
    buffer[*index] = '\0';
}

static void keyboard_buffer_append_text(char* buffer,
                                        size_t* index,
                                        size_t capacity,
                                        const char* text) {
    size_t position;

    position = 0U;
    while (text[position] != '\0') {
        keyboard_buffer_append_char(buffer, index, capacity, text[position]);
        position++;
    }
}

static void keyboard_buffer_append_u32(char* buffer,
                                       size_t* index,
                                       size_t capacity,
                                       uint32_t value) {
    char digits[10];
    int digit_count;

    if (value == 0U) {
        keyboard_buffer_append_char(buffer, index, capacity, '0');
        return;
    }

    digit_count = 0;
    while (value > 0U && digit_count < 10) {
        digits[digit_count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (digit_count > 0) {
        keyboard_buffer_append_char(buffer, index, capacity, digits[--digit_count]);
    }
}

static void keyboard_buffer_append_hex32(char* buffer,
                                         size_t* index,
                                         size_t capacity,
                                         uint32_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";
    int shift;

    keyboard_buffer_append_text(buffer, index, capacity, "0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        keyboard_buffer_append_char(buffer,
                                    index,
                                    capacity,
                                    hex_digits[(value >> shift) & 0x0FU]);
    }
}

static void keyboard_buffer_append_uptime(char* buffer, size_t* index, size_t capacity) {
    uint32_t frequency;
    uint32_t milliseconds;
    uint32_t seconds;
    uint32_t ticks;

    ticks = timer_get_ticks();
    frequency = timer_get_frequency_hz();
    seconds = ticks / frequency;
    milliseconds = ((ticks % frequency) * 1000U) / frequency;

    keyboard_buffer_append_u32(buffer, index, capacity, seconds);
    keyboard_buffer_append_char(buffer, index, capacity, '.');
    if (milliseconds < 100U) {
        keyboard_buffer_append_char(buffer, index, capacity, '0');
    }
    if (milliseconds < 10U) {
        keyboard_buffer_append_char(buffer, index, capacity, '0');
    }
    keyboard_buffer_append_u32(buffer, index, capacity, milliseconds);
    keyboard_buffer_append_text(buffer, index, capacity, " s");
}

static void keyboard_panel_border(void) {
    size_t inner_width;

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_putchar('+');

    inner_width = KEYBOARD_HELP_BOX_WIDTH - 2U;
    while (inner_width > 0U) {
        terminal_putchar('-');
        inner_width--;
    }

    terminal_writeline("+");
}

static void keyboard_panel_title_row(const char* title) {
    size_t content_length;
    size_t inner_width;
    size_t padding_left;
    size_t padding_right;

    inner_width = KEYBOARD_HELP_BOX_WIDTH - 2U;
    content_length = keyboard_strlen(title);
    if (content_length > inner_width) {
        content_length = inner_width;
    }

    padding_left = (inner_width - content_length) / 2U;
    padding_right = inner_width - content_length - padding_left;

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_putchar('|');
    keyboard_write_spaces(padding_left);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_write(title);

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    keyboard_write_spaces(padding_right);
    terminal_writeline("|");
}

static size_t keyboard_write_limited_text(const char* text, size_t max_chars) {
    size_t written;

    written = 0U;
    while (text[written] != '\0' && written < max_chars) {
        terminal_putchar(text[written]);
        written++;
    }

    return written;
}

static void keyboard_panel_row(const char* label, const char* text) {
    size_t label_length;
    size_t text_width;
    size_t used_width;
    size_t padding;

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write("| ");

    if (label != 0 && label[0] != '\0') {
        terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        terminal_write(label);
        label_length = keyboard_strlen(label);
    } else {
        label_length = 0U;
    }

    if (label_length < KEYBOARD_HELP_LABEL_WIDTH) {
        terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        keyboard_write_spaces(KEYBOARD_HELP_LABEL_WIDTH - label_length);
    }

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write(" | ");

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    text_width = KEYBOARD_HELP_BOX_WIDTH - (2U + KEYBOARD_HELP_LABEL_WIDTH + 3U + 1U);
    used_width = keyboard_write_limited_text(text, text_width);

    used_width += 2U + KEYBOARD_HELP_LABEL_WIDTH + 3U;
    if (used_width < KEYBOARD_HELP_BOX_WIDTH - 1U) {
        padding = (KEYBOARD_HELP_BOX_WIDTH - 1U) - used_width;
        terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        keyboard_write_spaces(padding);
    }

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_writeline("|");
}

static void keyboard_panel_blank_row(void) {
    keyboard_panel_row("", "");
}

static void keyboard_shell_capture_prompt_anchor(void) {
    keyboard_prompt_row = terminal_get_row();
    keyboard_prompt_column = terminal_get_column();
}

static size_t keyboard_shell_input_capacity(void) {
    if (keyboard_prompt_column >= VGA_WIDTH - 1U) {
        return 0U;
    }

    return (VGA_WIDTH - keyboard_prompt_column) - 1U;
}

static void keyboard_shell_redraw_input(void) {
    size_t capacity;

    capacity = keyboard_shell_input_capacity();
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_fill_rect(keyboard_prompt_column,
                       keyboard_prompt_row,
                       capacity,
                       1U,
                       terminal_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writeat(keyboard_input_buffer, keyboard_prompt_column, keyboard_prompt_row);
    terminal_set_cursor(keyboard_prompt_column + (size_t)keyboard_cursor_index, keyboard_prompt_row);
}

static void keyboard_shell_prompt_fresh(void) {
    terminal_prompt();
    keyboard_shell_capture_prompt_anchor();
}

static void keyboard_shell_print_usage(const char* usage) {
    terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_write("usage");
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write(" : ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeline(usage);
}

static void keyboard_shell_usage_and_prompt(const char* usage) {
    keyboard_shell_print_usage(usage);
    keyboard_shell_prompt_fresh();
}

static void keyboard_shell_print_error(const char* message) {
    terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_write("error");
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write(" : ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeline(message);
}

static void keyboard_shell_print_unknown_command(const char* command) {
    terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_write("error");
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write(" : ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write("unknown command");
    if (command != 0 && command[0] != '\0') {
        terminal_write(" '");
        terminal_write(command);
        terminal_write("'");
    }
    terminal_putchar('\n');

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write("hint");
    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_write("  : ");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeline("type help to list commands");
}

static int keyboard_shift_is_active(void) {
    return keyboard_left_shift_down != 0U || keyboard_right_shift_down != 0U;
}

static int keyboard_is_alpha(char c) {
    return c >= 'a' && c <= 'z';
}

static char keyboard_uppercase_char(char c) {
    if (!keyboard_is_alpha(c)) {
        return c;
    }

    return (char)(c - ('a' - 'A'));
}

static char keyboard_translate_scancode(uint8_t scancode) {
    char c;
    int shift_active;

    if (scancode >= 128U) {
        return 0;
    }

    shift_active = keyboard_shift_is_active();
    c = shift_active ? keyboard_shift_scancode_table[scancode]
                     : keyboard_scancode_table[scancode];
    if (c == 0) {
        return 0;
    }

    if (keyboard_is_alpha(c)) {
        if ((keyboard_caps_lock_on != 0U) ^ shift_active) {
            return keyboard_uppercase_char(c);
        }

        return c;
    }

    return c;
}

static const char* keyboard_memory_type_name(uint32_t type) {
    switch (type) {
        case TERMOB_MEMORY_TYPE_AVAILABLE:
            return "available";
        case TERMOB_MEMORY_TYPE_RESERVED:
            return "reserved";
        case TERMOB_MEMORY_TYPE_ACPI_RECLAIMABLE:
            return "acpi";
        case TERMOB_MEMORY_TYPE_NVS:
            return "nvs";
        case TERMOB_MEMORY_TYPE_BADRAM:
            return "badram";
        default:
            return "unknown";
    }
}

static void keyboard_copy_input_to_clipboard(void) {
    int i;

    keyboard_clipboard_length = keyboard_buffer_index;
    for (i = 0; i < keyboard_buffer_index; i++) {
        keyboard_clipboard_buffer[i] = keyboard_input_buffer[i];
    }
    keyboard_clipboard_buffer[keyboard_clipboard_length] = '\0';
}

static void keyboard_reset_input_buffer(void) {
    keyboard_buffer_index = 0;
    keyboard_cursor_index = 0;
    keyboard_input_buffer[0] = '\0';
}

static void keyboard_clear_input_line(void) {
    keyboard_reset_input_buffer();
    keyboard_shell_redraw_input();
}

static void keyboard_move_cursor_left(void) {
    if (keyboard_cursor_index > 0) {
        keyboard_cursor_index--;
        keyboard_shell_redraw_input();
    }
}

static void keyboard_move_cursor_right(void) {
    if (keyboard_cursor_index < keyboard_buffer_index) {
        keyboard_cursor_index++;
        keyboard_shell_redraw_input();
    }
}

static void keyboard_delete_character_before_cursor(void) {
    int index;

    if (keyboard_cursor_index <= 0 || keyboard_buffer_index <= 0) {
        return;
    }

    for (index = keyboard_cursor_index - 1; index < keyboard_buffer_index; index++) {
        keyboard_input_buffer[index] = keyboard_input_buffer[index + 1];
    }

    keyboard_cursor_index--;
    keyboard_buffer_index--;
    keyboard_shell_redraw_input();
}

static void keyboard_delete_character_at_cursor(void) {
    int index;

    if (keyboard_cursor_index >= keyboard_buffer_index || keyboard_buffer_index <= 0) {
        return;
    }

    for (index = keyboard_cursor_index; index < keyboard_buffer_index; index++) {
        keyboard_input_buffer[index] = keyboard_input_buffer[index + 1];
    }

    keyboard_buffer_index--;
    keyboard_shell_redraw_input();
}

static void keyboard_insert_character(char c) {
    int index;

    if (keyboard_buffer_index >= KEYBOARD_MAX_INPUT - 1) {
        return;
    }

    if ((size_t)keyboard_buffer_index >= keyboard_shell_input_capacity()) {
        return;
    }

    for (index = keyboard_buffer_index; index > keyboard_cursor_index; index--) {
        keyboard_input_buffer[index] = keyboard_input_buffer[index - 1];
    }

    keyboard_input_buffer[keyboard_cursor_index] = c;
    keyboard_buffer_index++;
    keyboard_cursor_index++;
    keyboard_input_buffer[keyboard_buffer_index] = '\0';
    keyboard_shell_redraw_input();
}

static void keyboard_paste_clipboard(void) {
    int i;

    for (i = 0; i < keyboard_clipboard_length; i++) {
        keyboard_insert_character(keyboard_clipboard_buffer[i]);
    }
}

static void keyboard_log_event(const char* message) {
    klog_writeline(message);
    serial_writeline(message);
}

static void keyboard_write_u32(uint32_t value) {
    char digits[10];
    int i;

    if (value == 0) {
        terminal_putchar('0');
        return;
    }

    i = 0;
    while (value > 0 && i < 10) {
        digits[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        terminal_putchar(digits[--i]);
    }
}

static void keyboard_write_hex32(uint32_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";
    int shift;

    terminal_write("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        terminal_putchar(hex_digits[(value >> shift) & 0x0F]);
    }
}

static void keyboard_write_hex8(uint8_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";

    terminal_putchar(hex_digits[(value >> 4) & 0x0FU]);
    terminal_putchar(hex_digits[value & 0x0FU]);
}

static uint16_t keyboard_read_le16(const uint8_t* buffer, size_t offset) {
    return (uint16_t)((uint16_t)buffer[offset] |
                      ((uint16_t)buffer[offset + 1U] << 8));
}

static uint32_t keyboard_read_le32(const uint8_t* buffer, size_t offset) {
    return (uint32_t)buffer[offset] |
           ((uint32_t)buffer[offset + 1U] << 8) |
           ((uint32_t)buffer[offset + 2U] << 16) |
           ((uint32_t)buffer[offset + 3U] << 24);
}

static int keyboard_is_power_of_two_u32(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

typedef enum keyboard_fat_type {
    KEYBOARD_FAT_NONE = 0,
    KEYBOARD_FAT_12,
    KEYBOARD_FAT_16,
    KEYBOARD_FAT_32
} keyboard_fat_type_t;

typedef struct keyboard_fat_info {
    keyboard_fat_type_t type;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t root_entry_count;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t root_dir_sectors;
    uint32_t fat_start_sector;
    uint32_t root_dir_start_sector;
    uint32_t data_start_sector;
    uint32_t data_sectors;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t fsinfo_sector;
    uint32_t backup_boot_sector;
    int boot_magic_present;
} keyboard_fat_info_t;

static int keyboard_bytes_equal(const uint8_t* buffer,
                                size_t offset,
                                const char* text,
                                size_t text_length);

static const char* keyboard_fat_type_name(keyboard_fat_type_t type) {
    switch (type) {
        case KEYBOARD_FAT_12:
            return "FAT12";
        case KEYBOARD_FAT_16:
            return "FAT16";
        case KEYBOARD_FAT_32:
            return "FAT32";
        default:
            return "unknown";
    }
}

static int keyboard_parse_fat_info(const uint8_t* sector, keyboard_fat_info_t* info) {
    uint32_t sectors_per_fat16;
    uint32_t sectors_per_fat32;

    if (sector == 0 || info == 0) {
        return 0;
    }

    info->type = KEYBOARD_FAT_NONE;
    info->bytes_per_sector = (uint32_t)keyboard_read_le16(sector, 11U);
    info->sectors_per_cluster = (uint32_t)sector[13];
    info->reserved_sectors = (uint32_t)keyboard_read_le16(sector, 14U);
    info->fat_count = (uint32_t)sector[16];
    info->root_entry_count = (uint32_t)keyboard_read_le16(sector, 17U);
    info->total_sectors = (uint32_t)keyboard_read_le16(sector, 19U);
    if (info->total_sectors == 0U) {
        info->total_sectors = keyboard_read_le32(sector, 32U);
    }

    sectors_per_fat16 = (uint32_t)keyboard_read_le16(sector, 22U);
    sectors_per_fat32 = keyboard_read_le32(sector, 36U);
    info->sectors_per_fat = sectors_per_fat16 != 0U ? sectors_per_fat16 : sectors_per_fat32;
    info->root_cluster = keyboard_read_le32(sector, 44U);
    info->fsinfo_sector = (uint32_t)keyboard_read_le16(sector, 48U);
    info->backup_boot_sector = (uint32_t)keyboard_read_le16(sector, 50U);
    info->boot_magic_present = sector[510] == 0x55U && sector[511] == 0xAAU;

    if (info->bytes_per_sector == 0U) {
        return 0;
    }

    info->root_dir_sectors =
        ((info->root_entry_count * 32U) + (info->bytes_per_sector - 1U)) / info->bytes_per_sector;
    info->fat_start_sector = info->reserved_sectors;
    info->root_dir_start_sector = info->fat_start_sector + (info->fat_count * info->sectors_per_fat);
    info->data_start_sector = info->root_dir_start_sector + info->root_dir_sectors;
    info->data_sectors = info->total_sectors > info->data_start_sector
                           ? (info->total_sectors - info->data_start_sector)
                           : 0U;
    info->cluster_count = info->sectors_per_cluster != 0U
                            ? (info->data_sectors / info->sectors_per_cluster)
                            : 0U;

    if (keyboard_bytes_equal(sector, 82U, "FAT32   ", 8U) ||
        (info->root_entry_count == 0U && sectors_per_fat16 == 0U && sectors_per_fat32 != 0U)) {
        info->type = KEYBOARD_FAT_32;
    } else if (keyboard_bytes_equal(sector, 54U, "FAT12   ", 8U)) {
        info->type = KEYBOARD_FAT_12;
    } else if (keyboard_bytes_equal(sector, 54U, "FAT16   ", 8U)) {
        info->type = KEYBOARD_FAT_16;
    } else if (info->cluster_count != 0U) {
        if (info->cluster_count < 4085U) {
            info->type = KEYBOARD_FAT_12;
        } else if (info->cluster_count < 65525U) {
            info->type = KEYBOARD_FAT_16;
        } else {
            info->type = KEYBOARD_FAT_32;
        }
    }

    return info->type != KEYBOARD_FAT_NONE;
}

static int keyboard_read_block_sector_512(uint32_t device_index,
                                          uint32_t lba,
                                          const char* command_name,
                                          uint8_t* sector_out) {
    termob_block_device_t block_device;

    if (!block_device_at((size_t)device_index, &block_device)) {
        terminal_writeline("Block device not found");
        return 0;
    }

    if (block_device.sector_size_bytes != 512U) {
        terminal_write("Only 512-byte sectors are supported by ");
        terminal_writeline(command_name);
        return 0;
    }

    if (block_device.sector_count != 0U && lba >= block_device.sector_count) {
        terminal_writeline("Requested LBA is outside the device range");
        return 0;
    }

    if (!block_read_device((size_t)device_index, lba, 1U, sector_out)) {
        terminal_writeline("Block read failed");
        return 0;
    }

    return 1;
}

static void keyboard_dump_sector_preview(const uint8_t* buffer, size_t bytes) {
    size_t offset;
    size_t index;

    for (offset = 0U; offset < bytes; offset += 16U) {
        keyboard_write_hex32((uint32_t)offset);
        terminal_write(": ");

        for (index = 0U; index < 16U; index++) {
            if (offset + index < bytes) {
                keyboard_write_hex8(buffer[offset + index]);
            } else {
                terminal_write("  ");
            }
            terminal_putchar(' ');
        }

        terminal_write(" |");
        for (index = 0U; index < 16U && offset + index < bytes; index++) {
            uint8_t c;

            c = buffer[offset + index];
            if (c >= 32U && c <= 126U) {
                terminal_putchar((char)c);
            } else {
                terminal_putchar('.');
            }
        }
        terminal_writeline("|");
    }
}

static int keyboard_bytes_equal(const uint8_t* buffer,
                                size_t offset,
                                const char* text,
                                size_t text_length) {
    size_t index;

    for (index = 0U; index < text_length; index++) {
        if (buffer[offset + index] != (uint8_t)text[index]) {
            return 0;
        }
    }

    return 1;
}

static const char* keyboard_detect_sector_signature(const uint8_t* sector) {
    if (sector[0] == 0x7FU && sector[1] == 'E' && sector[2] == 'L' && sector[3] == 'F') {
        return "ELF executable";
    }

    if (keyboard_bytes_equal(sector, 54U, "FAT12   ", 8U)) {
        return "FAT12 boot sector";
    }

    if (keyboard_bytes_equal(sector, 54U, "FAT16   ", 8U)) {
        return "FAT16 boot sector";
    }

    if (keyboard_bytes_equal(sector, 82U, "FAT32   ", 8U)) {
        return "FAT32 boot sector";
    }

    if (keyboard_bytes_equal(sector, 3U, "NTFS    ", 8U)) {
        return "NTFS boot sector";
    }

    return 0;
}

static void keyboard_report_sector_signature(const uint8_t* sector) {
    const char* signature;

    terminal_writeline("Sector signatures:");

    if (sector[510] == 0x55U && sector[511] == 0xAAU) {
        terminal_writeline("  Boot signature  : 0x55AA present");
    } else {
        terminal_writeline("  Boot signature  : not present");
    }

    signature = keyboard_detect_sector_signature(sector);
    if (signature != 0) {
        terminal_write("  Filesystem      : ");
        terminal_writeline(signature);
        return;
    }

    terminal_writeline("  Filesystem      : unknown");
}

static int keyboard_block_read_preview(uint32_t device_index,
                                       uint32_t lba,
                                       uint32_t sector_count) {
    termob_block_device_t block_device;
    uint8_t sector_buffer[512U * KEYBOARD_BLOCK_PREVIEW_MAX_SECTORS];
    uint32_t preview_bytes;

    if (!block_device_at((size_t)device_index, &block_device)) {
        terminal_writeline("Block device not found");
        return 0;
    }

    if (block_device.sector_size_bytes != 512U) {
        terminal_writeline("Only 512-byte sectors are supported by blkread");
        return 0;
    }

    if (sector_count == 0U || sector_count > KEYBOARD_BLOCK_PREVIEW_MAX_SECTORS) {
        terminal_writeline("Sector count must be between 1 and 4");
        return 0;
    }

    if (block_device.sector_count != 0U &&
        (lba >= block_device.sector_count ||
         sector_count > (block_device.sector_count - lba))) {
        terminal_writeline("Requested LBA is outside the device range");
        return 0;
    }

    terminal_write("Reading block device #");
    keyboard_write_u32(device_index);
    terminal_write("  lba ");
    keyboard_write_u32(lba);
    terminal_write("  count ");
    keyboard_write_u32(sector_count);
    terminal_writeline(" ...");

    if (!block_read_device((size_t)device_index, lba, sector_count, sector_buffer)) {
        keyboard_log_event("TERMOB: blkread failed");
        terminal_writeline("Block read failed");
        return 0;
    }

    keyboard_log_event("TERMOB: blkread ok");
    preview_bytes = sector_count * block_device.sector_size_bytes;
    if (preview_bytes > 128U) {
        preview_bytes = 128U;
    }
    keyboard_dump_sector_preview(sector_buffer, preview_bytes);
    return 1;
}

static void keyboard_block_signature_preview(uint32_t device_index, uint32_t lba) {
    termob_block_device_t block_device;
    uint8_t sector[512];

    if (!block_device_at((size_t)device_index, &block_device)) {
        terminal_writeline("Block device not found");
        return;
    }

    if (block_device.sector_size_bytes != 512U) {
        terminal_writeline("Only 512-byte sectors are supported by blksig");
        return;
    }

    if (block_device.sector_count != 0U && lba >= block_device.sector_count) {
        terminal_writeline("Requested LBA is outside the device range");
        return;
    }

    terminal_write("Inspecting signatures on block device #");
    keyboard_write_u32(device_index);
    terminal_write("  lba ");
    keyboard_write_u32(lba);
    terminal_writeline(" ...");

    if (!block_read_device((size_t)device_index, lba, 1U, sector)) {
        keyboard_log_event("TERMOB: blksig failed");
        terminal_writeline("Block read failed");
        return;
    }

    keyboard_log_event("TERMOB: blksig ok");
    keyboard_report_sector_signature(sector);
}

static void keyboard_block_find_signatures(uint32_t device_index,
                                           uint32_t start_lba,
                                           uint32_t sector_count) {
    termob_block_device_t block_device;
    uint8_t sector[512];
    uint32_t lba;
    uint32_t hits;

    if (!block_device_at((size_t)device_index, &block_device)) {
        terminal_writeline("Block device not found");
        return;
    }

    if (block_device.sector_size_bytes != 512U) {
        terminal_writeline("Only 512-byte sectors are supported by blkfind");
        return;
    }

    if (sector_count == 0U || sector_count > KEYBOARD_BLOCK_SCAN_MAX_SECTORS) {
        terminal_writeline("Scan count must be between 1 and 256");
        return;
    }

    if (block_device.sector_count != 0U &&
        (start_lba >= block_device.sector_count ||
         sector_count > (block_device.sector_count - start_lba))) {
        terminal_writeline("Requested LBA range is outside the device range");
        return;
    }

    terminal_write("Scanning block device #");
    keyboard_write_u32(device_index);
    terminal_write("  lba ");
    keyboard_write_u32(start_lba);
    terminal_write("..");
    keyboard_write_u32(start_lba + sector_count - 1U);
    terminal_writeline(" ...");

    hits = 0U;
    for (lba = start_lba; lba < start_lba + sector_count; lba++) {
        const char* signature;
        int has_boot_signature;

        if (!block_read_device((size_t)device_index, lba, 1U, sector)) {
            terminal_write("Read failed at lba ");
            keyboard_write_u32(lba);
            terminal_putchar('\n');
            keyboard_log_event("TERMOB: blkfind failed");
            return;
        }

        signature = keyboard_detect_sector_signature(sector);
        has_boot_signature = sector[510] == 0x55U && sector[511] == 0xAAU;
        if (signature == 0 && !has_boot_signature) {
            continue;
        }

        terminal_write("  LBA ");
        keyboard_write_u32(lba);
        terminal_write("  : ");
        if (signature != 0) {
            terminal_write(signature);
        } else {
            terminal_write("boot signature only");
        }

        if (has_boot_signature && signature != 0) {
            terminal_write(" + 0x55AA");
        }
        terminal_putchar('\n');
        hits++;
    }

    terminal_write("Matches found : ");
    keyboard_write_u32(hits);
    terminal_putchar('\n');
    keyboard_log_event("TERMOB: blkfind done");
}

static void keyboard_boot_sector_check(uint32_t device_index, uint32_t lba) {
    const char* signature;
    termob_block_device_t block_device;
    uint8_t sector[512];
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    int plausible;

    if (!block_device_at((size_t)device_index, &block_device)) {
        terminal_writeline("Block device not found");
        return;
    }

    if (block_device.sector_size_bytes != 512U) {
        terminal_writeline("Only 512-byte sectors are supported by bootchk");
        return;
    }

    if (block_device.sector_count != 0U && lba >= block_device.sector_count) {
        terminal_writeline("Requested LBA is outside the device range");
        return;
    }

    terminal_write("Checking boot sector on block device #");
    keyboard_write_u32(device_index);
    terminal_write("  lba ");
    keyboard_write_u32(lba);
    terminal_writeline(" ...");

    if (!block_read_device((size_t)device_index, lba, 1U, sector)) {
        keyboard_log_event("TERMOB: bootchk failed");
        terminal_writeline("Block read failed");
        return;
    }

    signature = keyboard_detect_sector_signature(sector);
    bytes_per_sector = (uint32_t)keyboard_read_le16(sector, 11U);
    sectors_per_cluster = (uint32_t)sector[13];
    reserved_sectors = (uint32_t)keyboard_read_le16(sector, 14U);
    fat_count = (uint32_t)sector[16];
    sectors_per_fat = (uint32_t)keyboard_read_le16(sector, 22U);
    if (signature != 0 && keyboard_bytes_equal(sector, 82U, "FAT32   ", 8U)) {
        sectors_per_fat = keyboard_read_le32(sector, 36U);
    }

    total_sectors = (uint32_t)keyboard_read_le16(sector, 19U);
    if (total_sectors == 0U) {
        total_sectors = keyboard_read_le32(sector, 32U);
    }

    terminal_writeline("Boot sector report:");
    terminal_write("  Signature    : ");
    terminal_writeline(signature != 0 ? signature : "unknown");
    terminal_write("  Boot magic   : ");
    terminal_writeline(sector[510] == 0x55U && sector[511] == 0xAAU ? "0x55AA present" : "missing");
    terminal_write("  Bytes/sector : ");
    keyboard_write_u32(bytes_per_sector);
    terminal_putchar('\n');
    terminal_write("  Cluster size : ");
    keyboard_write_u32(sectors_per_cluster);
    terminal_writeline(" sectors");
    terminal_write("  Reserved     : ");
    keyboard_write_u32(reserved_sectors);
    terminal_writeline(" sectors");
    terminal_write("  FATs         : ");
    keyboard_write_u32(fat_count);
    terminal_putchar('\n');
    terminal_write("  Sectors/FAT  : ");
    keyboard_write_u32(sectors_per_fat);
    terminal_putchar('\n');
    terminal_write("  Total sectors: ");
    keyboard_write_u32(total_sectors);
    terminal_putchar('\n');

    plausible = 1;
    if (!(bytes_per_sector == 512U ||
          bytes_per_sector == 1024U ||
          bytes_per_sector == 2048U ||
          bytes_per_sector == 4096U)) {
        plausible = 0;
    }
    if (!keyboard_is_power_of_two_u32(sectors_per_cluster) || sectors_per_cluster > 128U) {
        plausible = 0;
    }
    if (reserved_sectors == 0U || fat_count == 0U) {
        plausible = 0;
    }
    if ((signature != 0 &&
         (keyboard_bytes_equal(sector, 54U, "FAT12   ", 8U) ||
          keyboard_bytes_equal(sector, 54U, "FAT16   ", 8U) ||
          keyboard_bytes_equal(sector, 82U, "FAT32   ", 8U))) &&
        sectors_per_fat == 0U) {
        plausible = 0;
    }
    if (!(sector[510] == 0x55U && sector[511] == 0xAAU)) {
        plausible = 0;
    }

    terminal_write("  Verdict      : ");
    terminal_writeline(plausible ? "plausible boot sector" : "suspicious / corrupted");
    keyboard_log_event("TERMOB: bootchk ok");
}

static void keyboard_fat_info_dump(uint32_t device_index, uint32_t lba) {
    keyboard_fat_info_t info;
    uint8_t sector[512];

    terminal_write("Reading FAT info on block device #");
    keyboard_write_u32(device_index);
    terminal_write("  lba ");
    keyboard_write_u32(lba);
    terminal_writeline(" ...");

    if (!keyboard_read_block_sector_512(device_index, lba, "fatinfo", sector)) {
        keyboard_log_event("TERMOB: fatinfo failed");
        return;
    }

    if (!keyboard_parse_fat_info(sector, &info)) {
        terminal_writeline("No FAT boot sector detected");
        keyboard_log_event("TERMOB: fatinfo not-fat");
        return;
    }

    terminal_writeline("FAT boot sector:");
    terminal_write("  Type         : ");
    terminal_writeline(keyboard_fat_type_name(info.type));
    terminal_write("  Boot magic   : ");
    terminal_writeline(info.boot_magic_present ? "0x55AA present" : "missing");
    terminal_write("  Bytes/sector : ");
    keyboard_write_u32(info.bytes_per_sector);
    terminal_putchar('\n');
    terminal_write("  Cluster size : ");
    keyboard_write_u32(info.sectors_per_cluster);
    terminal_writeline(" sectors");
    terminal_write("  Reserved     : ");
    keyboard_write_u32(info.reserved_sectors);
    terminal_writeline(" sectors");
    terminal_write("  FAT count    : ");
    keyboard_write_u32(info.fat_count);
    terminal_putchar('\n');
    terminal_write("  Sectors/FAT  : ");
    keyboard_write_u32(info.sectors_per_fat);
    terminal_putchar('\n');
    terminal_write("  Total sectors: ");
    keyboard_write_u32(info.total_sectors);
    terminal_putchar('\n');
    terminal_write("  FAT start    : ");
    keyboard_write_u32(info.fat_start_sector);
    terminal_putchar('\n');
    terminal_write("  Root dir     : ");
    if (info.type == KEYBOARD_FAT_32) {
        terminal_write("cluster ");
        keyboard_write_u32(info.root_cluster);
        terminal_write("  sector ");
        keyboard_write_u32(info.data_start_sector +
                           ((info.root_cluster >= 2U ? info.root_cluster : 2U) - 2U) *
                               info.sectors_per_cluster);
    } else {
        terminal_write("sector ");
        keyboard_write_u32(info.root_dir_start_sector);
        terminal_write("  span ");
        keyboard_write_u32(info.root_dir_sectors);
    }
    terminal_putchar('\n');
    terminal_write("  Data start   : ");
    keyboard_write_u32(info.data_start_sector);
    terminal_putchar('\n');
    terminal_write("  Clusters     : ");
    keyboard_write_u32(info.cluster_count);
    terminal_putchar('\n');
    if (info.type == KEYBOARD_FAT_32) {
        terminal_write("  FSInfo       : ");
        keyboard_write_u32(info.fsinfo_sector);
        terminal_write("  backup ");
        keyboard_write_u32(info.backup_boot_sector);
        terminal_putchar('\n');
    } else {
        terminal_write("  Root entries : ");
        keyboard_write_u32(info.root_entry_count);
        terminal_putchar('\n');
    }

    keyboard_log_event("TERMOB: fatinfo ok");
}

static void keyboard_fat_check(uint32_t device_index, uint32_t lba) {
    keyboard_fat_info_t info;
    uint8_t sector[512];
    uint32_t issues;

    terminal_write("Checking FAT layout on block device #");
    keyboard_write_u32(device_index);
    terminal_write("  lba ");
    keyboard_write_u32(lba);
    terminal_writeline(" ...");

    if (!keyboard_read_block_sector_512(device_index, lba, "fatcheck", sector)) {
        keyboard_log_event("TERMOB: fatcheck failed");
        return;
    }

    if (!keyboard_parse_fat_info(sector, &info)) {
        terminal_writeline("No FAT boot sector detected");
        keyboard_log_event("TERMOB: fatcheck not-fat");
        return;
    }

    terminal_writeline("FAT consistency report:");
    issues = 0U;

    if (!info.boot_magic_present) {
        terminal_writeline("  Issue        : missing boot signature 0x55AA");
        issues++;
    }
    if (!(info.bytes_per_sector == 512U ||
          info.bytes_per_sector == 1024U ||
          info.bytes_per_sector == 2048U ||
          info.bytes_per_sector == 4096U)) {
        terminal_writeline("  Issue        : invalid bytes-per-sector");
        issues++;
    }
    if (!keyboard_is_power_of_two_u32(info.sectors_per_cluster) ||
        info.sectors_per_cluster > 128U) {
        terminal_writeline("  Issue        : invalid sectors-per-cluster");
        issues++;
    }
    if (info.reserved_sectors == 0U) {
        terminal_writeline("  Issue        : reserved sectors is zero");
        issues++;
    }
    if (info.fat_count == 0U) {
        terminal_writeline("  Issue        : FAT count is zero");
        issues++;
    }
    if (info.sectors_per_fat == 0U) {
        terminal_writeline("  Issue        : sectors-per-FAT is zero");
        issues++;
    }
    if (info.total_sectors == 0U) {
        terminal_writeline("  Issue        : total sectors is zero");
        issues++;
    }
    if (info.data_start_sector >= info.total_sectors) {
        terminal_writeline("  Issue        : data region starts beyond disk size");
        issues++;
    }
    if (info.cluster_count == 0U) {
        terminal_writeline("  Issue        : cluster count is zero");
        issues++;
    }

    if (info.type == KEYBOARD_FAT_32) {
        if (info.root_entry_count != 0U) {
            terminal_writeline("  Issue        : FAT32 root entry count should be zero");
            issues++;
        }
        if (info.root_cluster < 2U) {
            terminal_writeline("  Issue        : FAT32 root cluster is invalid");
            issues++;
        }
    } else {
        if (info.root_entry_count == 0U) {
            terminal_writeline("  Issue        : FAT12/16 root entry count is zero");
            issues++;
        }
    }

    terminal_write("  Verdict      : ");
    if (issues == 0U) {
        terminal_writeline("FAT layout looks consistent");
    } else {
        terminal_write("suspicious / corrupted (issues ");
        keyboard_write_u32(issues);
        terminal_writeline(")");
    }

    keyboard_log_event("TERMOB: fatcheck ok");
}

static int keyboard_fat_entry_read(uint32_t device_index,
                                   uint32_t boot_lba,
                                   uint32_t cluster,
                                   keyboard_fat_info_t* info_out,
                                   uint32_t* entry_value_out) {
    keyboard_fat_info_t info;
    uint8_t boot_sector[512];
    uint8_t entry_bytes[1024];
    uint32_t fat_offset_bytes;
    uint32_t fat_sector_delta;
    uint32_t sector_offset;
    uint32_t sectors_to_read;
    uint32_t entry_value;

    if (info_out == 0 || entry_value_out == 0) {
        return 0;
    }

    if (!keyboard_read_block_sector_512(device_index, boot_lba, "fatentry", boot_sector)) {
        return 0;
    }

    if (!keyboard_parse_fat_info(boot_sector, &info)) {
        return 0;
    }

    switch (info.type) {
        case KEYBOARD_FAT_12:
            fat_offset_bytes = cluster + (cluster / 2U);
            break;
        case KEYBOARD_FAT_16:
            fat_offset_bytes = cluster * 2U;
            break;
        case KEYBOARD_FAT_32:
            fat_offset_bytes = cluster * 4U;
            break;
        default:
            return 0;
    }

    fat_sector_delta = fat_offset_bytes / info.bytes_per_sector;
    sector_offset = fat_offset_bytes % info.bytes_per_sector;
    sectors_to_read = 1U;

    if ((info.type == KEYBOARD_FAT_12 && sector_offset >= 511U) ||
        (info.type == KEYBOARD_FAT_16 && sector_offset >= 511U) ||
        (info.type == KEYBOARD_FAT_32 && sector_offset >= 509U)) {
        sectors_to_read = 2U;
    }

    if (!block_read_device((size_t)device_index,
                           boot_lba + info.fat_start_sector + fat_sector_delta,
                           sectors_to_read,
                           entry_bytes)) {
        return 0;
    }

    switch (info.type) {
        case KEYBOARD_FAT_12: {
            uint16_t raw;

            raw = keyboard_read_le16(entry_bytes, sector_offset);
            if ((cluster & 1U) == 0U) {
                entry_value = (uint32_t)(raw & 0x0FFFU);
            } else {
                entry_value = (uint32_t)((raw >> 4) & 0x0FFFU);
            }
            break;
        }
        case KEYBOARD_FAT_16:
            entry_value = (uint32_t)keyboard_read_le16(entry_bytes, sector_offset);
            break;
        case KEYBOARD_FAT_32:
            entry_value = keyboard_read_le32(entry_bytes, sector_offset) & 0x0FFFFFFFU;
            break;
        default:
            return 0;
    }

    *info_out = info;
    *entry_value_out = entry_value;
    return 1;
}

static int keyboard_fat_entry_is_eoc(keyboard_fat_type_t type, uint32_t value) {
    switch (type) {
        case KEYBOARD_FAT_12:
            return value >= 0x0FF8U;
        case KEYBOARD_FAT_16:
            return value >= 0xFFF8U;
        case KEYBOARD_FAT_32:
            return value >= 0x0FFFFFF8U;
        default:
            return 0;
    }
}

static int keyboard_fat_entry_is_bad(keyboard_fat_type_t type, uint32_t value) {
    switch (type) {
        case KEYBOARD_FAT_12:
            return value == 0x0FF7U;
        case KEYBOARD_FAT_16:
            return value == 0xFFF7U;
        case KEYBOARD_FAT_32:
            return value == 0x0FFFFFF7U;
        default:
            return 0;
    }
}

static int keyboard_fat_entry_is_reserved(keyboard_fat_type_t type, uint32_t value) {
    switch (type) {
        case KEYBOARD_FAT_12:
            return value >= 0x0FF0U && value <= 0x0FF6U;
        case KEYBOARD_FAT_16:
            return value >= 0xFFF0U && value <= 0xFFF6U;
        case KEYBOARD_FAT_32:
            return value >= 0x0FFFFFF0U && value <= 0x0FFFFFF6U;
        default:
            return 0;
    }
}

static void keyboard_fat_entry_dump(uint32_t device_index,
                                    uint32_t boot_lba,
                                    uint32_t cluster) {
    keyboard_fat_info_t info;
    uint32_t entry_value;

    terminal_write("Reading FAT entry on block device #");
    keyboard_write_u32(device_index);
    terminal_write("  boot ");
    keyboard_write_u32(boot_lba);
    terminal_write("  cluster ");
    keyboard_write_u32(cluster);
    terminal_writeline(" ...");

    if (!keyboard_fat_entry_read(device_index, boot_lba, cluster, &info, &entry_value)) {
        terminal_writeline("FAT entry unavailable");
        keyboard_log_event("TERMOB: fatentry failed");
        return;
    }

    terminal_writeline("FAT entry:");
    terminal_write("  Type         : ");
    terminal_writeline(keyboard_fat_type_name(info.type));
    terminal_write("  Cluster      : ");
    keyboard_write_u32(cluster);
    terminal_putchar('\n');
    terminal_write("  Raw value    : ");
    keyboard_write_hex32(entry_value);
    terminal_putchar('\n');
    terminal_write("  Meaning      : ");
    if (entry_value == 0U) {
        terminal_writeline("free cluster");
    } else if (keyboard_fat_entry_is_bad(info.type, entry_value)) {
        terminal_writeline("bad cluster marker");
    } else if (keyboard_fat_entry_is_eoc(info.type, entry_value)) {
        terminal_writeline("end of chain");
    } else if (keyboard_fat_entry_is_reserved(info.type, entry_value)) {
        terminal_writeline("reserved marker");
    } else {
        terminal_write("next cluster ");
        keyboard_write_u32(entry_value);
        terminal_putchar('\n');
    }

    keyboard_log_event("TERMOB: fatentry ok");
}

static void keyboard_fat_chain_dump(uint32_t device_index,
                                    uint32_t boot_lba,
                                    uint32_t start_cluster) {
    keyboard_fat_info_t info;
    uint32_t chain[64];
    uint32_t cluster;
    uint32_t count;
    uint32_t entry_value;
    uint32_t index;

    terminal_write("Walking FAT chain on block device #");
    keyboard_write_u32(device_index);
    terminal_write("  boot ");
    keyboard_write_u32(boot_lba);
    terminal_write("  start ");
    keyboard_write_u32(start_cluster);
    terminal_writeline(" ...");

    if (start_cluster < 2U) {
        terminal_writeline("Cluster must be >= 2");
        keyboard_log_event("TERMOB: fatchain failed");
        return;
    }

    cluster = start_cluster;
    count = 0U;
    for (;;) {
        if (count >= 64U) {
            terminal_writeline("Chain too long for preview");
            keyboard_log_event("TERMOB: fatchain truncated");
            return;
        }

        for (index = 0U; index < count; index++) {
            if (chain[index] == cluster) {
                terminal_write("Loop detected at cluster ");
                keyboard_write_u32(cluster);
                terminal_putchar('\n');
                keyboard_log_event("TERMOB: fatchain loop");
                return;
            }
        }

        chain[count++] = cluster;
        if (!keyboard_fat_entry_read(device_index, boot_lba, cluster, &info, &entry_value)) {
            terminal_writeline("FAT chain unavailable");
            keyboard_log_event("TERMOB: fatchain failed");
            return;
        }

        terminal_write("  ");
        keyboard_write_u32(cluster);
        terminal_write(" -> ");

        if (entry_value == 0U) {
            terminal_writeline("FREE");
            keyboard_log_event("TERMOB: fatchain free");
            return;
        }
        if (keyboard_fat_entry_is_bad(info.type, entry_value)) {
            terminal_writeline("BAD");
            keyboard_log_event("TERMOB: fatchain bad");
            return;
        }
        if (keyboard_fat_entry_is_reserved(info.type, entry_value)) {
            terminal_writeline("RESERVED");
            keyboard_log_event("TERMOB: fatchain reserved");
            return;
        }
        if (keyboard_fat_entry_is_eoc(info.type, entry_value)) {
            terminal_writeline("EOC");
            keyboard_log_event("TERMOB: fatchain ok");
            return;
        }

        keyboard_write_u32(entry_value);
        terminal_putchar('\n');
        cluster = entry_value;
    }
}

typedef struct keyboard_fat_ls_context {
    uint32_t visible_entries;
    uint32_t deleted_entries;
    uint32_t lfn_entries;
    uint32_t suspicious_entries;
} keyboard_fat_ls_context_t;

static void keyboard_write_name_padded(const char* text, size_t width) {
    size_t length;
    size_t index;

    length = keyboard_strlen(text);
    if (length <= width) {
        terminal_write(text);
        if (length < width) {
            keyboard_write_spaces(width - length);
        }
        return;
    }

    if (width <= 1U) {
        terminal_putchar('~');
        return;
    }

    for (index = 0U; index < width - 1U; index++) {
        terminal_putchar(text[index]);
    }
    terminal_putchar('~');
}

static int keyboard_fat_ls_callback(const termob_fat_fs_t* fs,
                                    const termob_fat_dirent_t* dirent,
                                    void* user) {
    keyboard_fat_ls_context_t* context;

    (void)fs;

    context = (keyboard_fat_ls_context_t*)user;
    if (dirent->kind == TERMOB_FAT_DIRENT_DELETED) {
        context->deleted_entries++;
        return 1;
    }

    if (dirent->suspicious) {
        context->suspicious_entries++;
    }

    if (dirent->has_long_name != 0U) {
        context->lfn_entries++;
    }

    context->visible_entries++;
    switch (dirent->kind) {
        case TERMOB_FAT_DIRENT_DIRECTORY:
            terminal_write("[D] ");
            break;
        case TERMOB_FAT_DIRENT_VOLUME_LABEL:
            terminal_write("[V] ");
            break;
        case TERMOB_FAT_DIRENT_RESERVED:
            terminal_write("[?] ");
            break;
        case TERMOB_FAT_DIRENT_FILE:
        default:
            terminal_write("[F] ");
            break;
    }

    keyboard_write_name_padded(dirent->display_name, 24U);
    if (dirent->kind == TERMOB_FAT_DIRENT_FILE) {
        keyboard_write_spaces(1U);
        keyboard_write_u32(dirent->size_bytes);
        terminal_write(" bytes");
    } else if (dirent->kind == TERMOB_FAT_DIRENT_DIRECTORY) {
        terminal_write(" <DIR>");
    }

    terminal_write("  cluster=");
    keyboard_write_u32(dirent->first_cluster);
    if (dirent->suspicious) {
        terminal_write("  !");
    }
    terminal_putchar('\n');
    return 1;
}

static void keyboard_fat_print_stat(const termob_fat_fs_t* fs) {
    terminal_writeline("FAT runtime mount:");
    terminal_write("  Type         : ");
    terminal_writeline(fat_type_name(fs->fat_type));
    terminal_write("  Device       : #");
    keyboard_write_u32((uint32_t)fs->device_index);
    terminal_write("  boot ");
    keyboard_write_u32(fs->boot_lba);
    terminal_putchar('\n');
    terminal_write("  Bytes/sector : ");
    keyboard_write_u32(fs->bytes_per_sector);
    terminal_putchar('\n');
    terminal_write("  Cluster size : ");
    keyboard_write_u32(fs->sectors_per_cluster);
    terminal_writeline(" sectors");
    terminal_write("  Reserved     : ");
    keyboard_write_u32(fs->reserved_sectors);
    terminal_writeline(" sectors");
    terminal_write("  FAT count    : ");
    keyboard_write_u32(fs->fat_count);
    terminal_putchar('\n');
    terminal_write("  Sectors/FAT  : ");
    keyboard_write_u32(fs->sectors_per_fat);
    terminal_putchar('\n');
    terminal_write("  Total sectors: ");
    keyboard_write_u32(fs->total_sectors);
    terminal_putchar('\n');
    terminal_write("  First FAT    : ");
    keyboard_write_u32(fs->first_fat_lba);
    terminal_putchar('\n');
    terminal_write("  First data   : ");
    keyboard_write_u32(fs->first_data_lba);
    terminal_putchar('\n');
    if (fs->fat_type == TERMOB_FAT_TYPE_32) {
        terminal_write("  Root cluster : ");
        keyboard_write_u32(fs->root_cluster);
        terminal_putchar('\n');
    } else {
        terminal_write("  Root sector  : ");
        keyboard_write_u32(fs->first_root_dir_lba);
        terminal_write("  span ");
        keyboard_write_u32(fs->root_dir_sectors);
        terminal_writeline(" sectors");
    }
    terminal_write("  Clusters     : ");
    keyboard_write_u32(fs->cluster_count);
    terminal_putchar('\n');
}

static int keyboard_fat_mount_from_shell(uint32_t device_index,
                                         uint32_t boot_lba,
                                         termob_fat_fs_t* out_fs) {
    if (!fat_mount(out_fs, (size_t)device_index, boot_lba)) {
        terminal_writeline("FAT mount failed");
        return 0;
    }

    return 1;
}

static void keyboard_fat_ls_root_command(uint32_t device_index, uint32_t boot_lba) {
    keyboard_fat_ls_context_t context;
    termob_fat_fs_t fs;

    if (!keyboard_fat_mount_from_shell(device_index, boot_lba, &fs)) {
        keyboard_log_event("TERMOB: fatls mount failed");
        return;
    }

    context.visible_entries = 0U;
    context.deleted_entries = 0U;
    context.lfn_entries = 0U;
    context.suspicious_entries = 0U;

    terminal_writeline("FAT root directory:");
    if (!fat_list_root(&fs, keyboard_fat_ls_callback, &context)) {
        terminal_writeline("Directory read failed");
        keyboard_log_event("TERMOB: fatls failed");
        return;
    }

    terminal_write("Visible      : ");
    keyboard_write_u32(context.visible_entries);
    terminal_putchar('\n');
    terminal_write("Deleted skip : ");
    keyboard_write_u32(context.deleted_entries);
    terminal_putchar('\n');
    terminal_write("Long names   : ");
    keyboard_write_u32(context.lfn_entries);
    terminal_putchar('\n');
    terminal_write("Suspicious   : ");
    keyboard_write_u32(context.suspicious_entries);
    terminal_putchar('\n');

    keyboard_log_event("TERMOB: fatls ok");
}

static void keyboard_fat_ls_directory_command(uint32_t device_index,
                                              uint32_t boot_lba,
                                              uint32_t start_cluster) {
    keyboard_fat_ls_context_t context;
    termob_fat_fs_t fs;

    if (!keyboard_fat_mount_from_shell(device_index, boot_lba, &fs)) {
        keyboard_log_event("TERMOB: fatlsdir mount failed");
        return;
    }

    context.visible_entries = 0U;
    context.deleted_entries = 0U;
    context.lfn_entries = 0U;
    context.suspicious_entries = 0U;

    terminal_write("FAT directory cluster ");
    keyboard_write_u32(start_cluster);
    terminal_writeline(":");
    if (!fat_list_directory(&fs, start_cluster, keyboard_fat_ls_callback, &context)) {
        terminal_writeline("Directory read failed");
        keyboard_log_event("TERMOB: fatlsdir failed");
        return;
    }

    terminal_write("Visible      : ");
    keyboard_write_u32(context.visible_entries);
    terminal_putchar('\n');
    terminal_write("Deleted skip : ");
    keyboard_write_u32(context.deleted_entries);
    terminal_putchar('\n');
    terminal_write("Long names   : ");
    keyboard_write_u32(context.lfn_entries);
    terminal_putchar('\n');
    terminal_write("Suspicious   : ");
    keyboard_write_u32(context.suspicious_entries);
    terminal_putchar('\n');

    keyboard_log_event("TERMOB: fatlsdir ok");
}

static void keyboard_fat_lookup_command(uint32_t device_index,
                                        uint32_t boot_lba,
                                        const char* path) {
    termob_fat_fs_t fs;
    termob_fat_lookup_result_t result;
    termob_fat_status_t status;

    if (!keyboard_fat_mount_from_shell(device_index, boot_lba, &fs)) {
        keyboard_log_event("TERMOB: fatlookup mount failed");
        return;
    }

    status = fat_lookup_path(&fs, path, &result);
    if (status != TERMOB_FAT_STATUS_OK) {
        terminal_write("Path lookup failed: ");
        terminal_writeline(fat_status_name(status));
        keyboard_log_event("TERMOB: fatlookup failed");
        return;
    }

    terminal_writeline("FAT path lookup:");
    terminal_write("  Path       : ");
    terminal_writeline(path);
    if (result.is_root != 0U) {
        terminal_writeline("  Target     : root directory");
        keyboard_log_event("TERMOB: fatlookup ok");
        return;
    }

    terminal_write("  Name       : ");
    terminal_writeline(result.dirent.display_name);
    terminal_write("  Short      : ");
    terminal_writeline(result.dirent.short_name);
    terminal_write("  Kind       : ");
    terminal_writeline(fat_dirent_kind_name(result.dirent.kind));
    terminal_write("  Cluster    : ");
    keyboard_write_u32(result.dirent.first_cluster);
    terminal_putchar('\n');
    terminal_write("  Size       : ");
    keyboard_write_u32(result.dirent.size_bytes);
    terminal_writeline(" bytes");
    terminal_write("  Suspicious : ");
    terminal_writeline(result.dirent.suspicious != 0U ? "yes" : "no");
    keyboard_log_event("TERMOB: fatlookup ok");
}

static void keyboard_fat_ls_path_command(uint32_t device_index,
                                         uint32_t boot_lba,
                                         const char* path) {
    keyboard_fat_ls_context_t context;
    termob_fat_fs_t fs;
    termob_fat_status_t status;

    if (!keyboard_fat_mount_from_shell(device_index, boot_lba, &fs)) {
        keyboard_log_event("TERMOB: fatlspath mount failed");
        return;
    }

    context.visible_entries = 0U;
    context.deleted_entries = 0U;
    context.lfn_entries = 0U;
    context.suspicious_entries = 0U;

    terminal_write("FAT directory path ");
    terminal_writeline(path);
    status = fat_list_path(&fs, path, keyboard_fat_ls_callback, &context);
    if (status != TERMOB_FAT_STATUS_OK) {
        terminal_write("Directory read failed: ");
        terminal_writeline(fat_status_name(status));
        keyboard_log_event("TERMOB: fatlspath failed");
        return;
    }

    terminal_write("Visible      : ");
    keyboard_write_u32(context.visible_entries);
    terminal_putchar('\n');
    terminal_write("Deleted skip : ");
    keyboard_write_u32(context.deleted_entries);
    terminal_putchar('\n');
    terminal_write("Long names   : ");
    keyboard_write_u32(context.lfn_entries);
    terminal_putchar('\n');
    terminal_write("Suspicious   : ");
    keyboard_write_u32(context.suspicious_entries);
    terminal_putchar('\n');
    keyboard_log_event("TERMOB: fatlspath ok");
}

static void keyboard_write_filtered_file_bytes(const uint8_t* buffer, size_t length) {
    size_t index;

    for (index = 0U; index < length; index++) {
        uint8_t c;

        c = buffer[index];
        if ((c >= 32U && c <= 126U) || c == '\n' || c == '\r' || c == '\t') {
            terminal_putchar((char)c);
        } else {
            terminal_putchar('.');
        }
    }
}

static void keyboard_dump_bytes_hex_ascii(const uint8_t* buffer,
                                          size_t length,
                                          uint32_t base_offset) {
    size_t offset;
    size_t index;

    for (offset = 0U; offset < length; offset += KEYBOARD_FAT_HEXDUMP_LINE_BYTES) {
        keyboard_write_hex32(base_offset + (uint32_t)offset);
        terminal_write(": ");

        for (index = 0U; index < KEYBOARD_FAT_HEXDUMP_LINE_BYTES; index++) {
            if (offset + index < length) {
                keyboard_write_hex8(buffer[offset + index]);
            } else {
                terminal_write("  ");
            }
            terminal_putchar(' ');
        }

        terminal_write(" |");
        for (index = 0U; index < KEYBOARD_FAT_HEXDUMP_LINE_BYTES && offset + index < length; index++) {
            uint8_t c;

            c = buffer[offset + index];
            if (c >= 32U && c <= 126U) {
                terminal_putchar((char)c);
            } else {
                terminal_putchar('.');
            }
        }
        terminal_writeline("|");
    }
}

static int keyboard_chunk_looks_text(const uint8_t* buffer, size_t length) {
    size_t index;
    size_t suspicious_count;

    suspicious_count = 0U;
    for (index = 0U; index < length; index++) {
        uint8_t c;

        c = buffer[index];
        if ((c >= 32U && c <= 126U) || c == '\n' || c == '\r' || c == '\t') {
            continue;
        }

        if (c == 0U) {
            return 0;
        }

        suspicious_count++;
    }

    return (suspicious_count * 100U) <= (length * KEYBOARD_FAT_CAT_BINARY_THRESHOLD_PERCENT);
}

static void keyboard_fat_cat_command(uint32_t device_index,
                                     uint32_t boot_lba,
                                     const char* path) {
    termob_fat_fs_t fs;
    termob_fat_file_t file;
    termob_fat_status_t status;
    uint8_t chunk[KEYBOARD_FAT_CAT_CHUNK_BYTES];
    size_t total_read;
    uint8_t ended_with_newline;

    if (!keyboard_fat_mount_from_shell(device_index, boot_lba, &fs)) {
        keyboard_log_event("TERMOB: fatcat mount failed");
        return;
    }

    status = fat_open_file(&fs, path, &file);
    if (status != TERMOB_FAT_STATUS_OK) {
        terminal_write("File open failed: ");
        terminal_writeline(fat_status_name(status));
        keyboard_log_event("TERMOB: fatcat open failed");
        return;
    }

    terminal_write("FAT file ");
    terminal_writeline(file.dirent.has_long_name != 0U ? file.dirent.display_name
                                                       : file.dirent.short_name);
    terminal_write("Path        : ");
    terminal_writeline(path);
    terminal_write("Size        : ");
    keyboard_write_u32(file.dirent.size_bytes);
    terminal_writeline(" bytes");
    total_read = 0U;
    ended_with_newline = 0U;

    while (total_read < KEYBOARD_FAT_CAT_MAX_BYTES) {
        size_t chunk_limit;
        size_t chunk_read;

        chunk_limit = KEYBOARD_FAT_CAT_MAX_BYTES - total_read;
        if (chunk_limit > sizeof(chunk)) {
            chunk_limit = sizeof(chunk);
        }

        status = fat_read_file(&file, chunk, chunk_limit, &chunk_read);
        if (chunk_read > 0U) {
            if (!keyboard_chunk_looks_text(chunk, chunk_read)) {
                if (!ended_with_newline && total_read > 0U) {
                    terminal_putchar('\n');
                }
                terminal_writeline("fatcat refused binary-looking data");
                keyboard_log_event("TERMOB: fatcat rejected binary-looking file");
                return;
            }

            keyboard_write_filtered_file_bytes(chunk, chunk_read);
            ended_with_newline = chunk[chunk_read - 1U] == '\n' ? 1U : 0U;
            total_read += chunk_read;
        }

        if (status != TERMOB_FAT_STATUS_OK) {
            if (!ended_with_newline) {
                terminal_putchar('\n');
            }
            terminal_write("Read failed: ");
            terminal_writeline(fat_status_name(status));
            keyboard_log_event("TERMOB: fatcat read failed");
            return;
        }

        if (chunk_read == 0U) {
            break;
        }
    }

    if (total_read == 0U) {
        terminal_writeline("<empty>");
    } else if (!ended_with_newline) {
        terminal_putchar('\n');
    }

    if (file.position < file.dirent.size_bytes) {
        terminal_writeline("[output truncated]");
    }

    keyboard_log_event("TERMOB: fatcat ok");
}

static void keyboard_fat_read_command(uint32_t device_index,
                                      uint32_t boot_lba,
                                      const char* path,
                                      uint32_t offset,
                                      uint32_t count) {
    termob_fat_fs_t fs;
    termob_fat_lookup_result_t lookup;
    termob_fat_status_t status;
    uint8_t buffer[KEYBOARD_FAT_READ_MAX_BYTES];
    size_t read_bytes;

    if (count == 0U || count > KEYBOARD_FAT_READ_MAX_BYTES) {
        terminal_write("Count must be between 1 and ");
        keyboard_write_u32(KEYBOARD_FAT_READ_MAX_BYTES);
        terminal_putchar('\n');
        return;
    }

    if (!keyboard_fat_mount_from_shell(device_index, boot_lba, &fs)) {
        keyboard_log_event("TERMOB: fatread mount failed");
        return;
    }

    status = fat_lookup_path(&fs, path, &lookup);
    if (status != TERMOB_FAT_STATUS_OK) {
        terminal_write("Path lookup failed: ");
        terminal_writeline(fat_status_name(status));
        keyboard_log_event("TERMOB: fatread lookup failed");
        return;
    }

    if (lookup.is_root != 0U || lookup.dirent.kind != TERMOB_FAT_DIRENT_FILE) {
        terminal_writeline("Target is not a file");
        keyboard_log_event("TERMOB: fatread target not file");
        return;
    }

    status = fat_read_file_range(&fs, path, offset, buffer, count, &read_bytes);
    if (status != TERMOB_FAT_STATUS_OK) {
        terminal_write("Read failed: ");
        terminal_writeline(fat_status_name(status));
        keyboard_log_event("TERMOB: fatread failed");
        return;
    }

    terminal_write("FAT read     : ");
    terminal_writeline(lookup.dirent.has_long_name != 0U ? lookup.dirent.display_name
                                                         : lookup.dirent.short_name);
    terminal_write("Path         : ");
    terminal_writeline(path);
    terminal_write("Offset       : ");
    keyboard_write_u32(offset);
    terminal_putchar('\n');
    terminal_write("Bytes        : ");
    keyboard_write_u32((uint32_t)read_bytes);
    terminal_putchar('\n');

    if (read_bytes == 0U) {
        terminal_writeline("<eof>");
    } else {
        keyboard_dump_bytes_hex_ascii(buffer, read_bytes, offset);
    }

    keyboard_log_event("TERMOB: fatread ok");
}

static void keyboard_fat_ls_target_command(uint32_t device_index,
                                           uint32_t boot_lba,
                                           const char* path) {
    keyboard_fat_ls_context_t context;
    termob_fat_fs_t fs;
    termob_fat_status_t status;

    if (!keyboard_fat_mount_from_shell(device_index, boot_lba, &fs)) {
        keyboard_log_event("TERMOB: ls mount failed");
        return;
    }

    context.visible_entries = 0U;
    context.deleted_entries = 0U;
    context.lfn_entries = 0U;
    context.suspicious_entries = 0U;

    terminal_write("Listing path : ");
    terminal_writeline(path != 0 ? path : "/");
    status = fat_list_path(&fs, path != 0 ? path : "/", keyboard_fat_ls_callback, &context);
    if (status != TERMOB_FAT_STATUS_OK) {
        terminal_write("Directory read failed: ");
        terminal_writeline(fat_status_name(status));
        keyboard_log_event("TERMOB: ls failed");
        return;
    }

    terminal_write("Visible      : ");
    keyboard_write_u32(context.visible_entries);
    terminal_putchar('\n');
    terminal_write("Long names   : ");
    keyboard_write_u32(context.lfn_entries);
    terminal_putchar('\n');
    terminal_write("Suspicious   : ");
    keyboard_write_u32(context.suspicious_entries);
    terminal_putchar('\n');
    keyboard_log_event("TERMOB: ls ok");
}

static void keyboard_write_uptime_value(void) {
    uint32_t ticks;
    uint32_t frequency;
    uint32_t seconds;
    uint32_t milliseconds;

    ticks = timer_get_ticks();
    frequency = timer_get_frequency_hz();
    seconds = ticks / frequency;
    milliseconds = ((ticks % frequency) * 1000U) / frequency;

    keyboard_write_u32(seconds);
    terminal_putchar('.');
    if (milliseconds < 100U) {
        terminal_putchar('0');
    }
    if (milliseconds < 10U) {
        terminal_putchar('0');
    }
    keyboard_write_u32(milliseconds);
    terminal_write(" s");
}

static __attribute__((noreturn)) void keyboard_halt_forever(void) {
    __asm__ volatile ("cli");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static __attribute__((noreturn)) void keyboard_reboot_system(void) {
    uint32_t timeout;

    __asm__ volatile ("cli");

    timeout = 100000U;
    while (timeout > 0U) {
        if ((keyboard_inb(0x64) & 0x02U) == 0U) {
            break;
        }
        timeout--;
    }

    keyboard_outb(0x64, 0xFE);
    keyboard_halt_forever();
}

static void keyboard_show_help(void) {
    size_t index;

    keyboard_panel_border();
    keyboard_panel_title_row("TERMOB SHELL COMMANDS");
    keyboard_panel_border();

    for (index = 0U; index < (sizeof(keyboard_help_entries) / sizeof(keyboard_help_entries[0]));
         index++) {
        keyboard_panel_row(keyboard_help_entries[index].category,
                           keyboard_help_entries[index].summary);
    }

    keyboard_panel_blank_row();
    keyboard_panel_row("Discovery", "start with help, then status or meminfo");
    keyboard_panel_row("Note", "control commands act immediately");
    keyboard_panel_border();
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static const char* keyboard_lookup_usage_hint(const char* command) {
    size_t index;

    for (index = 0U; index < (sizeof(keyboard_usage_entries) / sizeof(keyboard_usage_entries[0]));
         index++) {
        if (keyboard_streq(command, keyboard_usage_entries[index].command)) {
            return keyboard_usage_entries[index].usage;
        }
    }

    return 0;
}

static void keyboard_show_dashboard(void) {
    char line[80];
    size_t index;
    const scheduler_task_t* current_task;

    current_task = scheduler_current_task();

    keyboard_panel_border();
    keyboard_panel_title_row("TERMOB SYSTEM DASHBOARD");
    keyboard_panel_border();
    keyboard_panel_row("Kernel", TERMOB_KERNEL_NAME " " TERMOB_KERNEL_VERSION);

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_text(line, &index, sizeof(line), TERMOB_KERNEL_ARCH);
    keyboard_buffer_append_text(line, &index, sizeof(line), "  ");
    keyboard_buffer_append_text(line, &index, sizeof(line), TERMOB_KERNEL_PROFILE);
    keyboard_panel_row("Profile", line);

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_uptime(line, &index, sizeof(line));
    keyboard_buffer_append_text(line, &index, sizeof(line), "  | ticks ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), timer_get_ticks());
    keyboard_panel_row("Runtime", line);

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_text(line, &index, sizeof(line), "heap ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_used());
    keyboard_buffer_append_text(line, &index, sizeof(line), "/");
    keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_total());
    keyboard_buffer_append_text(line, &index, sizeof(line), "  freeblk ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_free_block_count());
    keyboard_panel_row("Heap", line);

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_text(line, &index, sizeof(line), "pmm free ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), pmm_free_frames());
    keyboard_buffer_append_text(line, &index, sizeof(line), "  paging ");
    keyboard_buffer_append_text(line, &index, sizeof(line), paging_is_enabled() ? "on" : "off");
    keyboard_panel_row("Memory", line);

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_text(line, &index, sizeof(line), "dev ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)device_count());
    keyboard_buffer_append_text(line, &index, sizeof(line), "  drv ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)driver_count());
    keyboard_buffer_append_text(line, &index, sizeof(line), "  pci ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)pci_device_count());
    keyboard_panel_row("Drivers", line);

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_text(line, &index, sizeof(line), "blk ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)block_device_count());
    keyboard_buffer_append_text(line, &index, sizeof(line), "  virtio ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)virtio_logical_device_count());
    keyboard_panel_row("Storage", line);

    keyboard_panel_row("FAT", "read-only tools ready: fatstat fatls fatcat");

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_text(line, &index, sizeof(line), "speaker ");
    keyboard_buffer_append_text(line, &index, sizeof(line), sound_is_initialized() ? "on" : "off");
    keyboard_buffer_append_text(line, &index, sizeof(line), "  ac97 ");
    keyboard_buffer_append_text(line, &index, sizeof(line), audio_ac97_is_ready() ? "ready" : "off");
    keyboard_panel_row("Audio", line);

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_text(line, &index, sizeof(line), mouse_is_initialized() ? "ps/2 on" : "ps/2 off");
    keyboard_buffer_append_text(line, &index, sizeof(line), "  wheel ");
    keyboard_buffer_append_text(line, &index, sizeof(line), mouse_has_wheel() ? "yes" : "no");
    keyboard_panel_row("Mouse", line);

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_text(line, &index, sizeof(line), "tasks ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), scheduler_task_count());
    keyboard_buffer_append_text(line, &index, sizeof(line), "  ready ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), scheduler_ready_task_count());
    keyboard_buffer_append_text(line, &index, sizeof(line), "  slice ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), scheduler_timeslice_ticks());
    keyboard_panel_row("Sched", line);

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_text(line, &index, sizeof(line), "house ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), kernel_housekeeping_run_count());
    keyboard_buffer_append_text(line, &index, sizeof(line), "  tele ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), kernel_telemetry_run_count());
    keyboard_panel_row("Threads", line);

    keyboard_panel_row("Current", current_task != 0 ? current_task->name : "none");

    line[0] = '\0';
    index = 0U;
    keyboard_buffer_append_text(line, &index, sizeof(line), "klog ");
    keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)klog_size());
    keyboard_buffer_append_text(line, &index, sizeof(line), " bytes");
    keyboard_panel_row("Trace", line);
    keyboard_panel_border();
}

static void keyboard_process_command(const char* cmd) {
    const char* usage_hint;

    terminal_writeline("");

    if (cmd[0] == '\0') {
        keyboard_shell_prompt_fresh();
        return;
    }

    usage_hint = keyboard_lookup_usage_hint(cmd);
    if (usage_hint != 0) {
        keyboard_shell_usage_and_prompt(usage_hint);
        return;
    }

    if (keyboard_streq(cmd, "help")) {
        keyboard_show_help();
    } else if (keyboard_streq(cmd, "dashboard") || keyboard_streq(cmd, "sysview")) {
        keyboard_show_dashboard();
    } else if (keyboard_streq(cmd, "sched")) {
        scheduler_dump_to_terminal();
    } else if (keyboard_streq(cmd, "clear")) {
        kernel_draw_ui();
        keyboard_reset_input_buffer();
        keyboard_shell_capture_prompt_anchor();
        return;
    } else if (keyboard_streq(cmd, "info")) {
        terminal_writeline("Architecture : " TERMOB_KERNEL_ARCH);
        terminal_writeline("Profile      : " TERMOB_KERNEL_PROFILE);
        terminal_writeline("Display Mode : VGA Text 80x25");
        terminal_writeline("Kernel State : Running");
        terminal_write("Timer        : PIT ");
        keyboard_write_u32(timer_get_frequency_hz());
        terminal_writeline(" Hz");
        terminal_writeline("Monitor      : live footer runtime metrics enabled");
        terminal_write("Support      : ");
        terminal_writeline(TERMOB_KERNEL_SUPPORT_EMAIL);
    } else if (keyboard_streq(cmd, "perf")) {
        char line[80];
        size_t index;

        keyboard_panel_border();
        keyboard_panel_title_row("PERFORMANCE SNAPSHOT");
        keyboard_panel_border();

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_uptime(line, &index, sizeof(line));
        keyboard_panel_row("Uptime", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "used ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_used());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  free ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_free());
        keyboard_panel_row("Heap", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "free ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), pmm_free_frames());
        keyboard_buffer_append_text(line, &index, sizeof(line), " frames");
        keyboard_panel_row("PMM", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "total ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)device_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  pci ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)pci_device_count());
        keyboard_panel_row("Device", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "drops ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), keyboard_scancode_dropped);
        keyboard_buffer_append_text(line, &index, sizeof(line), "  mouse irq ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), mouse_irq_count());
        keyboard_panel_row("Input", line);

        keyboard_panel_row("Sound", sound_is_initialized() ? "PC speaker online" : "offline");
        keyboard_panel_border();
    } else if (keyboard_streq(cmd, "status")) {
        char line[80];
        size_t index;

        keyboard_panel_border();
        keyboard_panel_title_row("KERNEL RUNTIME STATUS");
        keyboard_panel_border();
        keyboard_panel_row("State", "running");

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_uptime(line, &index, sizeof(line));
        keyboard_buffer_append_text(line, &index, sizeof(line), "  | PIT ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), timer_get_frequency_hz());
        keyboard_buffer_append_text(line, &index, sizeof(line), " Hz");
        keyboard_panel_row("Clock", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "ticks ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), timer_get_ticks());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  log ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)klog_size());
        keyboard_panel_row("Trace", line);

        keyboard_panel_row("Serial", serial_is_enabled() ? "COM1 online" : "offline");

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "used ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_used());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  free ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_free());
        keyboard_panel_row("Heap", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "free ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), pmm_free_frames());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  paging ");
        keyboard_buffer_append_text(line,
                                    &index,
                                    sizeof(line),
                                    paging_is_enabled() ? "on" : "off");
        keyboard_panel_row("Memory", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "dev ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)device_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  drv ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)driver_count());
        keyboard_panel_row("Model", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "pci ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)pci_device_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  virtio ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)virtio_logical_device_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  vblk ");
        keyboard_buffer_append_u32(line,
                                   &index,
                                   sizeof(line),
                                   (uint32_t)virtio_blk_bound_device_count());
        keyboard_panel_row("Bus", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "blk ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)block_device_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  layer ");
        keyboard_buffer_append_text(line, &index, sizeof(line), block_is_initialized() ? "on" : "off");
        keyboard_panel_row("Store", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "kbd drops ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), keyboard_scancode_dropped);
        keyboard_buffer_append_text(line, &index, sizeof(line), "  mouse ");
        keyboard_buffer_append_text(line,
                                    &index,
                                    sizeof(line),
                                    mouse_is_initialized() ? (mouse_has_wheel() ? "wheel" : "ps/2")
                                                           : "off");
        keyboard_panel_row("Input", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "tasks ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), scheduler_task_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  current ");
        keyboard_buffer_append_text(line,
                                    &index,
                                    sizeof(line),
                                    scheduler_current_task() != 0 ? scheduler_current_task()->name
                                                                  : "none");
        keyboard_panel_row("Sched", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "house ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), kernel_housekeeping_run_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  tele ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), kernel_telemetry_run_count());
        keyboard_panel_row("Threads", line);

        keyboard_panel_row("Sound", sound_is_initialized() ? "PC speaker online" : "offline");
        keyboard_panel_border();
    } else if (keyboard_streq(cmd, "meminfo")) {
        char line[80];
        size_t index;

        keyboard_panel_border();
        keyboard_panel_title_row("MEMORY OVERVIEW");
        keyboard_panel_border();
        keyboard_panel_row("Heap", heap_is_initialized() ? "ready" : "offline");

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "base ");
        keyboard_buffer_append_hex32(line, &index, sizeof(line), heap_start_address());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  limit ");
        keyboard_buffer_append_hex32(line, &index, sizeof(line), heap_end_address());
        keyboard_panel_row("Layout", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "total ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_total());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  used ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_used());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  free ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_free());
        keyboard_panel_row("Usage", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "free blocks ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_free_block_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  largest ");
        keyboard_buffer_append_u32(line,
                                   &index,
                                   sizeof(line),
                                   (uint32_t)heap_largest_free_block());
        keyboard_panel_row("Free list", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "map ");
        keyboard_buffer_append_text(line,
                                    &index,
                                    sizeof(line),
                                    bootinfo_is_valid() ? "ready" : "off");
        keyboard_buffer_append_text(line, &index, sizeof(line), "  pmm ");
        keyboard_buffer_append_text(line,
                                    &index,
                                    sizeof(line),
                                    pmm_is_initialized() ? "ready" : "off");
        keyboard_panel_row("Boot", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_u32(line, &index, sizeof(line), bootinfo_usable_memory_bytes());
        keyboard_buffer_append_text(line, &index, sizeof(line), " bytes");
        keyboard_panel_row("Usable", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "top ");
        keyboard_buffer_append_hex32(line,
                                     &index,
                                     sizeof(line),
                                     bootinfo_highest_usable_address());
        keyboard_panel_row("Highest", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "total ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), pmm_total_frames());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  free ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), pmm_free_frames());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  used ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), pmm_used_frames());
        keyboard_panel_row("Frames", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_u32(line, &index, sizeof(line), pmm_bitmap_bytes());
        keyboard_buffer_append_text(line, &index, sizeof(line), " bytes");
        keyboard_panel_row("Bitmap", line);

        keyboard_panel_blank_row();
        keyboard_panel_row("Paging", paging_is_enabled() ? "enabled" : "offline");

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_hex32(line,
                                     &index,
                                     sizeof(line),
                                     paging_directory_physical_address());
        keyboard_panel_row("Dir", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_u32(line, &index, sizeof(line), paging_identity_mapped_bytes());
        keyboard_buffer_append_text(line, &index, sizeof(line), " bytes");
        keyboard_panel_row("Mapped", line);
        keyboard_panel_border();
    } else if (keyboard_streq(cmd, "heapinfo")) {
        char line[80];
        size_t index;

        keyboard_panel_border();
        keyboard_panel_title_row("HEAP DIAGNOSTICS");
        keyboard_panel_border();
        keyboard_panel_row("Heap", heap_is_initialized() ? "ready" : "offline");
        keyboard_panel_row("Integrity", heap_integrity_status() ? "clean" : "corrupt or suspect");

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "base ");
        keyboard_buffer_append_hex32(line, &index, sizeof(line), heap_start_address());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  limit ");
        keyboard_buffer_append_hex32(line, &index, sizeof(line), heap_end_address());
        keyboard_panel_row("Region", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "total ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_total());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  used ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_used());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  free ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_bytes_free());
        keyboard_panel_row("Bytes", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "total ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_total_block_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  used ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_used_block_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  free ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), (uint32_t)heap_free_block_count());
        keyboard_panel_row("Blocks", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "largest ");
        keyboard_buffer_append_u32(line,
                                   &index,
                                   sizeof(line),
                                   (uint32_t)heap_largest_free_block());
        keyboard_buffer_append_text(line, &index, sizeof(line), " bytes");
        keyboard_panel_row("Largest", line);

        line[0] = '\0';
        index = 0U;
        keyboard_buffer_append_text(line, &index, sizeof(line), "count ");
        keyboard_buffer_append_u32(line, &index, sizeof(line), heap_error_count());
        keyboard_buffer_append_text(line, &index, sizeof(line), "  flags ");
        keyboard_buffer_append_hex32(line, &index, sizeof(line), heap_error_flags());
        keyboard_panel_row("Errors", line);
        keyboard_panel_row("Last", heap_last_error_message());
        keyboard_panel_border();
    } else if (keyboard_streq(cmd, "paging")) {
        terminal_writeline("Paging subsystem:");
        terminal_write("  State      : ");
        terminal_writeline(paging_is_enabled() ? "Enabled" : "Offline");
        terminal_write("  Directory  : ");
        keyboard_write_hex32(paging_directory_physical_address());
        terminal_putchar('\n');
        terminal_write("  Tables     : ");
        keyboard_write_u32(paging_table_count());
        terminal_writeline(" tables");
        terminal_write("  Page size  : ");
        keyboard_write_u32(paging_page_size_bytes());
        terminal_writeline(" bytes");
        terminal_write("  Identity   : ");
        keyboard_write_u32(paging_identity_mapped_bytes());
        terminal_writeline(" bytes");
    } else if (keyboard_streq(cmd, "pmminfo")) {
        terminal_writeline("Physical frame allocator:");
        terminal_write("  State      : ");
        terminal_writeline(pmm_is_initialized() ? "Ready" : "Offline");
        terminal_write("  Frame size : ");
        keyboard_write_u32(pmm_frame_size());
        terminal_writeline(" bytes");
        terminal_write("  Total      : ");
        keyboard_write_u32(pmm_total_frames());
        terminal_writeline(" frames");
        terminal_write("  Free       : ");
        keyboard_write_u32(pmm_free_frames());
        terminal_writeline(" frames");
        terminal_write("  Used       : ");
        keyboard_write_u32(pmm_used_frames());
        terminal_writeline(" frames");
        terminal_write("  Bitmap     : ");
        keyboard_write_u32(pmm_bitmap_bytes());
        terminal_writeline(" bytes");
    } else if (keyboard_streq(cmd, "heapcheck")) {
        terminal_write("Heap check   : ");
        terminal_writeline(heap_check_integrity() ? "ok" : "failed");
        terminal_write("Used blocks  : ");
        keyboard_write_u32((uint32_t)heap_used_block_count());
        terminal_putchar('\n');
        terminal_write("Free blocks  : ");
        keyboard_write_u32((uint32_t)heap_free_block_count());
        terminal_putchar('\n');
        terminal_write("Largest free : ");
        keyboard_write_u32((uint32_t)heap_largest_free_block());
        terminal_writeline(" bytes");
        terminal_write("Errors seen  : ");
        keyboard_write_u32(heap_error_count());
        terminal_putchar('\n');
        terminal_write("Last error   : ");
        terminal_writeline(heap_last_error_message());
    } else if (keyboard_streq(cmd, "heaptest")) {
        terminal_writeline("Running heap self-test...");
        terminal_write("Heap selftest: ");
        terminal_writeline(heap_run_self_test() ? "ok" : "failed");
    } else if (keyboard_streq(cmd, "memmap")) {
        bootinfo_memory_range_t range;
        uint32_t range_index;

        terminal_writeline("Multiboot2 memory map:");
        terminal_write("  Entries    : ");
        keyboard_write_u32((uint32_t)bootinfo_memory_range_count());
        terminal_putchar('\n');

        for (range_index = 0; bootinfo_memory_range_at(range_index, &range); range_index++) {
            keyboard_write_hex32(range.base_addr);
            terminal_write(" - ");
            keyboard_write_hex32(range.base_addr + range.length);
            terminal_write("  ");
            terminal_writeline(keyboard_memory_type_name(range.type));
        }
    } else if (keyboard_streq(cmd, "lspci")) {
        pci_dump_to_terminal();
    } else if (keyboard_streq(cmd, "drivers")) {
        device_dump_to_terminal();
    } else if (keyboard_streq(cmd, "mouse")) {
        mouse_dump_to_terminal();
    } else if (keyboard_streq(cmd, "virtio")) {
        virtio_dump_to_terminal();
    } else if (keyboard_streq(cmd, "vblk")) {
        virtio_blk_dump_to_terminal();
    } else if (keyboard_streq(cmd, "lsblk")) {
        block_dump_to_terminal();
    } else if (keyboard_streq(cmd, "blkread0")) {
        keyboard_block_read_preview(0U, 0U, 1U);
    } else if (keyboard_startswith(cmd, "blkread ")) {
        const char* args;
        const char* next;
        uint32_t first_value;
        uint32_t second_value;

        args = cmd + 8;
        if (!keyboard_parse_u32_token(args, &next, &first_value)) {
            keyboard_shell_usage_and_prompt("blkread <lba>  or  blkread <device> <lba>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next == '\0') {
            keyboard_block_read_preview(0U, first_value, 1U);
        } else {
            if (!keyboard_parse_u32_token(next, &next, &second_value)) {
                keyboard_shell_usage_and_prompt("blkread <lba>  or  blkread <device> <lba>");
                return;
            }

            next = keyboard_skip_spaces(next);
            if (*next != '\0') {
                keyboard_shell_usage_and_prompt("blkread <lba>  or  blkread <device> <lba>");
                return;
            }

            keyboard_block_read_preview(first_value, second_value, 1U);
        }
    } else if (keyboard_startswith(cmd, "blkreadn ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t lba;
        uint32_t sector_count;

        args = cmd + 9;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &lba) ||
            !keyboard_parse_u32_token(next, &next, &sector_count)) {
            keyboard_shell_usage_and_prompt("blkreadn <device> <lba> <count>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("blkreadn <device> <lba> <count>");
            return;
        }

        keyboard_block_read_preview(device_index, lba, sector_count);
    } else if (keyboard_startswith(cmd, "blksig ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t lba;

        args = cmd + 7;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &lba)) {
            keyboard_shell_usage_and_prompt("blksig <device> <lba>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("blksig <device> <lba>");
            return;
        }

        keyboard_block_signature_preview(device_index, lba);
    } else if (keyboard_startswith(cmd, "blkfind ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t lba;
        uint32_t sector_count;

        args = cmd + 8;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &lba) ||
            !keyboard_parse_u32_token(next, &next, &sector_count)) {
            keyboard_shell_usage_and_prompt("blkfind <device> <lba> <count>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("blkfind <device> <lba> <count>");
            return;
        }

        keyboard_block_find_signatures(device_index, lba, sector_count);
    } else if (keyboard_startswith(cmd, "bootchk ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t lba;

        args = cmd + 8;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &lba)) {
            keyboard_shell_usage_and_prompt("bootchk <device> <lba>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("bootchk <device> <lba>");
            return;
        }

        keyboard_boot_sector_check(device_index, lba);
    } else if (keyboard_startswith(cmd, "fatinfo ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t lba;

        args = cmd + 8;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &lba)) {
            keyboard_shell_usage_and_prompt("fatinfo <device> <lba>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("fatinfo <device> <lba>");
            return;
        }

        keyboard_fat_info_dump(device_index, lba);
    } else if (keyboard_startswith(cmd, "fatcheck ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t lba;

        args = cmd + 9;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &lba)) {
            keyboard_shell_usage_and_prompt("fatcheck <device> <lba>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("fatcheck <device> <lba>");
            return;
        }

        keyboard_fat_check(device_index, lba);
    } else if (keyboard_startswith(cmd, "fatentry ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t boot_lba;
        uint32_t cluster;

        args = cmd + 9;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba) ||
            !keyboard_parse_u32_token(next, &next, &cluster)) {
            keyboard_shell_usage_and_prompt("fatentry <device> <boot_lba> <cluster>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("fatentry <device> <boot_lba> <cluster>");
            return;
        }

        keyboard_fat_entry_dump(device_index, boot_lba, cluster);
    } else if (keyboard_startswith(cmd, "fatchain ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t boot_lba;
        uint32_t cluster;

        args = cmd + 9;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba) ||
            !keyboard_parse_u32_token(next, &next, &cluster)) {
            keyboard_shell_usage_and_prompt("fatchain <device> <boot_lba> <cluster>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("fatchain <device> <boot_lba> <cluster>");
            return;
        }

        keyboard_fat_chain_dump(device_index, boot_lba, cluster);
    } else if (keyboard_startswith(cmd, "fatstat ")) {
        const char* args;
        const char* next;
        termob_fat_fs_t fs;
        uint32_t device_index;
        uint32_t boot_lba;

        args = cmd + 8;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba)) {
            keyboard_shell_usage_and_prompt("fatstat <device> <boot_lba>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("fatstat <device> <boot_lba>");
            return;
        }

        if (!keyboard_fat_mount_from_shell(device_index, boot_lba, &fs)) {
            keyboard_log_event("TERMOB: fatstat failed");
            keyboard_shell_prompt_fresh();
            return;
        }

        keyboard_fat_print_stat(&fs);
        keyboard_log_event("TERMOB: fatstat ok");
    } else if (keyboard_startswith(cmd, "fatlsroot ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t boot_lba;

        args = cmd + 10;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba)) {
            keyboard_shell_usage_and_prompt("fatlsroot <device> <boot_lba>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("fatlsroot <device> <boot_lba>");
            return;
        }

        keyboard_fat_ls_root_command(device_index, boot_lba);
    } else if (keyboard_startswith(cmd, "ls ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t boot_lba;

        args = cmd + 3;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba)) {
            keyboard_shell_usage_and_prompt("ls <device> <boot_lba> [</dir>]");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next == '\0') {
            keyboard_fat_ls_target_command(device_index, boot_lba, "/");
        } else {
            keyboard_fat_ls_target_command(device_index, boot_lba, next);
        }
    } else if (keyboard_startswith(cmd, "fatls ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t boot_lba;

        args = cmd + 6;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba)) {
            keyboard_shell_usage_and_prompt("fatls <device> <boot_lba>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("fatls <device> <boot_lba>");
            return;
        }

        keyboard_fat_ls_root_command(device_index, boot_lba);
    } else if (keyboard_startswith(cmd, "fatlsdir ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t boot_lba;
        uint32_t start_cluster;

        args = cmd + 9;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba) ||
            !keyboard_parse_u32_token(next, &next, &start_cluster)) {
            keyboard_shell_usage_and_prompt("fatlsdir <device> <boot_lba> <cluster>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next != '\0') {
            keyboard_shell_usage_and_prompt("fatlsdir <device> <boot_lba> <cluster>");
            return;
        }

        keyboard_fat_ls_directory_command(device_index, boot_lba, start_cluster);
    } else if (keyboard_startswith(cmd, "fatlookup ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t boot_lba;

        args = cmd + 10;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba)) {
            keyboard_shell_usage_and_prompt("fatlookup <device> <boot_lba> </path>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next == '\0') {
            keyboard_shell_usage_and_prompt("fatlookup <device> <boot_lba> </path>");
            return;
        }

        keyboard_fat_lookup_command(device_index, boot_lba, next);
    } else if (keyboard_startswith(cmd, "fatlspath ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t boot_lba;

        args = cmd + 10;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba)) {
            keyboard_shell_usage_and_prompt("fatlspath <device> <boot_lba> </dir>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next == '\0') {
            keyboard_shell_usage_and_prompt("fatlspath <device> <boot_lba> </dir>");
            return;
        }

        keyboard_fat_ls_path_command(device_index, boot_lba, next);
    } else if (keyboard_startswith(cmd, "fatcat ")) {
        const char* args;
        const char* next;
        uint32_t device_index;
        uint32_t boot_lba;

        args = cmd + 7;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba)) {
            keyboard_shell_usage_and_prompt("fatcat <device> <boot_lba> </file>");
            return;
        }

        next = keyboard_skip_spaces(next);
        if (*next == '\0') {
            keyboard_shell_usage_and_prompt("fatcat <device> <boot_lba> </file>");
            return;
        }

        keyboard_fat_cat_command(device_index, boot_lba, next);
    } else if (keyboard_startswith(cmd, "fatread ")) {
        const char* args;
        const char* next;
        const char* rest;
        const char* end;
        const char* count_start;
        const char* offset_end;
        const char* offset_start;
        const char* path_end;
        uint32_t device_index;
        uint32_t boot_lba;
        uint32_t offset;
        uint32_t count;
        char path_buffer[KEYBOARD_MAX_INPUT];
        size_t path_length;
        size_t i;

        args = cmd + 8;
        if (!keyboard_parse_u32_token(args, &next, &device_index) ||
            !keyboard_parse_u32_token(next, &next, &boot_lba)) {
            keyboard_shell_usage_and_prompt("fatread <device> <boot_lba> </file> <offset> <count>");
            return;
        }

        rest = keyboard_skip_spaces(next);
        if (*rest == '\0' || *rest != '/') {
            keyboard_shell_usage_and_prompt("fatread <device> <boot_lba> </file> <offset> <count>");
            return;
        }

        end = rest + keyboard_strlen(rest);
        end = keyboard_skip_spaces_backwards(rest, end);
        count_start = keyboard_find_token_start_backwards(rest, end);
        if (!keyboard_parse_u32_span(count_start, end, &count)) {
            keyboard_shell_usage_and_prompt("fatread <device> <boot_lba> </file> <offset> <count>");
            return;
        }

        offset_end = keyboard_skip_spaces_backwards(rest, count_start);
        offset_start = keyboard_find_token_start_backwards(rest, offset_end);
        if (!keyboard_parse_u32_span(offset_start, offset_end, &offset)) {
            keyboard_shell_usage_and_prompt("fatread <device> <boot_lba> </file> <offset> <count>");
            return;
        }

        path_end = keyboard_skip_spaces_backwards(rest, offset_start);
        path_length = (size_t)(path_end - rest);
        if (path_length == 0U || path_length >= sizeof(path_buffer)) {
            keyboard_shell_print_error("path too long");
            keyboard_shell_prompt_fresh();
            return;
        }

        for (i = 0U; i < path_length; i++) {
            path_buffer[i] = rest[i];
        }
        path_buffer[path_length] = '\0';
        keyboard_fat_read_command(device_index, boot_lba, path_buffer, offset, count);
    } else if (keyboard_streq(cmd, "sound")) {
        keyboard_panel_border();
        keyboard_panel_title_row("SOUND SUBSYSTEM");
        keyboard_panel_border();
        keyboard_panel_row("State", sound_is_initialized() ? "PC speaker online" : "offline");
        keyboard_panel_row("Backend", "PIT channel 2 + speaker port 0x61");
        keyboard_panel_row("Quality", "PC speaker live + AC'97 PCM test tone");
        keyboard_panel_row("Beep", "880 Hz / 350 ms via beep");
        keyboard_panel_row("Melody", "test theme via melody");
        keyboard_panel_row("AC97", "run-ac97 + ac97tone for PCM path");
        keyboard_panel_row("Host", "SDL default via make run");
        keyboard_panel_row("Proof", "make audio-proof writes build/termob-speaker.wav");
        keyboard_panel_border();
    } else if (keyboard_streq(cmd, "audio")) {
        audio_dump_to_terminal();
    } else if (keyboard_streq(cmd, "ac97")) {
        audio_dump_ac97_to_terminal();
    } else if (keyboard_streq(cmd, "ac97tone")) {
        terminal_writeline("Playing AC'97 PCM test tone...");
        if (!audio_ac97_play_test_tone()) {
            terminal_writeline("AC'97 PCM test tone unavailable");
        }
    } else if (keyboard_streq(cmd, "uname")) {
        terminal_write(TERMOB_KERNEL_NAME);
        terminal_write(" ");
        terminal_write(TERMOB_KERNEL_VERSION);
        terminal_write(" ");
        terminal_write(TERMOB_KERNEL_ARCH);
        terminal_write(" ");
        terminal_writeline(TERMOB_KERNEL_PROFILE);
    } else if (keyboard_streq(cmd, "version")) {
        terminal_writeline(TERMOB_KERNEL_NAME " " TERMOB_KERNEL_VERSION);
    } else if (keyboard_streq(cmd, "about")) {
        terminal_writeline("TERMOB OS Project");
        terminal_writeline("Custom monolithic kernel");
        terminal_writeline("Built from scratch in WSL");
    } else if (keyboard_streq(cmd, "uptime")) {
        terminal_write("Uptime       : ");
        keyboard_write_uptime_value();
        terminal_putchar('\n');
    } else if (keyboard_streq(cmd, "ticks")) {
        terminal_write("Timer ticks  : ");
        keyboard_write_u32(timer_get_ticks());
        terminal_putchar('\n');
    } else if (keyboard_streq(cmd, "logsize")) {
        terminal_write("Kernel log   : ");
        keyboard_write_u32((uint32_t)klog_size());
        terminal_writeline(" bytes");
    } else if (keyboard_streq(cmd, "dmesg")) {
        klog_dump_to_terminal();
    } else if (keyboard_streq(cmd, "clearlog")) {
        klog_clear();
        keyboard_log_event("TERMOB: kernel log cleared");
        terminal_writeline("Kernel log cleared");
    } else if (keyboard_streq(cmd, "beep")) {
        terminal_writeline("Playing test tone...");
        sound_beep(880U, 350U);
    } else if (keyboard_streq(cmd, "melody")) {
        terminal_writeline("Playing kernel theme...");
        sound_play_melody();
    } else if (keyboard_startswith(cmd, "echo ")) {
        terminal_writeline(cmd + 5);
    } else if (keyboard_streq(cmd, "halt")) {
        keyboard_log_event("TERMOB: halt requested from shell");
        terminal_writeline("System halted.");
        keyboard_halt_forever();
    } else if (keyboard_streq(cmd, "reboot")) {
        keyboard_log_event("TERMOB: reboot requested from shell");
        terminal_writeline("Rebooting...");
        keyboard_reboot_system();
    } else if (keyboard_streq(cmd, "panic")) {
        keyboard_log_event("TERMOB: panic requested from shell");
        kernel_panic_simple("Manual panic", "panic command invoked from shell");
    } else {
        keyboard_shell_print_unknown_command(cmd);
    }

    keyboard_shell_prompt_fresh();
}

void keyboard_init(void) {
    keyboard_buffer_index = 0;
    keyboard_cursor_index = 0;
    keyboard_clipboard_length = 0;
    keyboard_last_pressed_scancode = 0;
    keyboard_control_down = 0;
    keyboard_left_shift_down = 0;
    keyboard_right_shift_down = 0;
    keyboard_caps_lock_on = 0;
    keyboard_extended_prefix_pending = 0;
    keyboard_scancode_head = 0;
    keyboard_scancode_tail = 0;
    keyboard_scancode_dropped = 0;
    keyboard_prompt_row = terminal_get_row();
    keyboard_prompt_column = terminal_get_column();
    keyboard_input_buffer[0] = '\0';
    keyboard_clipboard_buffer[0] = '\0';
}

int keyboard_has_pending_scancode(void) {
    return (keyboard_inb(0x64) & 0x01U) != 0U;
}

uint8_t keyboard_read_scancode(void) {
    return keyboard_inb(0x60);
}

void keyboard_enqueue_scancode(uint8_t scancode) {
    uint32_t head;
    uint32_t next_head;

    head = keyboard_scancode_head;
    next_head = (head + 1U) % KEYBOARD_SCANCODE_QUEUE_SIZE;

    if (next_head == keyboard_scancode_tail) {
        keyboard_scancode_dropped++;
        return;
    }

    keyboard_scancode_queue[head] = scancode;
    keyboard_scancode_head = next_head;
}

void keyboard_drain_pending(void) {
    while (keyboard_scancode_tail != keyboard_scancode_head) {
        uint8_t scancode;

        scancode = keyboard_scancode_queue[keyboard_scancode_tail];
        keyboard_scancode_tail =
            (keyboard_scancode_tail + 1U) % KEYBOARD_SCANCODE_QUEUE_SIZE;
        keyboard_process_scancode(scancode);
    }
}

static void keyboard_process_extended_scancode(uint8_t scancode) {
    if ((scancode & 0x80U) != 0U) {
        return;
    }

    if (scancode == KEYBOARD_PAGE_UP_SCANCODE) {
        terminal_scrollback_page_up();
        return;
    }

    if (scancode == KEYBOARD_PAGE_DOWN_SCANCODE) {
        terminal_scrollback_page_down();
        if (!terminal_scrollback_is_active()) {
            keyboard_shell_redraw_input();
        }
        return;
    }

    if (scancode == KEYBOARD_LEFT_ARROW_SCANCODE) {
        keyboard_move_cursor_left();
        return;
    }

    if (scancode == KEYBOARD_RIGHT_ARROW_SCANCODE) {
        keyboard_move_cursor_right();
        return;
    }

    if (scancode == KEYBOARD_DELETE_SCANCODE) {
        keyboard_delete_character_at_cursor();
    }
}

void keyboard_process_scancode(uint8_t scancode) {
    char command_buffer[KEYBOARD_MAX_INPUT];
    char c;
    int index;

    if (scancode == KEYBOARD_EXTENDED_SCANCODE_PREFIX) {
        keyboard_extended_prefix_pending = 1U;
        return;
    }

    if (keyboard_extended_prefix_pending != 0U) {
        keyboard_extended_prefix_pending = 0U;
        keyboard_process_extended_scancode(scancode);
        return;
    }

    if ((scancode & 0x80) != 0) {
        uint8_t released;

        released = (uint8_t)(scancode & 0x7F);
        if (released == KEYBOARD_CTRL_SCANCODE) {
            keyboard_control_down = 0;
        }
        if (released == KEYBOARD_LEFT_SHIFT_SCANCODE) {
            keyboard_left_shift_down = 0;
        }
        if (released == KEYBOARD_RIGHT_SHIFT_SCANCODE) {
            keyboard_right_shift_down = 0;
        }
        if (released == keyboard_last_pressed_scancode) {
            keyboard_last_pressed_scancode = 0;
        }
        return;
    }

    if (scancode == KEYBOARD_CTRL_SCANCODE) {
        keyboard_control_down = 1;
        keyboard_last_pressed_scancode = scancode;
        return;
    }

    if (scancode == KEYBOARD_LEFT_SHIFT_SCANCODE) {
        keyboard_left_shift_down = 1;
        keyboard_last_pressed_scancode = scancode;
        return;
    }

    if (scancode == KEYBOARD_RIGHT_SHIFT_SCANCODE) {
        keyboard_right_shift_down = 1;
        keyboard_last_pressed_scancode = scancode;
        return;
    }

    if (scancode == keyboard_last_pressed_scancode) {
        return;
    }
    keyboard_last_pressed_scancode = scancode;

    if (scancode == KEYBOARD_CAPS_LOCK_SCANCODE) {
        keyboard_caps_lock_on = keyboard_caps_lock_on == 0U ? 1U : 0U;
        return;
    }

    if (terminal_scrollback_is_active()) {
        terminal_scrollback_follow();
        keyboard_shell_redraw_input();
    }

    if (scancode >= 128) {
        return;
    }

    c = keyboard_translate_scancode(scancode);
    if (c == 0) {
        return;
    }

    if (keyboard_control_down != 0U) {
        if (scancode == KEYBOARD_C_SCANCODE) {
            keyboard_copy_input_to_clipboard();
            return;
        }

        if (scancode == KEYBOARD_V_SCANCODE) {
            keyboard_paste_clipboard();
            return;
        }

        if (scancode == KEYBOARD_U_SCANCODE) {
            keyboard_clear_input_line();
            return;
        }

        if (scancode == KEYBOARD_L_SCANCODE) {
            kernel_draw_ui();
            keyboard_shell_capture_prompt_anchor();
            keyboard_shell_redraw_input();
            return;
        }
    }

    if (c == '\n') {
        for (index = 0; index <= keyboard_buffer_index; index++) {
            command_buffer[index] = keyboard_input_buffer[index];
        }

        keyboard_reset_input_buffer();
        keyboard_process_command(command_buffer);
        return;
    }

    if (c == '\b') {
        keyboard_delete_character_before_cursor();
        return;
    }

    keyboard_insert_character(c);
}

#include "../include/kernel.h"
#include "../include/klog.h"
#include "../include/keyboard.h"
#include "../include/kernel_ui.h"
#include "../include/panic.h"
#include "../include/serial.h"
#include "../include/terminal.h"
#include "../include/timer.h"

#define KEYBOARD_PROMPT_MIN_COL 15
#define KEYBOARD_MAX_INPUT 256

static const char keyboard_scancode_table[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0
};

static char keyboard_input_buffer[KEYBOARD_MAX_INPUT];
static int keyboard_buffer_index;
static uint8_t keyboard_last_pressed_scancode;

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

static void keyboard_shell_prompt_fresh(void) {
    terminal_prompt();
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

static void keyboard_process_command(const char* cmd) {
    terminal_writeline("");

    if (cmd[0] == '\0') {
        keyboard_shell_prompt_fresh();
        return;
    }

    if (keyboard_streq(cmd, "help")) {
        terminal_writeline("Commands:");
        terminal_writeline("  help      show commands");
        terminal_writeline("  clear     redraw interface");
        terminal_writeline("  info      system info");
        terminal_writeline("  status    kernel runtime status");
        terminal_writeline("  uname     print kernel identity");
        terminal_writeline("  version   kernel version");
        terminal_writeline("  about     project info");
        terminal_writeline("  uptime    show kernel uptime");
        terminal_writeline("  ticks     show raw timer ticks");
        terminal_writeline("  logsize   show kernel log size");
        terminal_writeline("  dmesg     show kernel log buffer");
        terminal_writeline("  clearlog  clear kernel log buffer");
        terminal_writeline("  echo ...  print text");
        terminal_writeline("  halt      stop the CPU");
        terminal_writeline("  reboot    restart the machine");
        terminal_writeline("  panic     test kernel panic screen");
    } else if (keyboard_streq(cmd, "clear")) {
        kernel_draw_ui();
        keyboard_buffer_index = 0;
        return;
    } else if (keyboard_streq(cmd, "info")) {
        terminal_writeline("Architecture : " TERMOB_KERNEL_ARCH);
        terminal_writeline("Profile      : " TERMOB_KERNEL_PROFILE);
        terminal_writeline("Display Mode : VGA Text 80x25");
        terminal_writeline("Kernel State : Running");
        terminal_write("Timer        : PIT ");
        keyboard_write_u32(timer_get_frequency_hz());
        terminal_writeline(" Hz");
    } else if (keyboard_streq(cmd, "status")) {
        terminal_writeline("Kernel runtime status:");
        terminal_writeline("  State      : Running");
        terminal_write("  Uptime     : ");
        keyboard_write_uptime_value();
        terminal_putchar('\n');
        terminal_write("  Timer      : PIT ");
        keyboard_write_u32(timer_get_frequency_hz());
        terminal_writeline(" Hz");
        terminal_write("  Ticks      : ");
        keyboard_write_u32(timer_get_ticks());
        terminal_putchar('\n');
        terminal_write("  Log size   : ");
        keyboard_write_u32((uint32_t)klog_size());
        terminal_writeline(" bytes");
        terminal_write("  Serial     : ");
        terminal_writeline(serial_is_enabled() ? "COM1 online" : "offline");
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
        terminal_writeline("Unknown command");
    }

    keyboard_shell_prompt_fresh();
}

void keyboard_init(void) {
    keyboard_buffer_index = 0;
    keyboard_last_pressed_scancode = 0;
}

uint8_t keyboard_read_scancode(void) {
    return keyboard_inb(0x60);
}

void keyboard_process_scancode(uint8_t scancode) {
    size_t col;
    size_t row;
    char c;

    if ((scancode & 0x80) != 0) {
        uint8_t released;

        released = (uint8_t)(scancode & 0x7F);
        if (released == keyboard_last_pressed_scancode) {
            keyboard_last_pressed_scancode = 0;
        }
        return;
    }

    if (scancode == keyboard_last_pressed_scancode) {
        return;
    }
    keyboard_last_pressed_scancode = scancode;

    if (scancode >= 128) {
        return;
    }

    c = keyboard_scancode_table[scancode];
    if (c == 0) {
        return;
    }

    if (c == '\n') {
        keyboard_input_buffer[keyboard_buffer_index] = '\0';
        keyboard_process_command(keyboard_input_buffer);
        keyboard_buffer_index = 0;
        return;
    }

    if (c == '\b') {
        if (keyboard_buffer_index > 0) {
            keyboard_buffer_index--;
            col = terminal_get_column();
            row = terminal_get_row();

            if (col > KEYBOARD_PROMPT_MIN_COL) {
                terminal_set_cursor(col - 1, row);
                terminal_putchar(' ');
                terminal_set_cursor(col - 1, row);
            }
        }
        return;
    }

    if (keyboard_buffer_index < KEYBOARD_MAX_INPUT - 1) {
        keyboard_input_buffer[keyboard_buffer_index++] = c;
        terminal_putchar(c);
    }
}

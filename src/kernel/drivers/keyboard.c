#include "../include/kernel.h"
#include "../include/terminal.h"
#include "../include/keyboard.h"
#include "../include/kernel_ui.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_PROMPT_MIN_COL 17
#define KEYBOARD_MAX_INPUT 256

static const char keyboard_scancode_table[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0
};

static char keyboard_input_buffer[KEYBOARD_MAX_INPUT];
static int keyboard_buffer_index = 0;
static unsigned char keyboard_last_pressed_scancode = 0;

static inline unsigned char keyboard_inb(unsigned short port) {
    unsigned char result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static int keyboard_streq(const char* a, const char* b) {
    int i;

    i = 0;
    while (a[i] && b[i]) {
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
    while (prefix[i]) {
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
        terminal_writeline("  version   kernel version");
        terminal_writeline("  about     project info");
        terminal_writeline("  echo ...  print text");
    } else if (keyboard_streq(cmd, "clear")) {
        kernel_draw_ui();
        keyboard_buffer_index = 0;
        return;
    } else if (keyboard_streq(cmd, "info")) {
        terminal_writeline("Architecture : " TERMOB_KERNEL_ARCH);
        terminal_writeline("Profile      : " TERMOB_KERNEL_PROFILE);
        terminal_writeline("Display Mode : VGA Text 80x25");
        terminal_writeline("Kernel State : Running");
    } else if (keyboard_streq(cmd, "version")) {
        terminal_writeline(TERMOB_KERNEL_NAME " " TERMOB_KERNEL_VERSION);
    } else if (keyboard_streq(cmd, "about")) {
        terminal_writeline("TERMOB OS Project");
        terminal_writeline("Custom monolithic kernel");
        terminal_writeline("Built from scratch in WSL");
    } else if (keyboard_startswith(cmd, "echo ")) {
        terminal_writeline(cmd + 5);
    } else {
        terminal_writeline("Unknown command");
    }

    keyboard_shell_prompt_fresh();
}

void keyboard_handle(void) {
    size_t col;
    size_t row;
    unsigned char scancode;
    char c;

    if ((keyboard_inb(KEYBOARD_STATUS_PORT) & 1) == 0) {
        return;
    }

    scancode = keyboard_inb(KEYBOARD_DATA_PORT);

    if (scancode & 0x80) {
        unsigned char released;

        released = scancode & 0x7F;
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
    if (!c) {
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

void keyboard_init(void) {
}

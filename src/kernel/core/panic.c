#include "../include/panic.h"

#include "../include/kernel.h"
#include "../include/klog.h"
#include "../include/serial.h"
#include "../include/terminal.h"

static void panic_write_hex32_all(uint32_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";
    char text[11];
    int index;
    int shift;

    text[0] = '0';
    text[1] = 'x';
    index = 2;

    for (shift = 28; shift >= 0; shift -= 4) {
        text[index++] = hex_digits[(value >> shift) & 0x0F];
    }

    text[index] = '\0';

    terminal_write(text);
    serial_write(text);
    klog_write(text);
}

static void panic_write_u32_all(uint32_t value) {
    char digits[10];
    int i;

    if (value == 0) {
        terminal_putchar('0');
        serial_write_char('0');
        klog_write("0");
        return;
    }

    i = 0;
    while (value > 0 && i < 10) {
        digits[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        char text[2];
        char digit;

        digit = digits[--i];
        text[0] = digit;
        text[1] = '\0';
        terminal_putchar(digit);
        serial_write_char(digit);
        klog_write(text);
    }
}

static void panic_write_all(const char* text) {
    terminal_write(text);
    serial_write(text);
    klog_write(text);
}

static void panic_writeline_all(const char* text) {
    terminal_writeline(text);
    serial_writeline(text);
    klog_writeline(text);
}

static void panic_newline_all(void) {
    terminal_putchar('\n');
    serial_write_char('\n');
    klog_write("\n");
}

static void panic_write_label_line_hex(const char* label, uint32_t value) {
    panic_write_all(label);
    panic_write_all(": ");
    panic_write_hex32_all(value);
    panic_newline_all();
}

static void panic_write_label_line_text(const char* label, const char* value) {
    panic_write_all(label);
    panic_write_all(": ");
    panic_writeline_all(value);
}

static void panic_prepare_screen(void) {
    __asm__ volatile ("cli");

    terminal_initialize(VGA_COLOR_WHITE, VGA_COLOR_RED);
    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
}

static __attribute__((noreturn)) void panic_halt(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

__attribute__((noreturn)) void kernel_panic_simple(const char* reason, const char* detail) {
    panic_prepare_screen();

    panic_writeline_all("TERMOB KERNEL PANIC");
    panic_newline_all();
    panic_write_label_line_text("Kernel", TERMOB_KERNEL_NAME);
    panic_write_label_line_text("Version", TERMOB_KERNEL_VERSION);
    panic_newline_all();
    panic_write_label_line_text("Reason", reason);

    if (detail != 0 && detail[0] != '\0') {
        panic_write_label_line_text("Detail", detail);
    }

    panic_newline_all();
    panic_writeline_all("System halted.");
    panic_halt();
}

__attribute__((noreturn)) void kernel_panic_assertion(const char* expression,
                                                      const char* file,
                                                      uint32_t line) {
    panic_prepare_screen();

    panic_writeline_all("TERMOB KERNEL PANIC");
    panic_newline_all();
    panic_write_label_line_text("Kernel", TERMOB_KERNEL_NAME);
    panic_write_label_line_text("Version", TERMOB_KERNEL_VERSION);
    panic_newline_all();
    panic_write_label_line_text("Reason", "Assertion failed");
    panic_write_label_line_text("Expression", expression);
    panic_write_all("Location: ");
    panic_write_all(file);
    panic_write_all(":");
    panic_write_u32_all(line);
    panic_newline_all();
    panic_newline_all();
    panic_writeline_all("System halted.");
    panic_halt();
}

__attribute__((noreturn)) void kernel_panic_exception(const char* exception_name,
                                                      const interrupt_frame_t* frame) {
    panic_prepare_screen();

    panic_writeline_all("TERMOB KERNEL PANIC");
    panic_newline_all();
    panic_write_label_line_text("Kernel", TERMOB_KERNEL_NAME);
    panic_write_label_line_text("Version", TERMOB_KERNEL_VERSION);
    panic_newline_all();
    panic_write_label_line_text("Reason", "CPU Exception");
    panic_write_label_line_text("Exception", exception_name);
    panic_write_label_line_hex("Vector", frame->vector);
    panic_write_label_line_hex("ErrorCode", frame->error_code);
    panic_write_label_line_hex("EIP", frame->eip);
    panic_write_label_line_hex("CS", frame->cs);
    panic_write_label_line_hex("EFLAGS", frame->eflags);
    panic_newline_all();
    panic_writeline_all("System halted.");
    panic_halt();
}

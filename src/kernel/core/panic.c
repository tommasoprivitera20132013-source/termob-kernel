#include "../include/panic.h"

#include "../include/kernel.h"
#include "../include/klog.h"
#include "../include/serial.h"
#include "../include/sound.h"
#include "../include/terminal.h"

#define PANIC_LABEL_WIDTH 10U

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

    if (value == 0U) {
        terminal_putchar('0');
        serial_write_char('0');
        klog_write("0");
        return;
    }

    i = 0;
    while (value > 0U && i < 10) {
        digits[i++] = (char)('0' + (value % 10U));
        value /= 10U;
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

static void panic_write_spaces_all(uint32_t count) {
    while (count > 0U) {
        panic_write_all(" ");
        count--;
    }
}

static void panic_write_label_prefix(const char* label) {
    uint32_t length;

    length = 0U;
    while (label[length] != '\0') {
        length++;
    }

    panic_write_all("  ");
    panic_write_all(label);
    if (length < PANIC_LABEL_WIDTH) {
        panic_write_spaces_all(PANIC_LABEL_WIDTH - length);
    }
    panic_write_all(": ");
}

static void panic_write_label_line_hex(const char* label, uint32_t value) {
    panic_write_label_prefix(label);
    panic_write_hex32_all(value);
    panic_newline_all();
}

static void panic_write_label_line_text(const char* label, const char* value) {
    panic_write_label_prefix(label);
    panic_writeline_all(value);
}

static void panic_write_section_title(const char* title) {
    panic_newline_all();
    panic_write_all("[ ");
    panic_write_all(title);
    panic_writeline_all(" ]");
}

static void panic_write_action_hint(void) {
    panic_write_section_title("ACTION");
    panic_writeline_all("  Machine halted.");
    panic_writeline_all("  Collect this screen and COM1 output before reboot.");
}

static void panic_draw_terminal_banner(void) {
    terminal_fill_rect(0, 2, VGA_WIDTH, 6, terminal_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_draw_box(2, 2, 18, 6, terminal_make_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_draw_box(22, 2, 56, 6, terminal_make_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));

    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    terminal_writeat("    .--------.", 4, 3);
    terminal_writeat("    |  !!  !!|", 4, 4);
    terminal_writeat("    |  PANIC |", 4, 5);
    terminal_writeat("    '--------'", 4, 6);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_writeat("STATE  : halted in panic context", 24, 3);
    terminal_writeat("KERNEL : " TERMOB_KERNEL_NAME " " TERMOB_KERNEL_VERSION, 24, 4);
    terminal_writeat("TARGET : " TERMOB_KERNEL_ARCH " / " TERMOB_KERNEL_PROFILE, 24, 5);
    terminal_writeat("OUTPUT : VGA + COM1 + klog mirror active", 24, 6);
}

static void panic_prepare_screen(void) {
    uint8_t footer_color;
    uint8_t header_color;

    header_color = terminal_make_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    footer_color = terminal_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);

    terminal_initialize(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_fill_rect(0, 0, VGA_WIDTH, 2, header_color);
    terminal_fill_rect(0, 23, VGA_WIDTH, 2, footer_color);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
    terminal_writecenter("TERMOB KERNEL PANIC", 0);
    terminal_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_RED);
    terminal_writecenter("fatal kernel error | execution stopped safely", 1);
    panic_draw_terminal_banner();

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    terminal_writeat("System halted | inspect registers and COM1 log before reboot", 1, 23);
    terminal_writeat("Support: " TERMOB_KERNEL_SUPPORT_EMAIL, 1, 24);

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_set_region(9, 22);
    terminal_set_cursor(0, 9);

    sound_panic_alert();
    __asm__ volatile ("cli");
}

static void panic_write_frame_registers(const interrupt_frame_t* frame) {
    panic_writeline_all("Registers:");
    panic_write_label_line_hex("EIP", frame->eip);
    panic_write_label_line_hex("CS", frame->cs);
    panic_write_label_line_hex("EFLAGS", frame->eflags);
    panic_write_label_line_hex("EAX", frame->eax);
    panic_write_label_line_hex("EBX", frame->ebx);
    panic_write_label_line_hex("ECX", frame->ecx);
    panic_write_label_line_hex("EDX", frame->edx);
    panic_write_label_line_hex("ESI", frame->esi);
    panic_write_label_line_hex("EDI", frame->edi);
    panic_write_label_line_hex("EBP", frame->ebp);
    panic_write_label_line_hex("ESP", frame->esp);
}

static void panic_write_page_fault_details(uint32_t error_code) {
    panic_writeline_all("Page fault details:");
    panic_write_label_line_text("Cause", (error_code & 0x01U) != 0U
                                             ? "protection violation"
                                             : "non-present page");
    panic_write_label_line_text("Access", (error_code & 0x02U) != 0U ? "write" : "read");
    panic_write_label_line_text("Mode", (error_code & 0x04U) != 0U ? "user" : "supervisor");
    panic_write_label_line_text("Reserved", (error_code & 0x08U) != 0U ? "set" : "clear");
    panic_write_label_line_text("Fetch", (error_code & 0x10U) != 0U
                                            ? "instruction fetch"
                                            : "data access");
}

static __attribute__((noreturn)) void panic_halt(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

__attribute__((noreturn)) void kernel_panic_simple(const char* reason, const char* detail) {
    panic_prepare_screen();

    panic_write_section_title("SUMMARY");
    panic_write_label_line_text("Kernel", TERMOB_KERNEL_NAME);
    panic_write_label_line_text("Version", TERMOB_KERNEL_VERSION);
    panic_write_label_line_text("Support", TERMOB_KERNEL_SUPPORT_EMAIL);
    panic_write_label_line_text("Reason", reason);

    if (detail != 0 && detail[0] != '\0') {
        panic_write_label_line_text("Detail", detail);
    }

    panic_write_action_hint();
    panic_halt();
}

__attribute__((noreturn)) void kernel_panic_assertion(const char* expression,
                                                      const char* file,
                                                      uint32_t line) {
    panic_prepare_screen();

    panic_write_section_title("SUMMARY");
    panic_write_label_line_text("Kernel", TERMOB_KERNEL_NAME);
    panic_write_label_line_text("Version", TERMOB_KERNEL_VERSION);
    panic_write_label_line_text("Support", TERMOB_KERNEL_SUPPORT_EMAIL);
    panic_write_label_line_text("Reason", "Assertion failed");

    panic_write_section_title("CONTEXT");
    panic_write_label_line_text("Expression", expression);
    panic_write_label_prefix("Location");
    panic_write_all(file);
    panic_write_all(":");
    panic_write_u32_all(line);
    panic_newline_all();

    panic_write_action_hint();
    panic_halt();
}

__attribute__((noreturn)) void kernel_panic_exception(const char* exception_name,
                                                      const interrupt_frame_t* frame) {
    panic_prepare_screen();

    panic_write_section_title("SUMMARY");
    panic_write_label_line_text("Kernel", TERMOB_KERNEL_NAME);
    panic_write_label_line_text("Version", TERMOB_KERNEL_VERSION);
    panic_write_label_line_text("Support", TERMOB_KERNEL_SUPPORT_EMAIL);
    panic_write_label_line_text("Reason", "CPU Exception");
    panic_write_label_line_text("Exception", exception_name);
    panic_write_label_line_hex("Vector", frame->vector);
    panic_write_label_line_hex("ErrorCode", frame->error_code);

    panic_write_section_title("REGISTERS");
    panic_write_frame_registers(frame);

    panic_write_action_hint();
    panic_halt();
}

__attribute__((noreturn)) void kernel_panic_page_fault(const interrupt_frame_t* frame,
                                                       uint32_t fault_address) {
    panic_prepare_screen();

    panic_write_section_title("SUMMARY");
    panic_write_label_line_text("Kernel", TERMOB_KERNEL_NAME);
    panic_write_label_line_text("Version", TERMOB_KERNEL_VERSION);
    panic_write_label_line_text("Support", TERMOB_KERNEL_SUPPORT_EMAIL);
    panic_write_label_line_text("Reason", "CPU Exception");
    panic_write_label_line_text("Exception", "Page Fault");
    panic_write_label_line_hex("CR2", fault_address);
    panic_write_label_line_hex("ErrorCode", frame->error_code);

    panic_write_section_title("PAGE FAULT");
    panic_write_page_fault_details(frame->error_code);

    panic_write_section_title("REGISTERS");
    panic_write_frame_registers(frame);

    panic_write_action_hint();
    panic_halt();
}

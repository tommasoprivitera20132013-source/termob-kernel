#include "../include/device.h"
#include "../include/block.h"
#include "../include/bootinfo.h"
#include "../include/audio.h"
#include "../include/kernel.h"
#include "../include/terminal.h"
#include "../include/keyboard.h"
#include "../include/kernel_ui.h"
#include "../include/heap.h"
#include "../include/idt.h"
#include "../include/klog.h"
#include "../include/mouse.h"
#include "../include/pic.h"
#include "../include/paging.h"
#include "../include/pci.h"
#include "../include/pmm.h"
#include "../include/serial.h"
#include "../include/scheduler.h"
#include "../include/sound.h"
#include "../include/timer.h"
#include "../include/virtio.h"

#define IRQ0_VECTOR 32
#define TERMOB_SHELL_TOP 8
#define TERMOB_SHELL_BOTTOM 22
#define TERMOB_BOOT_STAGE_ROW 10
#define IRQ1_VECTOR 33
#define IRQ12_VECTOR 44

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq12_stub(void);

static uint32_t kernel_ui_last_monitor_slot = UINT32_MAX;
static volatile uint32_t kernel_housekeeping_runs;
static volatile uint32_t kernel_telemetry_runs;

static void kernel_housekeeping_task(void* context) {
    uint32_t local_runs;

    (void)context;
    local_runs = 0U;

    for (;;) {
        local_runs++;
        kernel_housekeeping_runs = local_runs;
        scheduler_yield();
    }
}

static void kernel_telemetry_task(void* context) {
    uint32_t local_runs;

    (void)context;
    local_runs = 0U;

    for (;;) {
        local_runs++;
        kernel_telemetry_runs = local_runs;
        scheduler_yield();
    }
}

uint32_t kernel_housekeeping_run_count(void) {
    return kernel_housekeeping_runs;
}

uint32_t kernel_telemetry_run_count(void) {
    return kernel_telemetry_runs;
}

static void kernel_ui_draw_boot_banner(void) {
    uint8_t header_color;
    uint8_t footer_color;
    uint8_t body_color;

    header_color = terminal_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    footer_color = terminal_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    body_color = terminal_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    terminal_initialize(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_draw_hline(0, ' ', header_color);
    terminal_draw_hline(24, ' ', footer_color);
    terminal_fill_rect(0, 1, VGA_WIDTH, 23, body_color);
    terminal_draw_box(1, 1, VGA_WIDTH - 2, 7, terminal_make_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    terminal_draw_box(1, 9, VGA_WIDTH - 2, 13, terminal_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    terminal_writeat(TERMOB_KERNEL_NAME " " TERMOB_KERNEL_VERSION, 1, 0);
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    terminal_writeat("bootstrap console", 62, 0);

    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_writeat("   _______  _______  _______ ", 4, 2);
    terminal_writeat("  |_   _\\ \\/ /_   _||  _  _|", 4, 3);
    terminal_writeat("    | |  \\  /  | |  | |_) | ", 4, 4);
    terminal_writeat("    |_|  /_/   |_|  |____/  ", 4, 5);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_writeat("monolithic i386 kernel | grub multiboot2 | vga text mode", 35, 3);
    terminal_writeat("bringing core subsystems online before shell handoff", 35, 4);
    terminal_writeat("serial logging stays active on COM1 during boot", 35, 5);

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeat("Boot stages", 3, 9);

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    terminal_writeat("Booting... watch stage updates below | serial mirror active", 1, 24);
}

static void kernel_ui_boot_stage(uint32_t slot, const char* stage, const char* detail, int ok) {
    size_t row;

    row = (size_t)(TERMOB_BOOT_STAGE_ROW + slot);
    if (row >= 22U) {
        return;
    }

    terminal_fill_rect(3, row, VGA_WIDTH - 6, 1, terminal_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    terminal_setcolor(ok ? VGA_COLOR_LIGHT_GREEN : VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_writeat(ok ? "[ok]" : "[!!]", 4, row);

    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_writeat(stage, 10, row);

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_writeat(":", 30, row);

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_writeat(detail, 32, row);
}

static void kernel_ui_draw_header(void) {
    uint8_t footer_color;
    uint8_t header_color;
    uint8_t panel_color;

    header_color = terminal_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    panel_color = terminal_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    footer_color = terminal_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);

    terminal_initialize(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_draw_hline(0, ' ', header_color);
    terminal_fill_rect(0, 1, VGA_WIDTH, 7, panel_color);
    terminal_draw_box(1, 1, VGA_WIDTH - 2, 6, terminal_make_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    terminal_draw_hline(7, ' ', footer_color);
    terminal_draw_hline(24, ' ', footer_color);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    terminal_writeat(TERMOB_KERNEL_NAME " " TERMOB_KERNEL_VERSION, 1, 0);

    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    terminal_writeat("tty0 | runtime console", 57, 0);

    terminal_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_writeat("   .----------------------.", 3, 2);
    terminal_writeat("   |   TERMOB CONSOLE     |", 3, 3);
    terminal_writeat("   |   i386 / GRUB / VGA  |", 3, 4);
    terminal_writeat("   '----------------------'", 3, 5);

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_writeat("profile : " TERMOB_KERNEL_PROFILE " " TERMOB_KERNEL_ARCH, 32, 2);
    terminal_writeat("memory  : multiboot2  pmm  heap  paging", 32, 3);
    terminal_writeat("io      : pci  virtio-blk  audio-pci  speaker  ps/2 mouse", 32, 4);
    terminal_writeat("shell   : help  dashboard  status  sched", 32, 5);

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    terminal_writeat("shell ready | try: help  dashboard  status  heapinfo  lspci", 1, 7);
}

static void kernel_ui_append_char(char* text, size_t* index, char c) {
    if (*index >= VGA_WIDTH - 1U) {
        return;
    }

    text[*index] = c;
    (*index)++;
    text[*index] = '\0';
}

static void kernel_ui_append_text(char* text, size_t* index, const char* value) {
    size_t cursor;

    cursor = 0U;
    while (value[cursor] != '\0') {
        kernel_ui_append_char(text, index, value[cursor]);
        cursor++;
    }
}

static void kernel_ui_append_u32(char* text, size_t* index, uint32_t value) {
    char digits[10];
    int i;

    if (value == 0U) {
        kernel_ui_append_char(text, index, '0');
        return;
    }

    i = 0;
    while (value > 0U && i < 10) {
        digits[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (i > 0) {
        kernel_ui_append_char(text, index, digits[--i]);
    }
}

static void kernel_ui_append_uptime(char* text, size_t* index) {
    uint32_t frequency;
    uint32_t milliseconds;
    uint32_t seconds;
    uint32_t ticks;

    ticks = timer_get_ticks();
    frequency = timer_get_frequency_hz();
    seconds = ticks / frequency;
    milliseconds = ((ticks % frequency) * 1000U) / frequency;

    kernel_ui_append_u32(text, index, seconds);
    kernel_ui_append_char(text, index, '.');
    if (milliseconds < 100U) {
        kernel_ui_append_char(text, index, '0');
    }
    if (milliseconds < 10U) {
        kernel_ui_append_char(text, index, '0');
    }
    kernel_ui_append_u32(text, index, milliseconds);
    kernel_ui_append_char(text, index, 's');
}

static void kernel_ui_draw_footer(void) {
    char line[VGA_WIDTH];
    size_t current_column;
    size_t current_row;
    size_t index;
    uint8_t bar_color;

    bar_color = terminal_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    line[0] = '\0';
    index = 0U;

    kernel_ui_append_text(line, &index, "up ");
    kernel_ui_append_uptime(line, &index);
    kernel_ui_append_text(line, &index, " | heap ");
    kernel_ui_append_u32(line, &index, (uint32_t)(heap_bytes_used() / 1024U));
    kernel_ui_append_char(line, &index, '/');
    kernel_ui_append_u32(line, &index, (uint32_t)(heap_bytes_total() / 1024U));
    kernel_ui_append_text(line, &index, "K | free ");
    kernel_ui_append_u32(line, &index, pmm_free_frames());
    kernel_ui_append_text(line, &index, " fr | pci ");
    kernel_ui_append_u32(line, &index, (uint32_t)pci_device_count());
    kernel_ui_append_text(line, &index, " | snd ");
    kernel_ui_append_text(line, &index, sound_is_initialized() ? "on" : "off");
    kernel_ui_append_text(line, &index, " | mouse ");
    kernel_ui_append_text(line, &index, mouse_is_initialized() ? "on" : "off");
    kernel_ui_append_text(line, &index, " | tsk ");
    kernel_ui_append_u32(line, &index, scheduler_task_count());

    current_row = terminal_get_row();
    current_column = terminal_get_column();

    terminal_draw_hline(24, ' ', bar_color);
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    terminal_writeat(line, 1, 24);

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_set_cursor(current_column, current_row);
}

static void kernel_ui_refresh_footer_if_needed(void) {
    uint32_t slot;

    slot = timer_get_ticks() / 20U;
    if (slot == kernel_ui_last_monitor_slot) {
        return;
    }

    kernel_ui_last_monitor_slot = slot;
    kernel_ui_draw_footer();
}

void kernel_draw_ui(void) {
    kernel_ui_draw_header();
    kernel_ui_draw_footer();

    terminal_set_region(TERMOB_SHELL_TOP, TERMOB_SHELL_BOTTOM);
    terminal_set_cursor(1, TERMOB_SHELL_TOP);
    terminal_prompt();
    kernel_ui_last_monitor_slot = UINT32_MAX;
    kernel_ui_refresh_footer_if_needed();
}

void kernel_enter_shell(void) {
    terminal_set_region(TERMOB_SHELL_TOP, TERMOB_SHELL_BOTTOM);
    terminal_set_cursor(1, TERMOB_SHELL_TOP);
    terminal_prompt();
    kernel_ui_last_monitor_slot = UINT32_MAX;
    kernel_ui_refresh_footer_if_needed();
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    serial_init();
    klog_init();
    kernel_ui_draw_boot_banner();
    kernel_ui_boot_stage(0U, "serial", "COM1 online", 1);

    bootinfo_init(multiboot_magic, multiboot_info_addr);
    kernel_ui_boot_stage(1U,
                         "multiboot2",
                         bootinfo_is_valid() ? "memory map ready" : "memory map unavailable",
                         bootinfo_is_valid());
    heap_init();
    kernel_ui_boot_stage(2U, "heap", heap_is_initialized() ? "allocator ready" : "allocator offline",
                         heap_is_initialized());
    pmm_init();
    kernel_ui_boot_stage(3U, "pmm", pmm_is_initialized() ? "frame allocator ready" : "frame allocator unavailable",
                         pmm_is_initialized());
    paging_init();
    kernel_ui_boot_stage(4U, "paging", paging_is_enabled() ? "bootstrap mapping active" : "paging unavailable",
                         paging_is_enabled());
    device_model_init();
    kernel_ui_boot_stage(5U,
                         "device model",
                         device_model_is_initialized() ? "driver registry online"
                                                       : "driver registry unavailable",
                         device_model_is_initialized());
    block_init();
    kernel_ui_boot_stage(6U, "block", block_is_initialized() ? "block layer online" : "block layer unavailable",
                         block_is_initialized());
    virtio_init();
    kernel_ui_boot_stage(7U,
                         "virtio",
                         virtio_is_initialized() ? "transport and blk hooks ready"
                                                 : "transport unavailable",
                         virtio_is_initialized());
    audio_init();
    kernel_ui_boot_stage(8U,
                         "audio pci",
                         audio_is_initialized() ? "controller skeleton online"
                                                : "controller skeleton unavailable",
                         audio_is_initialized());
    pci_init();
    kernel_ui_boot_stage(9U, "pci", pci_is_initialized() ? "enumeration ready" : "enumeration unavailable",
                         pci_is_initialized());
    sound_init();
    kernel_ui_boot_stage(10U, "sound", sound_is_initialized() ? "pc speaker ready" : "pc speaker unavailable",
                         sound_is_initialized());
    scheduler_init();
    kernel_ui_boot_stage(11U,
                         "scheduler",
                         scheduler_is_initialized() ? "thread core online" : "foundation unavailable",
                         scheduler_is_initialized());
    scheduler_create_kernel_task("housekeeping", kernel_housekeeping_task, 0);
    scheduler_create_kernel_task("telemetry", kernel_telemetry_task, 0);

    klog_writeline("TERMOB: serial online");
    klog_writeline("TERMOB: kernel boot start");
    klog_writeline(bootinfo_is_valid() ? "TERMOB: multiboot2 memory map online"
                                       : "TERMOB: multiboot2 memory map unavailable");
    klog_writeline("TERMOB: heap online");
    klog_writeline(pmm_is_initialized() ? "TERMOB: pmm online" : "TERMOB: pmm unavailable");
    klog_writeline(paging_is_enabled() ? "TERMOB: paging online" : "TERMOB: paging unavailable");
    klog_writeline(device_model_is_initialized() ? "TERMOB: device model online"
                                                 : "TERMOB: device model unavailable");
    klog_writeline(block_is_initialized() ? "TERMOB: block layer online"
                                          : "TERMOB: block layer unavailable");
    klog_writeline(virtio_is_initialized() ? "TERMOB: virtio transport + blk online"
                                           : "TERMOB: virtio transport unavailable");
    klog_writeline(audio_is_initialized() ? "TERMOB: audio pci skeleton online"
                                          : "TERMOB: audio pci skeleton unavailable");
    klog_writeline(pci_is_initialized() ? "TERMOB: pci enumeration online"
                                        : "TERMOB: pci enumeration unavailable");
    klog_writeline(sound_is_initialized() ? "TERMOB: pc speaker online"
                                          : "TERMOB: pc speaker unavailable");
    klog_writeline(scheduler_is_initialized() ? "TERMOB: scheduler foundation online"
                                              : "TERMOB: scheduler unavailable");
    serial_writeline("TERMOB: serial online");
    serial_writeline("TERMOB: kernel boot start");
    serial_writeline(bootinfo_is_valid() ? "TERMOB: multiboot2 memory map online"
                                         : "TERMOB: multiboot2 memory map unavailable");
    serial_writeline("TERMOB: heap online");
    serial_writeline(pmm_is_initialized() ? "TERMOB: pmm online" : "TERMOB: pmm unavailable");
    serial_writeline(paging_is_enabled() ? "TERMOB: paging online" : "TERMOB: paging unavailable");
    serial_writeline(device_model_is_initialized() ? "TERMOB: device model online"
                                                   : "TERMOB: device model unavailable");
    serial_writeline(block_is_initialized() ? "TERMOB: block layer online"
                                            : "TERMOB: block layer unavailable");
    serial_writeline(virtio_is_initialized() ? "TERMOB: virtio transport + blk online"
                                             : "TERMOB: virtio transport unavailable");
    serial_writeline(audio_is_initialized() ? "TERMOB: audio pci skeleton online"
                                            : "TERMOB: audio pci skeleton unavailable");
    serial_writeline(pci_is_initialized() ? "TERMOB: pci enumeration online"
                                          : "TERMOB: pci enumeration unavailable");
    serial_writeline(sound_is_initialized() ? "TERMOB: pc speaker online"
                                            : "TERMOB: pc speaker unavailable");
    serial_writeline(scheduler_is_initialized() ? "TERMOB: scheduler foundation online"
                                                : "TERMOB: scheduler unavailable");

    kernel_draw_ui();
    keyboard_init();

    idt_init();
    idt_set_gate(IRQ0_VECTOR, (uint32_t)irq0_stub);
    idt_set_gate(IRQ1_VECTOR, (uint32_t)irq1_stub);
    idt_set_gate(IRQ12_VECTOR, (uint32_t)irq12_stub);
    pic_init();
    mouse_init();
    timer_init();

    klog_writeline(mouse_is_initialized() ? "TERMOB: ps/2 mouse online"
                                          : "TERMOB: ps/2 mouse unavailable");
    serial_writeline(mouse_is_initialized() ? "TERMOB: ps/2 mouse online"
                                            : "TERMOB: ps/2 mouse unavailable");
    klog_writeline("TERMOB: idt pic pit keyboard ready");
    serial_writeline("TERMOB: idt pic pit keyboard ready");

    __asm__ volatile ("sti");

    for (;;) {
        mouse_drain_pending();
        keyboard_drain_pending();
        scheduler_dispatch();
        kernel_ui_refresh_footer_if_needed();
        __asm__ volatile ("hlt");
    }
}

#include "../include/mouse.h"

#include "../include/klog.h"
#include "../include/pic.h"
#include "../include/serial.h"
#include "../include/terminal.h"

#define MOUSE_DATA_PORT              0x60
#define MOUSE_STATUS_PORT            0x64
#define MOUSE_COMMAND_PORT           0x64
#define MOUSE_STATUS_OUTPUT_FULL     0x01U
#define MOUSE_STATUS_INPUT_FULL      0x02U
#define MOUSE_STATUS_AUX_DATA        0x20U
#define MOUSE_QUEUE_SIZE             128U
#define MOUSE_WAIT_TIMEOUT           100000U
#define MOUSE_ACK                    0xFAU
#define MOUSE_IDENTIFY_STANDARD      0x00U
#define MOUSE_IDENTIFY_WHEEL         0x03U
#define MOUSE_IDENTIFY_WHEEL_BUTTONS 0x04U

static volatile uint32_t mouse_queue_head;
static volatile uint32_t mouse_queue_tail;
static volatile uint32_t mouse_queue_dropped;
static uint8_t mouse_queue[MOUSE_QUEUE_SIZE];
static uint8_t mouse_packet[4];
static uint8_t mouse_packet_index;
static uint8_t mouse_packet_size = 3U;
static uint8_t mouse_ready;
static uint8_t mouse_wheel_ready;
static uint8_t mouse_buttons;
static int8_t mouse_last_wheel_delta;
static uint32_t mouse_irq_events;
static uint32_t mouse_packets_seen;
static uint32_t mouse_scroll_up_events;
static uint32_t mouse_scroll_down_events;
static uint32_t mouse_sync_losses;

static inline uint8_t mouse_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void mouse_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void mouse_log_event(const char* message) {
    klog_writeline(message);
    serial_writeline(message);
}

static int mouse_wait_input_ready(void) {
    uint32_t timeout;

    timeout = MOUSE_WAIT_TIMEOUT;
    while (timeout > 0U) {
        if ((mouse_inb(MOUSE_STATUS_PORT) & MOUSE_STATUS_INPUT_FULL) == 0U) {
            return 1;
        }
        timeout--;
    }

    return 0;
}

static int mouse_wait_output_ready(void) {
    uint32_t timeout;

    timeout = MOUSE_WAIT_TIMEOUT;
    while (timeout > 0U) {
        if ((mouse_inb(MOUSE_STATUS_PORT) & MOUSE_STATUS_OUTPUT_FULL) != 0U) {
            return 1;
        }
        timeout--;
    }

    return 0;
}

static int mouse_write_controller(uint8_t command) {
    if (!mouse_wait_input_ready()) {
        return 0;
    }

    mouse_outb(MOUSE_COMMAND_PORT, command);
    return 1;
}

static int mouse_write_data(uint8_t value) {
    if (!mouse_wait_input_ready()) {
        return 0;
    }

    mouse_outb(MOUSE_DATA_PORT, value);
    return 1;
}

static int mouse_read_data(uint8_t* out_value) {
    if (out_value == 0 || !mouse_wait_output_ready()) {
        return 0;
    }

    *out_value = mouse_inb(MOUSE_DATA_PORT);
    return 1;
}

static int mouse_write_device_command(uint8_t command) {
    return mouse_write_controller(0xD4U) && mouse_write_data(command);
}

static int mouse_expect_ack(void) {
    uint8_t response;

    if (!mouse_read_data(&response)) {
        return 0;
    }

    return response == MOUSE_ACK;
}

static int mouse_send_device_command(uint8_t command) {
    return mouse_write_device_command(command) && mouse_expect_ack();
}

static int mouse_send_set_sample_rate(uint8_t rate) {
    return mouse_send_device_command(0xF3U) &&
           mouse_write_device_command(rate) &&
           mouse_expect_ack();
}

static int mouse_identify(uint8_t* out_id) {
    uint8_t response;

    if (!mouse_write_device_command(0xF2U) || !mouse_expect_ack()) {
        return 0;
    }

    if (!mouse_read_data(&response)) {
        return 0;
    }

    *out_id = response;
    return 1;
}

static void mouse_enqueue_byte(uint8_t value) {
    uint32_t head;
    uint32_t next_head;

    head = mouse_queue_head;
    next_head = (head + 1U) % MOUSE_QUEUE_SIZE;
    if (next_head == mouse_queue_tail) {
        mouse_queue_dropped++;
        return;
    }

    mouse_queue[head] = value;
    mouse_queue_head = next_head;
}

static void mouse_process_packet(void) {
    int8_t wheel_delta;

    mouse_packets_seen++;
    mouse_buttons = (uint8_t)(mouse_packet[0] & 0x07U);

    if (!mouse_wheel_ready || mouse_packet_size < 4U) {
        return;
    }

    wheel_delta = (int8_t)mouse_packet[3];
    if (wheel_delta > 0) {
        mouse_scroll_up_events += (uint32_t)wheel_delta;
        while (wheel_delta > 0) {
            terminal_scrollback_line_up();
            wheel_delta--;
        }
        mouse_last_wheel_delta = mouse_packet[3];
    } else if (wheel_delta < 0) {
        int8_t steps;

        steps = (int8_t)(-wheel_delta);
        mouse_scroll_down_events += (uint32_t)steps;
        while (steps > 0) {
            terminal_scrollback_line_down();
            steps--;
        }
        mouse_last_wheel_delta = mouse_packet[3];
    } else {
        mouse_last_wheel_delta = 0;
    }
}

void mouse_init(void) {
    uint8_t config;
    uint8_t device_id;

    mouse_queue_head = 0U;
    mouse_queue_tail = 0U;
    mouse_queue_dropped = 0U;
    mouse_packet_index = 0U;
    mouse_packet_size = 3U;
    mouse_ready = 0U;
    mouse_wheel_ready = 0U;
    mouse_buttons = 0U;
    mouse_last_wheel_delta = 0;
    mouse_irq_events = 0U;
    mouse_packets_seen = 0U;
    mouse_scroll_up_events = 0U;
    mouse_scroll_down_events = 0U;
    mouse_sync_losses = 0U;

    if (!mouse_write_controller(0xA8U)) {
        mouse_log_event("TERMOB: ps/2 mouse enable failed");
        return;
    }

    if (!mouse_write_controller(0x20U) || !mouse_read_data(&config)) {
        mouse_log_event("TERMOB: ps/2 mouse config read failed");
        return;
    }

    config |= 0x02U;
    config &= (uint8_t)~0x20U;
    if (!mouse_write_controller(0x60U) || !mouse_write_data(config)) {
        mouse_log_event("TERMOB: ps/2 mouse config write failed");
        return;
    }

    if (!mouse_send_device_command(0xF6U)) {
        mouse_log_event("TERMOB: ps/2 mouse defaults command failed");
        return;
    }

    if (mouse_send_set_sample_rate(200U) &&
        mouse_send_set_sample_rate(100U) &&
        mouse_send_set_sample_rate(80U) &&
        mouse_identify(&device_id)) {
        if (device_id == MOUSE_IDENTIFY_WHEEL || device_id == MOUSE_IDENTIFY_WHEEL_BUTTONS) {
            mouse_wheel_ready = 1U;
            mouse_packet_size = 4U;
        } else if (device_id == MOUSE_IDENTIFY_STANDARD) {
            mouse_packet_size = 3U;
        }
    }

    if (!mouse_send_device_command(0xF4U)) {
        mouse_log_event("TERMOB: ps/2 mouse data reporting enable failed");
        return;
    }

    pic_clear_irq_mask(2U);
    pic_clear_irq_mask(12U);
    mouse_ready = 1U;
    mouse_log_event(mouse_wheel_ready ? "TERMOB: ps/2 mouse wheel online"
                                      : "TERMOB: ps/2 mouse online");
}

int mouse_is_initialized(void) {
    return mouse_ready != 0U;
}

int mouse_has_wheel(void) {
    return mouse_wheel_ready != 0U;
}

void mouse_handle_irq(void) {
    uint8_t status;

    status = mouse_inb(MOUSE_STATUS_PORT);
    if ((status & MOUSE_STATUS_OUTPUT_FULL) == 0U || (status & MOUSE_STATUS_AUX_DATA) == 0U) {
        return;
    }

    mouse_irq_events++;
    mouse_enqueue_byte(mouse_inb(MOUSE_DATA_PORT));
}

void mouse_drain_pending(void) {
    while (mouse_queue_tail != mouse_queue_head) {
        uint8_t value;

        value = mouse_queue[mouse_queue_tail];
        mouse_queue_tail = (mouse_queue_tail + 1U) % MOUSE_QUEUE_SIZE;

        if (mouse_packet_index == 0U && (value & 0x08U) == 0U) {
            mouse_sync_losses++;
            continue;
        }

        mouse_packet[mouse_packet_index++] = value;
        if (mouse_packet_index >= mouse_packet_size) {
            mouse_packet_index = 0U;
            mouse_process_packet();
        }
    }
}

void mouse_dump_to_terminal(void) {
    terminal_writeline("PS/2 mouse:");
    terminal_write("  State      : ");
    terminal_writeline(mouse_ready ? "Ready" : "Offline");
    terminal_write("  Wheel      : ");
    terminal_writeline(mouse_wheel_ready ? "Enabled" : "Unavailable");
    terminal_write("  Packet     : ");
    if (mouse_ready) {
        char digits[10];
        int i;
        uint32_t value;

        value = mouse_packet_size;
        i = 0;
        while (value > 0U && i < 10) {
            digits[i++] = (char)('0' + (value % 10U));
            value /= 10U;
        }
        while (i > 0) {
            terminal_putchar(digits[--i]);
        }
    } else {
        terminal_write("0");
    }
    terminal_writeline(" bytes");
    terminal_write("  IRQ events : ");
    {
        char digits[10];
        int i;
        uint32_t value;

        value = mouse_irq_events;
        if (value == 0U) {
            terminal_write("0");
        } else {
            i = 0;
            while (value > 0U && i < 10) {
                digits[i++] = (char)('0' + (value % 10U));
                value /= 10U;
            }
            while (i > 0) {
                terminal_putchar(digits[--i]);
            }
        }
    }
    terminal_putchar('\n');
    terminal_write("  Packets    : ");
    {
        char digits[10];
        int i;
        uint32_t value;

        value = mouse_packets_seen;
        if (value == 0U) {
            terminal_write("0");
        } else {
            i = 0;
            while (value > 0U && i < 10) {
                digits[i++] = (char)('0' + (value % 10U));
                value /= 10U;
            }
            while (i > 0) {
                terminal_putchar(digits[--i]);
            }
        }
    }
    terminal_putchar('\n');
    terminal_write("  Scroll up  : ");
    {
        char digits[10];
        int i;
        uint32_t value;

        value = mouse_scroll_up_events;
        if (value == 0U) {
            terminal_write("0");
        } else {
            i = 0;
            while (value > 0U && i < 10) {
                digits[i++] = (char)('0' + (value % 10U));
                value /= 10U;
            }
            while (i > 0) {
                terminal_putchar(digits[--i]);
            }
        }
    }
    terminal_write("  down ");
    {
        char digits[10];
        int i;
        uint32_t value;

        value = mouse_scroll_down_events;
        if (value == 0U) {
            terminal_write("0");
        } else {
            i = 0;
            while (value > 0U && i < 10) {
                digits[i++] = (char)('0' + (value % 10U));
                value /= 10U;
            }
            while (i > 0) {
                terminal_putchar(digits[--i]);
            }
        }
    }
    terminal_putchar('\n');
    terminal_write("  Buttons    : ");
    terminal_putchar((mouse_buttons & 0x01U) != 0U ? 'L' : '-');
    terminal_putchar((mouse_buttons & 0x02U) != 0U ? 'R' : '-');
    terminal_putchar((mouse_buttons & 0x04U) != 0U ? 'M' : '-');
    terminal_putchar('\n');
    terminal_write("  Queue drop : ");
    {
        char digits[10];
        int i;
        uint32_t value;

        value = mouse_queue_dropped;
        if (value == 0U) {
            terminal_write("0");
        } else {
            i = 0;
            while (value > 0U && i < 10) {
                digits[i++] = (char)('0' + (value % 10U));
                value /= 10U;
            }
            while (i > 0) {
                terminal_putchar(digits[--i]);
            }
        }
    }
    terminal_write("  sync loss ");
    {
        char digits[10];
        int i;
        uint32_t value;

        value = mouse_sync_losses;
        if (value == 0U) {
            terminal_write("0");
        } else {
            i = 0;
            while (value > 0U && i < 10) {
                digits[i++] = (char)('0' + (value % 10U));
                value /= 10U;
            }
            while (i > 0) {
                terminal_putchar(digits[--i]);
            }
        }
    }
    terminal_putchar('\n');
}

uint32_t mouse_irq_count(void) {
    return mouse_irq_events;
}

uint32_t mouse_packet_count(void) {
    return mouse_packets_seen;
}

uint32_t mouse_scroll_up_count(void) {
    return mouse_scroll_up_events;
}

uint32_t mouse_scroll_down_count(void) {
    return mouse_scroll_down_events;
}

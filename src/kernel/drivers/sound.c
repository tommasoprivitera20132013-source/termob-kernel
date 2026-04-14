#include "../include/sound.h"

#include "../include/terminal.h"
#include "../include/timer.h"

#define SPEAKER_PORT 0x61U
#define PIT_CHANNEL0_DATA 0x40U
#define PIT_CHANNEL2_DATA 0x42U
#define PIT_COMMAND 0x43U
#define PIT_BASE_HZ 1193182U
#define SOUND_MIN_FREQUENCY_HZ 37U
#define SOUND_MAX_FREQUENCY_HZ 20000U

static int sound_ready;

static inline void sound_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t sound_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int sound_interrupts_enabled(void) {
    uint32_t eflags;

    __asm__ volatile ("pushf; pop %0" : "=r"(eflags));
    return (eflags & 0x00000200U) != 0U;
}

static void sound_write_u32(uint32_t value) {
    char digits[10];
    int i;

    if (value == 0U) {
        terminal_putchar('0');
        return;
    }

    i = 0;
    while (value > 0U && i < 10) {
        digits[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (i > 0) {
        terminal_putchar(digits[--i]);
    }
}

static void sound_wait_ms(uint32_t duration_ms) {
    uint32_t start_ticks;
    uint32_t wait_ticks;

    wait_ticks = (duration_ms * timer_get_frequency_hz() + 999U) / 1000U;
    if (wait_ticks == 0U) {
        wait_ticks = 1U;
    }

    start_ticks = timer_get_ticks();
    while ((uint32_t)(timer_get_ticks() - start_ticks) < wait_ticks) {
        __asm__ volatile ("" : : : "memory");
    }
}

static uint16_t sound_read_pit_channel0_count(void) {
    uint8_t high;
    uint8_t low;

    sound_outb(PIT_COMMAND, 0x00U);
    low = sound_inb(PIT_CHANNEL0_DATA);
    high = sound_inb(PIT_CHANNEL0_DATA);

    return (uint16_t)(((uint16_t)high << 8) | low);
}

static void sound_wait_noirq_ms(uint32_t duration_ms) {
    uint16_t current_count;
    uint16_t previous_count;
    uint32_t wait_periods;

    if (duration_ms == 0U) {
        return;
    }

    wait_periods = (duration_ms * timer_get_frequency_hz() + 999U) / 1000U;
    if (wait_periods == 0U) {
        wait_periods = 1U;
    }

    previous_count = sound_read_pit_channel0_count();
    while (wait_periods > 0U) {
        current_count = sound_read_pit_channel0_count();
        if (current_count > previous_count) {
            wait_periods--;
        }
        previous_count = current_count;
    }
}

static void sound_play_note(uint32_t frequency_hz, uint32_t duration_ms, uint32_t gap_ms) {
    if (frequency_hz == 0U) {
        sound_stop();
        sound_wait_ms(duration_ms);
    } else {
        sound_beep(frequency_hz, duration_ms);
    }

    if (gap_ms != 0U) {
        sound_wait_ms(gap_ms);
    }
}

static void sound_play_note_noirq(uint32_t frequency_hz, uint32_t duration_ms, uint32_t gap_ms) {
    if (frequency_hz == 0U) {
        sound_stop();
        sound_wait_noirq_ms(duration_ms);
    } else {
        sound_play_tone(frequency_hz);
        sound_wait_noirq_ms(duration_ms);
        sound_stop();
    }

    if (gap_ms != 0U) {
        sound_wait_noirq_ms(gap_ms);
    }
}

static void sound_play_panic_note(uint32_t frequency_hz, uint32_t duration_ms, uint32_t gap_ms) {
    if (sound_interrupts_enabled()) {
        sound_play_note(frequency_hz, duration_ms, gap_ms);
    } else {
        sound_play_note_noirq(frequency_hz, duration_ms, gap_ms);
    }
}

void sound_init(void) {
    sound_ready = 1;
    sound_stop();
}

int sound_is_initialized(void) {
    return sound_ready;
}

void sound_play_tone(uint32_t frequency_hz) {
    uint16_t divisor;
    uint8_t speaker_state;

    if (!sound_ready) {
        return;
    }

    if (frequency_hz < SOUND_MIN_FREQUENCY_HZ || frequency_hz > SOUND_MAX_FREQUENCY_HZ) {
        return;
    }

    divisor = (uint16_t)(PIT_BASE_HZ / frequency_hz);

    sound_outb(PIT_COMMAND, 0xB6);
    sound_outb(PIT_CHANNEL2_DATA, (uint8_t)(divisor & 0xFFU));
    sound_outb(PIT_CHANNEL2_DATA, (uint8_t)((divisor >> 8) & 0xFFU));

    speaker_state = sound_inb(SPEAKER_PORT);
    if ((speaker_state & 0x03U) != 0x03U) {
        sound_outb(SPEAKER_PORT, (uint8_t)(speaker_state | 0x03U));
    }
}

void sound_stop(void) {
    uint8_t speaker_state;

    speaker_state = sound_inb(SPEAKER_PORT);
    sound_outb(SPEAKER_PORT, (uint8_t)(speaker_state & 0xFCU));
}

void sound_beep(uint32_t frequency_hz, uint32_t duration_ms) {
    if (!sound_ready) {
        return;
    }

    sound_play_tone(frequency_hz);
    sound_wait_ms(duration_ms);
    sound_stop();
}

void sound_play_melody(void) {
    if (!sound_ready) {
        return;
    }

    sound_play_note(659U, 180U, 30U);
    sound_play_note(784U, 180U, 30U);
    sound_play_note(988U, 220U, 35U);
    sound_play_note(784U, 180U, 30U);
    sound_play_note(1047U, 260U, 40U);
    sound_play_note(0U, 70U, 0U);
    sound_play_note(988U, 220U, 30U);
    sound_play_note(784U, 260U, 0U);
}

void sound_panic_alert(void) {
    if (!sound_ready) {
        return;
    }

    sound_play_panic_note(988U, 400U, 100U);
    sound_play_panic_note(784U, 350U, 100U);
    sound_play_panic_note(659U, 350U, 100U);
    sound_play_panic_note(523U, 500U, 150U);
    sound_play_panic_note(659U, 350U, 100U);
    sound_play_panic_note(784U, 500U, 0U);

    sound_play_panic_note(988U, 400U, 100U);
    sound_play_panic_note(784U, 350U, 100U);
    sound_play_panic_note(659U, 350U, 100U);
    sound_play_panic_note(523U, 400U, 100U);
    sound_play_panic_note(784U, 100U, 0U);
}

void sound_dump_to_terminal(void) {
    terminal_writeline("Sound subsystem:");
    terminal_write("  State      : ");
    terminal_writeline(sound_ready ? "PC speaker online" : "Offline");
    terminal_writeline("  Backend    : PIT channel 2 + speaker port 0x61");
    terminal_writeline("  Quality    : PC speaker only (monophonic)");
    terminal_write("  Test beep  : ");
    sound_write_u32(880U);
    terminal_writeline(" Hz / 350 ms via `beep`");
    terminal_writeline("  Melody     : test theme via `melody`");
    terminal_writeline("  Host test  : `make run-audio` or `make audio-proof`");
    terminal_writeline("  Upgrade    : use `audio` to inspect PCI audio hardware");
}

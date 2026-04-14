#include "../include/audio.h"

#include "../include/device.h"
#include "../include/heap.h"
#include "../include/klog.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/sound.h"
#include "../include/terminal.h"
#include "../include/timer.h"

#define PCI_CLASS_MULTIMEDIA 0x04U
#define PCI_SUBCLASS_AUDIO_CONTROLLER 0x01U
#define PCI_SUBCLASS_HDA_CONTROLLER 0x03U

#define PCI_COMMAND_REGISTER 0x04U
#define PCI_COMMAND_IO_SPACE 0x0001U
#define PCI_COMMAND_BUS_MASTER 0x0004U
#define PCI_IRQ_LINE_REGISTER 0x3CU
#define PCI_BAR0_REGISTER 0x10U
#define PCI_BAR1_REGISTER 0x14U

#define PCI_VENDOR_INTEL 0x8086U
#define PCI_VENDOR_ENSONIQ 0x1274U
#define PCI_VENDOR_VIRTIO 0x1AF4U

#define PCI_DEVICE_INTEL_ICH_AC97 0x2415U
#define PCI_DEVICE_INTEL_ICH0_AC97 0x2425U

#define AC97_MIXER_RESET 0x00U
#define AC97_MASTER_VOLUME 0x02U
#define AC97_PCM_OUT_VOLUME 0x18U
#define AC97_POWERDOWN_STATUS 0x26U
#define AC97_EXTENDED_AUDIO_ID 0x28U
#define AC97_EXTENDED_AUDIO_STATUS 0x2AU
#define AC97_PCM_FRONT_DAC_RATE 0x2CU

#define AC97_PO_BDBAR 0x10U
#define AC97_PO_CIV 0x14U
#define AC97_PO_LVI 0x15U
#define AC97_PO_SR 0x16U
#define AC97_PO_PICB 0x18U
#define AC97_PO_PIV 0x1AU
#define AC97_PO_CR 0x1BU
#define AC97_GLOB_CNT 0x2CU
#define AC97_GLOB_STA 0x30U

#define AC97_X_CR_RPBM 0x01U
#define AC97_X_CR_RR 0x02U
#define AC97_X_CR_IOCE 0x10U

#define AC97_X_SR_DCH 0x01U
#define AC97_X_SR_CELV 0x02U
#define AC97_X_SR_LVBCI 0x04U
#define AC97_X_SR_BCIS 0x08U
#define AC97_X_SR_FIFOE 0x10U

#define AC97_BD_FLAG_BUP 0x40000000U
#define AC97_BD_FLAG_IOC 0x80000000U

#define AC97_DESCRIPTOR_COUNT 32U
#define AC97_PCM_TONE_HZ 440U
#define AC97_PCM_TONE_MS 300U
#define AC97_PCM_BASE_RATE_HZ 48000U
#define AC97_PCM_TONE_AMPLITUDE 12000

typedef struct {
    uint32_t buffer_pointer;
    uint32_t control_length;
} audio_ac97_bd_t;

static int audio_ready;
static size_t audio_bound_device_count_value;

typedef struct {
    int detected;
    int controller_ready;
    int pcm_ready;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t irq_line;
    uint16_t pci_command;
    uint16_t mixer_base;
    uint16_t busmaster_base;
    uint16_t reset_register;
    uint16_t master_volume;
    uint16_t pcm_out_volume;
    uint16_t powerdown_status;
    uint16_t extended_audio_id;
    uint16_t extended_audio_status;
    uint16_t pcm_front_dac_rate;
    uint32_t global_control;
    uint32_t global_status;
    uint32_t po_bdbar;
    uint16_t po_status;
    uint16_t po_picb;
    uint8_t po_civ;
    uint8_t po_lvi;
    uint8_t po_piv;
    uint8_t po_control;
    uint32_t playback_rate_hz;
    uint32_t playback_duration_ms;
    audio_ac97_bd_t* pcm_out_bdl;
    int16_t* pcm_out_buffer;
    uint32_t pcm_out_samples;
    uint32_t pcm_out_buffer_bytes;
} audio_ac97_state_t;

static audio_ac97_state_t audio_ac97_state;

static void audio_write_u32(uint32_t value) {
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

static void audio_write_hex8(uint8_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";

    terminal_putchar(hex_digits[(value >> 4) & 0x0FU]);
    terminal_putchar(hex_digits[value & 0x0FU]);
}

static void audio_write_hex16(uint16_t value) {
    audio_write_hex8((uint8_t)((value >> 8) & 0xFFU));
    audio_write_hex8((uint8_t)(value & 0xFFU));
}

static void audio_write_hex32(uint32_t value) {
    audio_write_hex16((uint16_t)((value >> 16) & 0xFFFFU));
    audio_write_hex16((uint16_t)(value & 0xFFFFU));
}

static inline uint16_t audio_inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint8_t audio_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t audio_inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void audio_outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void audio_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void audio_outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static int audio_is_known_ac97_device(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id == PCI_VENDOR_INTEL) {
        return device_id == PCI_DEVICE_INTEL_ICH_AC97 ||
               device_id == PCI_DEVICE_INTEL_ICH0_AC97;
    }

    return 0;
}

static int audio_bar_is_io(uint32_t bar_value) {
    return (bar_value & 0x01U) != 0U;
}

static uint16_t audio_bar_io_base(uint32_t bar_value) {
    if (!audio_bar_is_io(bar_value)) {
        return 0U;
    }

    return (uint16_t)(bar_value & 0xFFF0U);
}

static void audio_log_event(const char* message) {
    klog_writeline(message);
    serial_writeline(message);
}

static uint32_t audio_duration_to_wait_ticks(uint32_t duration_ms) {
    uint32_t frequency;
    uint32_t ticks;

    frequency = timer_get_frequency_hz();
    ticks = (duration_ms * frequency + 999U) / 1000U;
    if (ticks == 0U) {
        ticks = 1U;
    }

    return ticks;
}

static void audio_wait_ticks(uint32_t wait_ticks) {
    uint32_t start_ticks;

    start_ticks = timer_get_ticks();
    while ((timer_get_ticks() - start_ticks) < wait_ticks) {
    }
}

static void audio_ac97_refresh_busmaster_state(void) {
    if (!audio_ac97_state.controller_ready) {
        return;
    }

    audio_ac97_state.po_bdbar = audio_inl((uint16_t)(audio_ac97_state.busmaster_base +
                                                     AC97_PO_BDBAR));
    audio_ac97_state.po_civ = audio_inb((uint16_t)(audio_ac97_state.busmaster_base +
                                                   AC97_PO_CIV));
    audio_ac97_state.po_lvi = audio_inb((uint16_t)(audio_ac97_state.busmaster_base +
                                                   AC97_PO_LVI));
    audio_ac97_state.po_status = audio_inw((uint16_t)(audio_ac97_state.busmaster_base +
                                                      AC97_PO_SR));
    audio_ac97_state.po_picb = audio_inw((uint16_t)(audio_ac97_state.busmaster_base +
                                                    AC97_PO_PICB));
    audio_ac97_state.po_piv = audio_inb((uint16_t)(audio_ac97_state.busmaster_base +
                                                   AC97_PO_PIV));
    audio_ac97_state.po_control = audio_inb((uint16_t)(audio_ac97_state.busmaster_base +
                                                       AC97_PO_CR));
    audio_ac97_state.global_control = audio_inl((uint16_t)(audio_ac97_state.busmaster_base +
                                                           AC97_GLOB_CNT));
    audio_ac97_state.global_status = audio_inl((uint16_t)(audio_ac97_state.busmaster_base +
                                                          AC97_GLOB_STA));
}

static void audio_ac97_generate_triangle_tone(int16_t* buffer,
                                              uint32_t frame_count,
                                              uint32_t sample_rate_hz,
                                              uint32_t tone_hz) {
    uint32_t frame_index;
    uint32_t period_frames;
    uint32_t half_period;

    if (buffer == 0 || frame_count == 0U || sample_rate_hz == 0U || tone_hz == 0U) {
        return;
    }

    period_frames = sample_rate_hz / tone_hz;
    if (period_frames < 2U) {
        period_frames = 2U;
    }
    half_period = period_frames / 2U;
    if (half_period == 0U) {
        half_period = 1U;
    }

    for (frame_index = 0U; frame_index < frame_count; frame_index++) {
        uint32_t position;
        int32_t sample;

        position = frame_index % period_frames;
        if (position < half_period) {
            sample = -(int32_t)AC97_PCM_TONE_AMPLITUDE +
                     ((int32_t)(position * (uint32_t)(AC97_PCM_TONE_AMPLITUDE * 4)) /
                      (int32_t)period_frames);
        } else {
            sample = (int32_t)(AC97_PCM_TONE_AMPLITUDE * 3) -
                     ((int32_t)(position * (uint32_t)(AC97_PCM_TONE_AMPLITUDE * 4)) /
                      (int32_t)period_frames);
        }

        if (sample > AC97_PCM_TONE_AMPLITUDE) {
            sample = AC97_PCM_TONE_AMPLITUDE;
        }
        if (sample < -AC97_PCM_TONE_AMPLITUDE) {
            sample = -AC97_PCM_TONE_AMPLITUDE;
        }

        buffer[frame_index * 2U] = (int16_t)sample;
        buffer[frame_index * 2U + 1U] = (int16_t)sample;
    }
}

static int audio_ac97_prepare_pcm_buffer(void) {
    uint32_t frame_count;
    uint32_t sample_count;
    uint32_t buffer_bytes;

    if (audio_ac97_state.pcm_out_bdl != 0 && audio_ac97_state.pcm_out_buffer != 0) {
        return 1;
    }

    frame_count = (AC97_PCM_BASE_RATE_HZ * AC97_PCM_TONE_MS) / 1000U;
    if (frame_count == 0U) {
        return 0;
    }

    sample_count = frame_count * 2U;
    buffer_bytes = frame_count * sizeof(int16_t) * 2U;

    audio_ac97_state.pcm_out_bdl =
        (audio_ac97_bd_t*)kcalloc(AC97_DESCRIPTOR_COUNT, sizeof(audio_ac97_bd_t));
    audio_ac97_state.pcm_out_buffer = (int16_t*)kmalloc(buffer_bytes);
    if (audio_ac97_state.pcm_out_bdl == 0 || audio_ac97_state.pcm_out_buffer == 0) {
        return 0;
    }

    audio_ac97_state.pcm_out_samples = sample_count;
    audio_ac97_state.pcm_out_buffer_bytes = buffer_bytes;
    audio_ac97_state.playback_rate_hz = AC97_PCM_BASE_RATE_HZ;
    audio_ac97_state.playback_duration_ms = AC97_PCM_TONE_MS;

    audio_ac97_generate_triangle_tone(audio_ac97_state.pcm_out_buffer,
                                      frame_count,
                                      audio_ac97_state.playback_rate_hz,
                                      AC97_PCM_TONE_HZ);

    audio_ac97_state.pcm_out_bdl[0].buffer_pointer =
        (uint32_t)(uintptr_t)audio_ac97_state.pcm_out_buffer;
    audio_ac97_state.pcm_out_bdl[0].control_length =
        AC97_BD_FLAG_IOC | AC97_BD_FLAG_BUP | (audio_ac97_state.pcm_out_samples & 0xFFFFU);

    return 1;
}

static void audio_ac97_select_sample_rate(void) {
    uint16_t ext_status;

    if ((audio_ac97_state.extended_audio_id & 0x0001U) == 0U) {
        audio_outw((uint16_t)(audio_ac97_state.mixer_base + AC97_PCM_FRONT_DAC_RATE),
                   AC97_PCM_BASE_RATE_HZ);
        audio_ac97_state.playback_rate_hz = AC97_PCM_BASE_RATE_HZ;
        return;
    }

    ext_status = audio_inw((uint16_t)(audio_ac97_state.mixer_base + AC97_EXTENDED_AUDIO_STATUS));
    ext_status |= 0x0001U;
    audio_outw((uint16_t)(audio_ac97_state.mixer_base + AC97_EXTENDED_AUDIO_STATUS), ext_status);
    audio_outw((uint16_t)(audio_ac97_state.mixer_base + AC97_PCM_FRONT_DAC_RATE),
               AC97_PCM_BASE_RATE_HZ);
    audio_ac97_state.playback_rate_hz = AC97_PCM_BASE_RATE_HZ;
}

static void audio_ac97_reset_pcm_out(void) {
    uint16_t status_bits;

    if (!audio_ac97_state.controller_ready) {
        return;
    }

    audio_outb((uint16_t)(audio_ac97_state.busmaster_base + AC97_PO_CR), 0U);
    audio_outb((uint16_t)(audio_ac97_state.busmaster_base + AC97_PO_CR), AC97_X_CR_RR);
    audio_wait_ticks(1U);
    audio_outb((uint16_t)(audio_ac97_state.busmaster_base + AC97_PO_CR), 0U);

    status_bits = (uint16_t)(AC97_X_SR_FIFOE | AC97_X_SR_BCIS | AC97_X_SR_LVBCI);
    audio_outw((uint16_t)(audio_ac97_state.busmaster_base + AC97_PO_SR), status_bits);
}

static void audio_ac97_start_pcm_out(void) {
    audio_outl((uint16_t)(audio_ac97_state.busmaster_base + AC97_PO_BDBAR),
               (uint32_t)(uintptr_t)audio_ac97_state.pcm_out_bdl);
    audio_outb((uint16_t)(audio_ac97_state.busmaster_base + AC97_PO_LVI), 0U);
    audio_outb((uint16_t)(audio_ac97_state.busmaster_base + AC97_PO_CR),
               (uint8_t)(AC97_X_CR_IOCE | AC97_X_CR_RPBM));
}

static void audio_ac97_wait_for_completion(void) {
    uint32_t wait_ticks;
    uint32_t guard_start;

    wait_ticks = audio_duration_to_wait_ticks(audio_ac97_state.playback_duration_ms + 60U);
    audio_wait_ticks(wait_ticks);

    guard_start = timer_get_ticks();
    while ((timer_get_ticks() - guard_start) < audio_duration_to_wait_ticks(120U)) {
        uint16_t status_bits;

        status_bits = audio_inw((uint16_t)(audio_ac97_state.busmaster_base + AC97_PO_SR));
        if ((status_bits & (AC97_X_SR_BCIS | AC97_X_SR_LVBCI | AC97_X_SR_DCH)) != 0U) {
            break;
        }
    }
}

static void audio_ac97_refresh_state(void) {
    if (!audio_ac97_state.controller_ready) {
        return;
    }

    audio_ac97_state.reset_register = audio_inw((uint16_t)(audio_ac97_state.mixer_base +
                                                           AC97_MIXER_RESET));
    audio_ac97_state.master_volume = audio_inw((uint16_t)(audio_ac97_state.mixer_base +
                                                          AC97_MASTER_VOLUME));
    audio_ac97_state.pcm_out_volume = audio_inw((uint16_t)(audio_ac97_state.mixer_base +
                                                           AC97_PCM_OUT_VOLUME));
    audio_ac97_state.powerdown_status = audio_inw((uint16_t)(audio_ac97_state.mixer_base +
                                                             AC97_POWERDOWN_STATUS));
    audio_ac97_state.extended_audio_id = audio_inw((uint16_t)(audio_ac97_state.mixer_base +
                                                              AC97_EXTENDED_AUDIO_ID));
    audio_ac97_state.extended_audio_status = audio_inw((uint16_t)(audio_ac97_state.mixer_base +
                                                                  AC97_EXTENDED_AUDIO_STATUS));
    audio_ac97_state.pcm_front_dac_rate = audio_inw((uint16_t)(audio_ac97_state.mixer_base +
                                                               AC97_PCM_FRONT_DAC_RATE));
    audio_ac97_refresh_busmaster_state();
}

static void audio_ac97_try_init(const kernel_device_t* device) {
    uint16_t command_value;
    uint32_t bar0_value;
    uint32_t bar1_value;

    if (device == 0 || audio_ac97_state.detected) {
        return;
    }

    if (!audio_is_known_ac97_device(device->vendor_id, device->device_id)) {
        return;
    }

    audio_ac97_state.detected = 1;
    audio_ac97_state.vendor_id = device->vendor_id;
    audio_ac97_state.device_id = device->device_id;
    audio_ac97_state.bus = device->bus;
    audio_ac97_state.slot = device->slot;
    audio_ac97_state.function = device->function;
    audio_ac97_state.irq_line = pci_config_read8(device->bus,
                                                 device->slot,
                                                 device->function,
                                                 PCI_IRQ_LINE_REGISTER);

    command_value = pci_config_read16(device->bus,
                                      device->slot,
                                      device->function,
                                      PCI_COMMAND_REGISTER);
    command_value |= (uint16_t)(PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER);
    pci_config_write16(device->bus, device->slot, device->function, PCI_COMMAND_REGISTER, command_value);
    audio_ac97_state.pci_command = pci_config_read16(device->bus,
                                                     device->slot,
                                                     device->function,
                                                     PCI_COMMAND_REGISTER);

    bar0_value = pci_config_read32(device->bus, device->slot, device->function, PCI_BAR0_REGISTER);
    bar1_value = pci_config_read32(device->bus, device->slot, device->function, PCI_BAR1_REGISTER);
    audio_ac97_state.mixer_base = audio_bar_io_base(bar0_value);
    audio_ac97_state.busmaster_base = audio_bar_io_base(bar1_value);

    if (audio_ac97_state.mixer_base == 0U || audio_ac97_state.busmaster_base == 0U) {
        audio_log_event("TERMOB: ac97 controller detected but io bars are unavailable");
        return;
    }

    audio_ac97_state.controller_ready = 1;
    audio_ac97_refresh_state();

    /* Keep the codec output path unmuted for future PCM work. */
    audio_outw((uint16_t)(audio_ac97_state.mixer_base + AC97_MASTER_VOLUME), 0x0000U);
    audio_outw((uint16_t)(audio_ac97_state.mixer_base + AC97_PCM_OUT_VOLUME), 0x0000U);
    audio_ac97_select_sample_rate();
    audio_ac97_state.pcm_ready = audio_ac97_prepare_pcm_buffer();
    audio_ac97_refresh_state();

    audio_log_event("TERMOB: ac97 controller ready");
}

static int audio_pci_match(const kernel_device_t* device) {
    if (device == 0 || device->bus_type != TERMOB_BUS_PCI) {
        return 0;
    }

    if (device->class_code != PCI_CLASS_MULTIMEDIA) {
        return 0;
    }

    if (device->subclass == PCI_SUBCLASS_AUDIO_CONTROLLER ||
        device->subclass == PCI_SUBCLASS_HDA_CONTROLLER) {
        return 1;
    }

    return 0;
}

static void audio_pci_probe(kernel_device_t* device) {
    audio_bound_device_count_value++;
    klog_writeline("TERMOB: pci audio device bound");
    serial_writeline("TERMOB: pci audio device bound");
    audio_ac97_try_init(device);
}

const char* audio_device_name(uint16_t vendor_id, uint16_t device_id, uint8_t subclass) {
    if (vendor_id == PCI_VENDOR_INTEL) {
        if (device_id == 0x2415U || device_id == 0x2425U) {
            return "Intel AC'97";
        }

        if (device_id == 0x2668U || device_id == 0x27D8U || device_id == 0x293EU) {
            return "Intel HD Audio";
        }
    }

    if (vendor_id == PCI_VENDOR_ENSONIQ && device_id == 0x1371U) {
        return "Ensoniq ES1370";
    }

    if (vendor_id == PCI_VENDOR_VIRTIO) {
        if (device_id == 0x1054U) {
            return "virtio-snd";
        }

        return "virtio audio";
    }

    if (subclass == PCI_SUBCLASS_HDA_CONTROLLER) {
        return "HD Audio controller";
    }

    return "PCI audio controller";
}

void audio_init(void) {
    static const kernel_driver_t audio_pci_driver = {
        "audio-pci",
        TERMOB_BUS_PCI,
        audio_pci_match,
        audio_pci_probe
    };

    if (audio_ready) {
        return;
    }

    audio_bound_device_count_value = 0U;
    audio_ac97_state.detected = 0;
    audio_ac97_state.controller_ready = 0;
    audio_ac97_state.pcm_ready = 0;
    audio_ac97_state.vendor_id = 0U;
    audio_ac97_state.device_id = 0U;
    audio_ac97_state.bus = 0U;
    audio_ac97_state.slot = 0U;
    audio_ac97_state.function = 0U;
    audio_ac97_state.irq_line = 0U;
    audio_ac97_state.pci_command = 0U;
    audio_ac97_state.mixer_base = 0U;
    audio_ac97_state.busmaster_base = 0U;
    audio_ac97_state.reset_register = 0U;
    audio_ac97_state.master_volume = 0U;
    audio_ac97_state.pcm_out_volume = 0U;
    audio_ac97_state.powerdown_status = 0U;
    audio_ac97_state.extended_audio_id = 0U;
    audio_ac97_state.extended_audio_status = 0U;
    audio_ac97_state.pcm_front_dac_rate = 0U;
    audio_ac97_state.global_control = 0U;
    audio_ac97_state.global_status = 0U;
    audio_ac97_state.po_bdbar = 0U;
    audio_ac97_state.po_status = 0U;
    audio_ac97_state.po_picb = 0U;
    audio_ac97_state.po_civ = 0U;
    audio_ac97_state.po_lvi = 0U;
    audio_ac97_state.po_piv = 0U;
    audio_ac97_state.po_control = 0U;
    audio_ac97_state.playback_rate_hz = 0U;
    audio_ac97_state.playback_duration_ms = 0U;
    audio_ac97_state.pcm_out_bdl = 0;
    audio_ac97_state.pcm_out_buffer = 0;
    audio_ac97_state.pcm_out_samples = 0U;
    audio_ac97_state.pcm_out_buffer_bytes = 0U;
    audio_ready = device_register_driver(&audio_pci_driver);
}

int audio_is_initialized(void) {
    return audio_ready;
}

size_t audio_bound_device_count(void) {
    return audio_bound_device_count_value;
}

int audio_ac97_is_ready(void) {
    return audio_ac97_state.controller_ready;
}

int audio_ac97_play_test_tone(void) {
    if (!audio_ac97_state.controller_ready) {
        return 0;
    }

    if (!audio_ac97_state.pcm_ready) {
        audio_ac97_state.pcm_ready = audio_ac97_prepare_pcm_buffer();
        if (!audio_ac97_state.pcm_ready) {
            audio_log_event("TERMOB: ac97 pcm buffer allocation failed");
            return 0;
        }
    }

    audio_ac97_generate_triangle_tone(audio_ac97_state.pcm_out_buffer,
                                      audio_ac97_state.pcm_out_samples / 2U,
                                      audio_ac97_state.playback_rate_hz,
                                      AC97_PCM_TONE_HZ);
    audio_ac97_select_sample_rate();
    audio_ac97_reset_pcm_out();
    audio_ac97_start_pcm_out();
    audio_ac97_wait_for_completion();
    audio_ac97_reset_pcm_out();
    audio_ac97_refresh_state();
    audio_log_event("TERMOB: ac97 pcm test tone played");
    return 1;
}

void audio_dump_to_terminal(void) {
    kernel_device_t device;
    size_t device_index;
    size_t detected_devices;

    terminal_writeline("Audio subsystem:");
    terminal_write("  PCI driver : ");
    terminal_writeline(audio_ready ? "Registered" : "Offline");
    terminal_write("  Speaker    : ");
    terminal_writeline(sound_is_initialized() ? "PC speaker online" : "offline");
    terminal_write("  AC97       : ");
    terminal_writeline(audio_ac97_state.controller_ready ? "controller ready"
                                                         : (audio_ac97_state.detected
                                                            ? "detected, io not ready"
                                                            : "not detected"));
    terminal_write("  PCM DMA    : ");
    terminal_writeline(audio_ac97_state.pcm_ready ? "test tone armed" : "offline");
    terminal_write("  PCI bound  : ");
    audio_write_u32((uint32_t)audio_bound_device_count_value);
    terminal_writeline(" devices");

    detected_devices = 0U;
    for (device_index = 0U; device_at(device_index, &device); device_index++) {
        if (!audio_pci_match(&device)) {
            continue;
        }

        detected_devices++;
    }

    terminal_write("  PCI seen   : ");
    audio_write_u32((uint32_t)detected_devices);
    terminal_writeline(" devices");
    terminal_writeline("  Quality    : PC speaker live + AC'97 PCM test tone");
    terminal_writeline("  Test cmd   : ac97tone");

    if (detected_devices == 0U) {
        terminal_writeline("  No PCI audio controller detected");
        return;
    }

    terminal_writeline("Detected PCI audio hardware:");
    for (device_index = 0U; device_at(device_index, &device); device_index++) {
        if (!audio_pci_match(&device)) {
            continue;
        }

        terminal_write("  ");
        audio_write_hex8(device.bus);
        terminal_putchar(':');
        audio_write_hex8(device.slot);
        terminal_putchar('.');
        audio_write_hex8(device.function);
        terminal_write("  ");
        terminal_write(audio_device_name(device.vendor_id, device.device_id, device.subclass));
        terminal_write("  ");
        audio_write_hex16(device.vendor_id);
        terminal_putchar(':');
        audio_write_hex16(device.device_id);

        if (device.bound_driver_name != 0) {
            terminal_write("  -> ");
            terminal_write(device.bound_driver_name);
        }

        terminal_putchar('\n');
    }
}

void audio_dump_ac97_to_terminal(void) {
    terminal_writeline("AC'97 controller:");

    if (!audio_ac97_state.detected) {
        terminal_writeline("  State      : No supported AC'97 controller detected");
        terminal_writeline("  Tip        : start the kernel with make run-ac97");
        return;
    }

    terminal_write("  Controller : ");
    terminal_writeline(audio_device_name(audio_ac97_state.vendor_id,
                                         audio_ac97_state.device_id,
                                         PCI_SUBCLASS_AUDIO_CONTROLLER));
    terminal_write("  PCI        : ");
    audio_write_hex8(audio_ac97_state.bus);
    terminal_putchar(':');
    audio_write_hex8(audio_ac97_state.slot);
    terminal_putchar('.');
    audio_write_hex8(audio_ac97_state.function);
    terminal_write("  ");
    audio_write_hex16(audio_ac97_state.vendor_id);
    terminal_putchar(':');
    audio_write_hex16(audio_ac97_state.device_id);
    terminal_putchar('\n');

    terminal_write("  State      : ");
    terminal_writeline(audio_ac97_state.controller_ready ? "Ready" : "Detected, not ready");
    terminal_write("  IRQ line   : ");
    audio_write_u32(audio_ac97_state.irq_line);
    terminal_putchar('\n');
    terminal_write("  PCI cmd    : ");
    audio_write_hex16(audio_ac97_state.pci_command);
    terminal_putchar('\n');
    terminal_write("  Mixer BAR  : ");
    audio_write_hex16(audio_ac97_state.mixer_base);
    terminal_putchar('\n');
    terminal_write("  BM BAR     : ");
    audio_write_hex16(audio_ac97_state.busmaster_base);
    terminal_putchar('\n');

    if (!audio_ac97_state.controller_ready) {
        return;
    }

    audio_ac97_refresh_state();

    terminal_write("  Codec rst  : ");
    audio_write_hex16(audio_ac97_state.reset_register);
    terminal_putchar('\n');
    terminal_write("  Master vol : ");
    audio_write_hex16(audio_ac97_state.master_volume);
    terminal_putchar('\n');
    terminal_write("  PCM vol    : ");
    audio_write_hex16(audio_ac97_state.pcm_out_volume);
    terminal_putchar('\n');
    terminal_write("  Power      : ");
    audio_write_hex16(audio_ac97_state.powerdown_status);
    terminal_putchar('\n');
    terminal_write("  Ext ID     : ");
    audio_write_hex16(audio_ac97_state.extended_audio_id);
    terminal_putchar('\n');
    terminal_write("  Ext stat   : ");
    audio_write_hex16(audio_ac97_state.extended_audio_status);
    terminal_putchar('\n');
    terminal_write("  DAC rate   : ");
    audio_write_u32(audio_ac97_state.pcm_front_dac_rate);
    terminal_writeline(" Hz");
    terminal_write("  GLOB CNT   : ");
    audio_write_hex32(audio_ac97_state.global_control);
    terminal_putchar('\n');
    terminal_write("  GLOB STA   : ");
    audio_write_hex32(audio_ac97_state.global_status);
    terminal_putchar('\n');
    terminal_write("  PO BDBAR   : ");
    audio_write_hex32(audio_ac97_state.po_bdbar);
    terminal_putchar('\n');
    terminal_write("  PO CIV/LVI : ");
    audio_write_hex8(audio_ac97_state.po_civ);
    terminal_write(" / ");
    audio_write_hex8(audio_ac97_state.po_lvi);
    terminal_putchar('\n');
    terminal_write("  PO PICB    : ");
    audio_write_u32(audio_ac97_state.po_picb);
    terminal_putchar('\n');
    terminal_write("  PO PIV/CR  : ");
    audio_write_hex8(audio_ac97_state.po_piv);
    terminal_write(" / ");
    audio_write_hex8(audio_ac97_state.po_control);
    terminal_putchar('\n');
    terminal_write("  PO SR      : ");
    audio_write_hex16(audio_ac97_state.po_status);
    terminal_putchar('\n');
    terminal_write("  PCM tone   : ");
    terminal_writeline(audio_ac97_state.pcm_ready ? "ready via ac97tone" : "buffer offline");
}

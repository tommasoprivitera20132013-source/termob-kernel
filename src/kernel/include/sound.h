#ifndef TERMOB_SOUND_H
#define TERMOB_SOUND_H

#include <stdint.h>

void sound_init(void);
int sound_is_initialized(void);
void sound_play_tone(uint32_t frequency_hz);
void sound_stop(void);
void sound_beep(uint32_t frequency_hz, uint32_t duration_ms);
void sound_play_melody(void);
void sound_panic_alert(void);
void sound_dump_to_terminal(void);

#endif

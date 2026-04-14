#ifndef TERMOB_AUDIO_H
#define TERMOB_AUDIO_H

#include <stddef.h>
#include <stdint.h>

void audio_init(void);
int audio_is_initialized(void);
size_t audio_bound_device_count(void);
int audio_ac97_is_ready(void);
int audio_ac97_play_test_tone(void);
const char* audio_device_name(uint16_t vendor_id, uint16_t device_id, uint8_t subclass);
void audio_dump_to_terminal(void);
void audio_dump_ac97_to_terminal(void);

#endif

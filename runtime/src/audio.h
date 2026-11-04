#ifndef VCOS_AUDIO_H
#define VCOS_AUDIO_H

#include <stdint.h>

#define VCOS_AUDIO_RATE 44100
#define VCOS_AUDIO_FRAME_SAMPLES 735

int audio_open(const char *spec);
int audio_open_ex(const char *spec, int sample_rate, int channels);
void audio_write(const int16_t *samples, int frames);
void audio_close(void);
int audio_active(void);

#endif

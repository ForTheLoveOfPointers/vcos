#include "audio.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

enum {
    AUDIO_NONE = 0,
    AUDIO_WAV = 1,
    AUDIO_ALSA = 2
};

static int audio_mode = AUDIO_NONE;
static int audio_rate = VCOS_AUDIO_RATE;
static int audio_channels = 1;
static FILE *wav_file;
static long wav_bytes;
static void *alsa_lib;
static void *alsa_pcm;

typedef struct snd_pcm snd_pcm_t;
typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;

typedef int (*alsa_open_fn)(snd_pcm_t **, const char *, int, int);
typedef int (*alsa_set_params_fn)(snd_pcm_t *, int, int, unsigned int,
                                  unsigned int, int, unsigned int);
typedef snd_pcm_sframes_t (*alsa_writei_fn)(snd_pcm_t *, const void *,
                                            snd_pcm_uframes_t);
typedef int (*alsa_recover_fn)(snd_pcm_t *, int, int);
typedef int (*alsa_close_fn)(snd_pcm_t *);
typedef int (*alsa_drain_fn)(snd_pcm_t *);

static alsa_open_fn alsa_open;
static alsa_set_params_fn alsa_set_params;
static alsa_writei_fn alsa_writei;
static alsa_recover_fn alsa_recover;
static alsa_close_fn alsa_close;
static alsa_drain_fn alsa_drain;

static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_wav_header(FILE *f, uint32_t data_bytes)
{
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    put_u32le(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_u32le(h + 16, 16);
    put_u16le(h + 20, 1);
    put_u16le(h + 22, 1);
    put_u32le(h + 24, (uint32_t)audio_rate);
    put_u32le(h + 28, (uint32_t)(audio_rate * audio_channels * 2));
    put_u16le(h + 32, (uint16_t)audio_channels);
    put_u16le(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put_u32le(h + 40, data_bytes);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), f);
}

static int open_alsa(void)
{
    alsa_lib = dlopen("libasound.so.2", RTLD_NOW);
    if (!alsa_lib) {
        return -1;
    }
    alsa_open = (alsa_open_fn)dlsym(alsa_lib, "snd_pcm_open");
    alsa_set_params = (alsa_set_params_fn)dlsym(alsa_lib, "snd_pcm_set_params");
    alsa_writei = (alsa_writei_fn)dlsym(alsa_lib, "snd_pcm_writei");
    alsa_recover = (alsa_recover_fn)dlsym(alsa_lib, "snd_pcm_recover");
    alsa_close = (alsa_close_fn)dlsym(alsa_lib, "snd_pcm_close");
    alsa_drain = (alsa_drain_fn)dlsym(alsa_lib, "snd_pcm_drain");
    if (!alsa_open || !alsa_set_params || !alsa_writei || !alsa_close) {
        dlclose(alsa_lib);
        alsa_lib = NULL;
        return -1;
    }
    snd_pcm_t *pcm = NULL;
    if (alsa_open(&pcm, "default", 0, 0) < 0) {
        dlclose(alsa_lib);
        alsa_lib = NULL;
        return -1;
    }
    if (alsa_set_params(pcm, 2, 3, (unsigned int)audio_channels,
                        (unsigned int)audio_rate, 1, 100000) < 0) {
        if (alsa_close) {
            alsa_close(pcm);
        }
        dlclose(alsa_lib);
        alsa_lib = NULL;
        return -1;
    }
    alsa_pcm = pcm;
    return 0;
}

int audio_open(const char *spec)
{
    return audio_open_ex(spec, VCOS_AUDIO_RATE, 1);
}

int audio_open_ex(const char *spec, int sample_rate, int channels)
{
    audio_mode = AUDIO_NONE;
    audio_rate = sample_rate > 0 ? sample_rate : VCOS_AUDIO_RATE;
    audio_channels = channels == 2 ? 2 : 1;
    if (!spec || strcmp(spec, "none") == 0) {
        return 0;
    }
    if (strcmp(spec, "auto") == 0) {
        if (access("/dev/snd/controlC0", F_OK) == 0 && open_alsa() == 0) {
            audio_mode = AUDIO_ALSA;
            vcos_log("audio: alsa output");
            return 0;
        }
        return 0;
    }
    if (strncmp(spec, "wav:", 4) == 0 && spec[4] != '\0') {
        wav_file = fopen(spec + 4, "wb");
        if (!wav_file) {
            vcos_error("audio: cannot write %s", spec + 4);
            return -1;
        }
        write_wav_header(wav_file, 0);
        wav_bytes = 0;
        audio_mode = AUDIO_WAV;
        vcos_log("audio: writing %s", spec + 4);
        return 0;
    }
    if (strcmp(spec, "alsa") == 0) {
        if (open_alsa() != 0) {
            vcos_error("audio: ALSA default device unavailable");
            return -1;
        }
        audio_mode = AUDIO_ALSA;
        vcos_log("audio: alsa output");
        return 0;
    }
    vcos_error("audio: unknown sink %s", spec);
    return -1;
}

void audio_write(const int16_t *samples, int frames)
{
    if (audio_mode == AUDIO_NONE || !samples || frames <= 0) {
        return;
    }
    if (audio_mode == AUDIO_WAV) {
        size_t count = (size_t)frames * (size_t)audio_channels;
        size_t n = fwrite(samples, sizeof(int16_t), count, wav_file);
        wav_bytes += (long)(n * sizeof(int16_t));
        return;
    }
    if (audio_mode == AUDIO_ALSA && alsa_pcm) {
        const int16_t *p = samples;
        int left = frames;
        while (left > 0) {
            snd_pcm_sframes_t n = alsa_writei((snd_pcm_t *)alsa_pcm, p,
                                              (snd_pcm_uframes_t)left);
            if (n < 0) {
                if (alsa_recover &&
                    alsa_recover((snd_pcm_t *)alsa_pcm, (int)n, 1) < 0) {
                    return;
                }
                continue;
            }
            p += n;
            left -= (int)n;
        }
    }
}

int audio_active(void)
{
    return audio_mode != AUDIO_NONE;
}

void audio_close(void)
{
    if (audio_mode == AUDIO_WAV && wav_file) {
        write_wav_header(wav_file, (uint32_t)wav_bytes);
        fclose(wav_file);
        wav_file = NULL;
    }
    if (audio_mode == AUDIO_ALSA && alsa_pcm) {
        if (alsa_drain) {
            alsa_drain((snd_pcm_t *)alsa_pcm);
        }
        if (alsa_close) {
            alsa_close((snd_pcm_t *)alsa_pcm);
        }
        alsa_pcm = NULL;
    }
    if (alsa_lib) {
        dlclose(alsa_lib);
        alsa_lib = NULL;
    }
    audio_mode = AUDIO_NONE;
}

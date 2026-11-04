#ifndef VCOS_BACKEND_H
#define VCOS_BACKEND_H

#include <stdint.h>

#include "input.h"

typedef struct backend {
    const char *name;
    int (*init)(int scale, const char *title);
    void (*present)(const uint32_t *fb, int width, int height);
    int (*poll)(input *in, int *quit);
    void (*shutdown)(void);
} backend;

extern const backend backend_x11;
extern const backend backend_drm;
extern const backend backend_fb;
extern const backend backend_ppm;
extern const backend backend_null;

void backend_ppm_set_output(const char *path);
void backend_ppm_set_frame(int frame);
void backend_ppm_reset(void);
void write_ppm(const char *path, const uint32_t *fb, int width, int height);

#endif

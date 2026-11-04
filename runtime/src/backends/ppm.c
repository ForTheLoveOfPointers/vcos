#include "../backend.h"

#include <stdio.h>
#include <stdlib.h>

static const char *ppm_path;
static int ppm_frame;

void backend_ppm_set_output(const char *path)
{
    ppm_path = path;
}

void backend_ppm_set_frame(int frame)
{
    ppm_frame = frame;
}

void backend_ppm_reset(void)
{
    ppm_path = NULL;
    ppm_frame = 0;
}

void write_ppm(const char *path, const uint32_t *fb, int width, int height)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; i++) {
        uint32_t c = fb[i];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)((c >> 16) & 0xFF);
        rgb[1] = (unsigned char)((c >> 8) & 0xFF);
        rgb[2] = (unsigned char)(c & 0xFF);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static int ppm_init(int scale, const char *title)
{
    (void)scale;
    (void)title;
    if (!ppm_path) {
        ppm_path = "vcos.ppm";
    }
    return 0;
}

static void ppm_present(const uint32_t *fb, int width, int height)
{
    write_ppm(ppm_path, fb, width, height);
    ppm_frame++;
}

static int ppm_poll(input *in, int *quit)
{
    (void)in;
    (void)quit;
    return 0;
}

static void ppm_shutdown(void) {}

const backend backend_ppm = {
    "ppm",
    ppm_init,
    ppm_present,
    ppm_poll,
    ppm_shutdown,
};

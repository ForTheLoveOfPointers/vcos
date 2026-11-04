#include "../backend.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "../log.h"

#ifdef __linux__

static int fb_fd = -1;
static uint8_t *fb_mem;
static size_t fb_len;
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static int fb_scale = 1;
static int fb_xoff;
static int fb_yoff;

static int fb_init(int scale, const char *title)
{
    (void)title;
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        vcos_error("fbdev: cannot open /dev/fb0");
        return -1;
    }
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0) {
        vcos_error("fbdev: cannot query framebuffer");
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }
    if (var.bits_per_pixel != 16 && var.bits_per_pixel != 24 &&
        var.bits_per_pixel != 32) {
        vcos_error("fbdev: unsupported %u bpp", var.bits_per_pixel);
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }

    int max_scale = var.xres / 256;
    int max_scale_y = var.yres / 240;
    if (max_scale_y < max_scale) {
        max_scale = max_scale_y;
    }
    if (scale <= 0 || scale > max_scale) {
        scale = max_scale > 0 ? max_scale : 1;
    }
    fb_scale = scale;
    fb_xoff = (int)(var.xres - 256 * scale) / 2;
    fb_yoff = (int)(var.yres - 240 * scale) / 2;

    fb_len = (size_t)fix.line_length * (size_t)var.yres_virtual;
    fb_mem = mmap(NULL, fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        vcos_error("fbdev: mmap failed");
        close(fb_fd);
        fb_fd = -1;
        fb_mem = NULL;
        return -1;
    }
    vcos_log("fbdev: %ux%u %u bpp scale %d", var.xres, var.yres,
             var.bits_per_pixel, scale);
    return 0;
}

static void put_pixel(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= (int)var.xres || y >= (int)var.yres) {
        return;
    }
    size_t bytes = var.bits_per_pixel / 8;
    uint8_t *p = fb_mem + (size_t)fix.line_length * (size_t)y + (size_t)x * bytes;
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    if (var.bits_per_pixel == 16) {
        uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        memcpy(p, &v, 2);
    } else if (var.bits_per_pixel == 24) {
        p[0] = b;
        p[1] = g;
        p[2] = r;
    } else {
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = 0;
    }
}

static void fb_present(const uint32_t *fb, int width, int height)
{
    for (int y = 0; y < height * fb_scale; y++) {
        for (int x = 0; x < width * fb_scale; x++) {
            uint32_t color = fb[(y / fb_scale) * width + (x / fb_scale)];
            put_pixel(fb_xoff + x, fb_yoff + y, color);
        }
    }
}

static int fb_poll(input *in, int *quit)
{
    (void)in;
    (void)quit;
    return 0;
}

static void fb_shutdown(void)
{
    if (fb_mem) {
        munmap(fb_mem, fb_len);
        fb_mem = NULL;
    }
    if (fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
}

const backend backend_fb = {
    "fb",
    fb_init,
    fb_present,
    fb_poll,
    fb_shutdown,
};

#else

static int fb_unavailable(int scale, const char *title)
{
    (void)scale;
    (void)title;
    return -1;
}

static void no_present(const uint32_t *fb, int width, int height)
{
    (void)fb;
    (void)width;
    (void)height;
}

static int no_poll(input *in, int *quit)
{
    (void)in;
    (void)quit;
    return 0;
}

static void no_shutdown(void) {}

const backend backend_fb = {
    "fb",
    fb_unavailable,
    no_present,
    no_poll,
    no_shutdown,
};

#endif

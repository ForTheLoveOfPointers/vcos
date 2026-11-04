#include "../backend.h"

static int null_init(int scale, const char *title)
{
    (void)scale;
    (void)title;
    return 0;
}

static void null_present(const uint32_t *fb, int width, int height)
{
    (void)fb;
    (void)width;
    (void)height;
}

static int null_poll(input *in, int *quit)
{
    (void)in;
    (void)quit;
    return 0;
}

static void null_shutdown(void) {}

const backend backend_null = {
    "null",
    null_init,
    null_present,
    null_poll,
    null_shutdown,
};

#include "../backend.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../log.h"

#ifdef VCOS_WITH_X11

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

static Display *dpy;
static Window win;
static GC gc;
static XImage *img;
static uint32_t *scaled;
static int win_w;
static int win_h;
static int scale_factor = 1;
static Atom wm_delete;
static int quit_flag;

static uint8_t map_key(KeySym ks, int *player, int *quit)
{
    *player = 0;
    switch (ks) {
    case XK_Up:
        return VCOS_BTN_UP;
    case XK_Down:
        return VCOS_BTN_DOWN;
    case XK_Left:
        return VCOS_BTN_LEFT;
    case XK_Right:
        return VCOS_BTN_RIGHT;
    case XK_z:
    case XK_Z:
    case XK_space:
        return VCOS_BTN_A;
    case XK_x:
    case XK_X:
        return VCOS_BTN_B;
    case XK_Return:
    case XK_KP_Enter:
        return VCOS_BTN_START;
    case XK_Shift_R:
    case XK_Shift_L:
        return VCOS_BTN_SELECT;
    case XK_w:
    case XK_W:
        *player = 1;
        return VCOS_BTN_UP;
    case XK_s:
    case XK_S:
        *player = 1;
        return VCOS_BTN_DOWN;
    case XK_a:
    case XK_A:
        *player = 1;
        return VCOS_BTN_LEFT;
    case XK_d:
    case XK_D:
        *player = 1;
        return VCOS_BTN_RIGHT;
    case XK_f:
    case XK_F:
        *player = 1;
        return VCOS_BTN_A;
    case XK_g:
    case XK_G:
        *player = 1;
        return VCOS_BTN_B;
    case XK_r:
    case XK_R:
        *player = 1;
        return VCOS_BTN_START;
    case XK_t:
    case XK_T:
        *player = 1;
        return VCOS_BTN_SELECT;
    case XK_Escape:
    case XK_q:
        *quit = 1;
        return 0;
    default:
        return 0;
    }
}

static int x11_init(int scale, const char *title)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        return -1;
    }
    if (scale < 1) {
        scale = 3;
    }
    if (scale > 8) {
        scale = 8;
    }
    scale_factor = scale;
    win_w = 256 * scale;
    win_h = 240 * scale;

    int screen = DefaultScreen(dpy);
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0,
                              (unsigned)win_w, (unsigned)win_h, 0,
                              BlackPixel(dpy, screen), BlackPixel(dpy, screen));
    XStoreName(dpy, win, title ? title : "VCOS");
    XSelectInput(dpy, win, KeyPressMask | KeyReleaseMask | StructureNotifyMask);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    gc = XCreateGC(dpy, win, 0, NULL);
    img = XCreateImage(dpy, DefaultVisual(dpy, screen),
                       (unsigned)DefaultDepth(dpy, screen), ZPixmap, 0, NULL,
                       (unsigned)win_w, (unsigned)win_h, 32, 0);
    if (!img) {
        XCloseDisplay(dpy);
        dpy = NULL;
        return -1;
    }
    img->data = calloc(1, (size_t)img->bytes_per_line * (size_t)win_h);
    scaled = malloc((size_t)win_w * (size_t)win_h * sizeof(uint32_t));
    if (!img->data || !scaled) {
        XDestroyImage(img);
        img = NULL;
        free(scaled);
        scaled = NULL;
        XCloseDisplay(dpy);
        dpy = NULL;
        return -1;
    }

    XMapWindow(dpy, win);
    XFlush(dpy);
    quit_flag = 0;
    vcos_log("backend: x11 %dx%d (scale %d)", win_w, win_h, scale);
    return 0;
}

static void x11_present(const uint32_t *fb, int width, int height)
{
    int stride = img->bytes_per_line / 4;
    uint32_t *dst = (uint32_t *)img->data;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t color = fb[y * width + x];
            uint32_t *row = dst + (size_t)(y * scale_factor) * (size_t)stride +
                            (size_t)(x * scale_factor);
            for (int sy = 0; sy < scale_factor; sy++) {
                uint32_t *p = row + (size_t)sy * (size_t)stride;
                for (int sx = 0; sx < scale_factor; sx++) {
                    p[sx] = color;
                }
            }
        }
    }
    XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned)win_w, (unsigned)win_h);
    XFlush(dpy);
}

static int x11_poll(input *in, int *quit)
{
    static uint8_t buttons[2];
    while (XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == KeyPress || ev.type == KeyRelease) {
            int pressed = (ev.type == KeyPress);
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            int q = 0;
            int player = 0;
            uint8_t button = map_key(ks, &player, &q);
            if (q) {
                quit_flag = 1;
            }
            if (button) {
                if (pressed) {
                    buttons[player] |= button;
                } else {
                    buttons[player] = (uint8_t)(buttons[player] & ~button);
                }
            }
        } else if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == wm_delete) {
                quit_flag = 1;
            }
        } else if (ev.type == DestroyNotify) {
            quit_flag = 1;
        }
    }
    input_set(in, 0, buttons[0]);
    input_set(in, 1, buttons[1]);
    if (quit_flag && quit) {
        *quit = 1;
    }
    return 0;
}

static void x11_shutdown(void)
{
    if (!dpy) {
        return;
    }
    if (img) {
        XDestroyImage(img);
        img = NULL;
    }
    free(scaled);
    scaled = NULL;
    if (win) {
        XDestroyWindow(dpy, win);
        win = 0;
    }
    XCloseDisplay(dpy);
    dpy = NULL;
}

const backend backend_x11 = {
    "x11",
    x11_init,
    x11_present,
    x11_poll,
    x11_shutdown,
};

#else

static int x11_unavailable(int scale, const char *title)
{
    (void)scale;
    (void)title;
    vcos_error("x11 backend was not compiled in (build with libX11)");
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

const backend backend_x11 = {
    "x11",
    x11_unavailable,
    no_present,
    no_poll,
    no_shutdown,
};

#endif

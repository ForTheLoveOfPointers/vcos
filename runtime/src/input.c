#include "input.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#endif

#include "log.h"

#ifndef __linux__
#define EV_KEY 1
#define EV_MAX 0x1f
#define KEY_MAX 0x2ff
#endif

void input_init(input *in)
{
    memset(in->buttons, 0, sizeof(in->buttons));
    memset(in->shift, 0, sizeof(in->shift));
    in->strobe = 0;
}

void input_set(input *in, int player, uint8_t mask)
{
    if (player >= 0 && player < 2) {
        in->buttons[player] = mask;
    }
}

void input_set_button(input *in, int player, uint8_t button, int pressed)
{
    if (player < 0 || player >= 2) {
        return;
    }
    if (pressed) {
        in->buttons[player] |= button;
    } else {
        in->buttons[player] = (uint8_t)(in->buttons[player] & ~button);
    }
}

void input_write_strobe(input *in, uint8_t value)
{
    if (value & 1) {
        in->shift[0] = in->buttons[0];
        in->shift[1] = in->buttons[1];
        in->strobe = 1;
    } else if (in->strobe) {
        in->strobe = 0;
    }
}

uint8_t input_read(input *in, int player)
{
    if (player < 0 || player >= 2) {
        return 0;
    }
    if (in->strobe) {
        in->shift[player] = in->buttons[player];
        return (uint8_t)(in->buttons[player] & 1);
    }
    uint8_t value = (uint8_t)(in->shift[player] & 1);
    in->shift[player] >>= 1;
    in->shift[player] |= 0x80;
    return value;
}

#ifdef __linux__

#include <time.h>

#define MAX_EVDEV 8

typedef struct {
    int fd;
    int player;
    int gamepad;
    char path[64];
} evdev_dev;

static evdev_dev evdev_devs[MAX_EVDEV];
static int evdev_count;
static time_t evdev_last_scan;

static int evdev_has_key(const unsigned long *bits, int code)
{
    return (int)((bits[code / (8 * sizeof(unsigned long))] >>
                  (code % (8 * sizeof(unsigned long)))) & 1UL);
}

static int evdev_probe(int fd, int *gamepad)
{
    unsigned long evbit = 0;
    unsigned long keybit[KEY_MAX / (8 * sizeof(unsigned long)) + 1];
    memset(keybit, 0, sizeof(keybit));
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit) < 0 ||
        !(evbit & (1UL << EV_KEY)) ||
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) {
        return 0;
    }
    const int gamepad_codes[] = {BTN_SOUTH, BTN_EAST, BTN_START, BTN_SELECT,
                                 BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT,
                                 BTN_DPAD_RIGHT};
    *gamepad = 0;
    for (size_t k = 0; k < sizeof(gamepad_codes) / sizeof(gamepad_codes[0]); k++) {
        if (evdev_has_key(keybit, gamepad_codes[k])) {
            *gamepad = 1;
            break;
        }
    }
    const int keyboard_codes[] = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_Z,
                                  KEY_X, KEY_ENTER, KEY_RIGHTSHIFT, KEY_SPACE};
    for (size_t k = 0; k < sizeof(keyboard_codes) / sizeof(keyboard_codes[0]); k++) {
        if (evdev_has_key(keybit, keyboard_codes[k])) {
            return 1;
        }
    }
    return *gamepad;
}

static void evdev_close_all(void)
{
    for (int i = 0; i < evdev_count; i++) {
        if (evdev_devs[i].fd >= 0) {
            close(evdev_devs[i].fd);
        }
    }
    evdev_count = 0;
}

static void evdev_scan(void)
{
    int old_count = evdev_count;
    evdev_close_all();

    int opened = 0;
    int gamepad_seen = 0;
    for (int i = 0; i < 32 && opened < MAX_EVDEV; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        int gamepad = 0;
        if (!evdev_probe(fd, &gamepad)) {
            close(fd);
            continue;
        }
        evdev_devs[opened].fd = fd;
        evdev_devs[opened].gamepad = gamepad;
        evdev_devs[opened].player = -1;
        snprintf(evdev_devs[opened].path, sizeof(evdev_devs[opened].path), "%s", path);
        opened++;
        if (gamepad) {
            gamepad_seen = 1;
        }
    }
    evdev_count = opened;

    int p1_taken = 0;
    int p2_taken = 0;
    if (gamepad_seen) {
        for (int i = 0; i < evdev_count; i++) {
            if (!evdev_devs[i].gamepad) {
                continue;
            }
            if (!p1_taken) {
                evdev_devs[i].player = 0;
                p1_taken = 1;
            } else if (!p2_taken) {
                evdev_devs[i].player = 1;
                p2_taken = 1;
            }
        }
    }
    for (int i = 0; i < evdev_count; i++) {
        if (evdev_devs[i].player >= 0) {
            continue;
        }
        if (!p1_taken) {
            evdev_devs[i].player = 0;
            p1_taken = 1;
        } else if (!p2_taken) {
            evdev_devs[i].player = 1;
            p2_taken = 1;
        }
    }

    if (old_count != evdev_count) {
        for (int i = 0; i < evdev_count; i++) {
            if (evdev_devs[i].player >= 0) {
                vcos_log("input: opened %s as player %d%s", evdev_devs[i].path,
                         evdev_devs[i].player + 1,
                         evdev_devs[i].gamepad ? " (gamepad)" : "");
            }
        }
        vcos_log("input: %d device(s) active", evdev_count);
    }
    evdev_last_scan = time(NULL);
}

void input_evdev_init(void)
{
    evdev_scan();
}

uint8_t input_map_evdev_key_player2(unsigned short code)
{
    switch (code) {
    case KEY_W:
        return VCOS_BTN_UP;
    case KEY_S:
        return VCOS_BTN_DOWN;
    case KEY_A:
        return VCOS_BTN_LEFT;
    case KEY_D:
        return VCOS_BTN_RIGHT;
    case KEY_F:
        return VCOS_BTN_A;
    case KEY_G:
        return VCOS_BTN_B;
    case KEY_R:
        return VCOS_BTN_START;
    case KEY_T:
        return VCOS_BTN_SELECT;
    default:
        return 0;
    }
}

uint8_t input_map_evdev_key(unsigned short code)
{
    switch (code) {
    case KEY_UP:
    case BTN_DPAD_UP:
        return VCOS_BTN_UP;
    case KEY_DOWN:
    case BTN_DPAD_DOWN:
        return VCOS_BTN_DOWN;
    case KEY_LEFT:
    case BTN_DPAD_LEFT:
        return VCOS_BTN_LEFT;
    case KEY_RIGHT:
    case BTN_DPAD_RIGHT:
        return VCOS_BTN_RIGHT;
    case KEY_Z:
    case KEY_SPACE:
    case BTN_SOUTH:
        return VCOS_BTN_A;
    case KEY_X:
    case BTN_EAST:
        return VCOS_BTN_B;
    case KEY_ENTER:
    case BTN_START:
        return VCOS_BTN_START;
    case KEY_RIGHTSHIFT:
    case KEY_LEFTSHIFT:
    case BTN_SELECT:
        return VCOS_BTN_SELECT;
    default:
        return 0;
    }
}

static uint8_t map_key(unsigned short code)
{
    return input_map_evdev_key(code);
}

void input_evdev_poll(input *in)
{
    time_t now = time(NULL);
    if (now - evdev_last_scan >= 2) {
        evdev_scan();
    }
    for (int i = 0; i < evdev_count; i++) {
        struct input_event ev;
        while (evdev_devs[i].fd >= 0 &&
               read(evdev_devs[i].fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type != EV_KEY || evdev_devs[i].player < 0) {
                continue;
            }
            int player = evdev_devs[i].player;
            uint8_t button = map_key((unsigned short)ev.code);
            if (!evdev_devs[i].gamepad) {
                uint8_t player2 =
                    input_map_evdev_key_player2((unsigned short)ev.code);
                if (player2 != 0) {
                    player = 1;
                    button = player2;
                }
            }
            if (button != 0) {
                input_set_button(in, player, button, ev.value != 0);
            }
        }
    }
}

#else

void input_evdev_init(void) {}
void input_evdev_poll(input *in) { (void)in; }
uint8_t input_map_evdev_key(unsigned short code) { (void)code; return 0; }
uint8_t input_map_evdev_key_player2(unsigned short code)
{
    (void)code;
    return 0;
}

#endif

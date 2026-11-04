#ifndef VCOS_INPUT_H
#define VCOS_INPUT_H

#include <stdint.h>

#ifndef VCOS_BTN_A
#define VCOS_BTN_A 0x01
#endif
#ifndef VCOS_BTN_B
#define VCOS_BTN_B 0x02
#endif
#ifndef VCOS_BTN_SELECT
#define VCOS_BTN_SELECT 0x04
#endif
#ifndef VCOS_BTN_START
#define VCOS_BTN_START 0x08
#endif
#ifndef VCOS_BTN_UP
#define VCOS_BTN_UP 0x10
#endif
#ifndef VCOS_BTN_DOWN
#define VCOS_BTN_DOWN 0x20
#endif
#ifndef VCOS_BTN_LEFT
#define VCOS_BTN_LEFT 0x40
#endif
#ifndef VCOS_BTN_RIGHT
#define VCOS_BTN_RIGHT 0x80
#endif

typedef struct input {
    uint8_t buttons[2];
    uint8_t shift[2];
    uint8_t strobe;
} input;

void input_init(input *in);
void input_set(input *in, int player, uint8_t mask);
void input_set_button(input *in, int player, uint8_t button, int pressed);
void input_write_strobe(input *in, uint8_t value);
uint8_t input_read(input *in, int player);
void input_evdev_init(void);
void input_evdev_poll(input *in);
uint8_t input_map_evdev_key(unsigned short code);
uint8_t input_map_evdev_key_player2(unsigned short code);

#endif

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>

#include "../runtime/src/apu.h"
#include "../runtime/src/input.h"
#include "../runtime/src/ppu.h"
#include "../runtime/src/services.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

static void test_apu(void)
{
    apu a;
    apu_init(&a);
    apu_write(&a, 0x4015, 0x01);
    apu_write(&a, 0x4000, 0xBF);
    apu_write(&a, 0x4002, 0xA6);
    apu_write(&a, 0x4003, 0x01);
    int16_t buf[2048];
    apu_render(&a, buf, 2048, 44100);
    long energy = 0;
    for (int i = 0; i < 2048; i++) {
        energy += buf[i] < 0 ? -buf[i] : buf[i];
    }
    CHECK(energy > 100000);
    CHECK(apu_read_status(&a) == 1);

    apu_write(&a, 0x4015, 0x00);
    apu_render(&a, buf, 2048, 44100);
    for (int i = 0; i < 2048; i++) {
        CHECK(buf[i] == 0);
    }
}

static void test_input(void)
{
    input in;
    input_init(&in);
    input_set(&in, 0, VCOS_BTN_A | VCOS_BTN_LEFT);
    input_set(&in, 1, VCOS_BTN_B | VCOS_BTN_RIGHT);
    input_write_strobe(&in, 1);
    input_write_strobe(&in, 0);
    CHECK(input_read(&in, 0) == 1);
    CHECK(input_read(&in, 0) == 0);
    CHECK(input_read(&in, 0) == 0);
    CHECK(input_read(&in, 0) == 0);
    CHECK(input_read(&in, 0) == 0);
    CHECK(input_read(&in, 0) == 0);
    CHECK(input_read(&in, 0) == 1);
    CHECK(input_read(&in, 0) == 0);
    input_write_strobe(&in, 1);
    input_write_strobe(&in, 0);
    CHECK(input_read(&in, 1) == 0);
    CHECK(input_read(&in, 1) == 1);
    CHECK(input_read(&in, 1) == 0);
    CHECK(input_read(&in, 1) == 0);
    CHECK(input_read(&in, 1) == 0);
    CHECK(input_read(&in, 1) == 0);
    CHECK(input_read(&in, 1) == 0);
    CHECK(input_read(&in, 1) == 1);
}

static void test_keymap(void)
{
    CHECK(input_map_evdev_key(KEY_Z) == VCOS_BTN_A);
    CHECK(input_map_evdev_key(KEY_X) == VCOS_BTN_B);
    CHECK(input_map_evdev_key(KEY_ENTER) == VCOS_BTN_START);
    CHECK(input_map_evdev_key(KEY_RIGHTSHIFT) == VCOS_BTN_SELECT);
    CHECK(input_map_evdev_key(KEY_UP) == VCOS_BTN_UP);
    CHECK(input_map_evdev_key(BTN_SOUTH) == VCOS_BTN_A);
    CHECK(input_map_evdev_key(BTN_DPAD_RIGHT) == VCOS_BTN_RIGHT);
    CHECK(input_map_evdev_key(KEY_F1) == 0);
    CHECK(input_map_evdev_key_player2(KEY_W) == VCOS_BTN_UP);
    CHECK(input_map_evdev_key_player2(KEY_S) == VCOS_BTN_DOWN);
    CHECK(input_map_evdev_key_player2(KEY_A) == VCOS_BTN_LEFT);
    CHECK(input_map_evdev_key_player2(KEY_D) == VCOS_BTN_RIGHT);
    CHECK(input_map_evdev_key_player2(KEY_F) == VCOS_BTN_A);
    CHECK(input_map_evdev_key_player2(KEY_G) == VCOS_BTN_B);
    CHECK(input_map_evdev_key_player2(KEY_R) == VCOS_BTN_START);
    CHECK(input_map_evdev_key_player2(KEY_T) == VCOS_BTN_SELECT);
    CHECK(input_map_evdev_key_player2(KEY_UP) == 0);
}

static void test_ppu(void)
{
    ppu p;
    ppu_init(&p, NULL);
    ppu_reset(&p);
    memset(p.chr_ram, 0xFF, sizeof(p.chr_ram));
    p.palette[0] = 0x0F;
    p.palette[1] = 0x30;
    p.palette[2] = 0x21;
    p.palette[3] = 0x12;
    p.mask = 0x1E;
    p.ctrl = 0x00;
    ppu_render_scanline(&p, 0);
    CHECK(p.fb[0] != 0x666666);
    CHECK(p.bg_index[0] == 3);

    memset(p.oam, 0, sizeof(p.oam));
    for (int i = 0; i < 9; i++) {
        p.oam[i * 4 + 0] = 0;
        p.oam[i * 4 + 1] = 0;
        p.oam[i * 4 + 2] = 0;
        p.oam[i * 4 + 3] = (uint8_t)(i * 8);
    }
    p.oam[0] = 0;
    p.status = 0;
    ppu_render_scanline(&p, 1);
    CHECK((p.status & 0x20) != 0);
    CHECK((p.status & 0x40) != 0);
}

static uint8_t service_read(services *s, int off)
{
    return services_read(s, (uint16_t)(VCOS_SVC_BASE + off));
}

static void test_services(void)
{
    char dir[] = "/tmp/vcos-svc-test.XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char shell[512];
    snprintf(shell, sizeof(shell), "%s/shell.nes", dir);
    FILE *f = fopen(shell, "wb");
    CHECK(f != NULL);
    fputs("shell", f);
    fclose(f);
    const char *names[] = {"music.nes", "demo.nes"};
    for (int i = 0; i < 2; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        f = fopen(path, "wb");
        CHECK(f != NULL);
        fputs("rom", f);
        fclose(f);
    }

    services s;
    services_init(&s, shell, dir, dir, dir, "vcos-module-host", 0);
    CHECK(s.library_count == 2);
    CHECK(strcmp(s.library[0].name, "DEMO") == 0);
    CHECK(strcmp(s.library[1].name, "MUSIC") == 0);
    CHECK(s.system_count == 1);
    CHECK(strcmp(s.systems[0].name, "NES") == 0);
    CHECK(s.systems[0].count == 2);

    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_SYS_COUNT);
    CHECK(service_read(&s, 8) == 1);

    services_write(&s, VCOS_SVC_BASE + 1, 0);
    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_SYS_NAME);
    CHECK(strcmp((char *)s.data, "NES") == 0);

    services_write(&s, VCOS_SVC_BASE + 1, 0);
    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_SYS_GAME_COUNT);
    CHECK(service_read(&s, 8) == 2);

    services_write(&s, VCOS_SVC_BASE + 1, 0);
    services_write(&s, VCOS_SVC_BASE + 2, 1);
    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_SYS_GAME_NAME);
    CHECK(strcmp((char *)s.data, "MUSIC") == 0);

    services_write(&s, VCOS_SVC_BASE + 1, 0);
    services_write(&s, VCOS_SVC_BASE + 2, 1);
    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_SYS_LAUNCH);
    char path[512];
    char module[512];
    int external = -1;
    CHECK(services_take_request(&s, path, sizeof(path), module, sizeof(module),
                                &external) == VCOS_SVC_REQUEST_LOAD);
    CHECK(strstr(path, "music.nes") != NULL);

    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_LIB_COUNT);
    CHECK(service_read(&s, 8) == 2);

    services_write(&s, VCOS_SVC_BASE + 1, 1);
    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_LIB_NAME);
    CHECK(strcmp((char *)s.data, "MUSIC") == 0);

    services_write(&s, VCOS_SVC_BASE + 1, 1);
    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_LIB_LAUNCH);
    CHECK(services_take_request(&s, path, sizeof(path), module, sizeof(module),
                                &external) == VCOS_SVC_REQUEST_LOAD);
    CHECK(strstr(path, "music.nes") != NULL);

    for (int i = 0; i < 16; i++) {
        services_write(&s, (uint16_t)(VCOS_SVC_BASE + 8 + i), (uint8_t)(i + 1));
    }
    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_SAVE);
    for (int i = 0; i < 16; i++) {
        services_write(&s, (uint16_t)(VCOS_SVC_BASE + 8 + i), 0);
    }
    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_LOAD);
    for (int i = 0; i < 16; i++) {
        CHECK(s.data[i] == (uint8_t)(i + 1));
    }

    char save[512];
    snprintf(save, sizeof(save), "%s/save.bin", dir);
    f = fopen(save, "rb");
    CHECK(f != NULL);
    uint8_t header[12];
    CHECK(fread(header, 1, sizeof(header), f) == sizeof(header));
    fclose(f);
    CHECK(memcmp(header, "VCS1", 4) == 0);
    CHECK(header[4] == 1);
    CHECK(header[5] == 16);

    f = fopen(save, "wb");
    CHECK(f != NULL);
    for (int i = 0; i < 16; i++) {
        fputc(i + 10, f);
    }
    fclose(f);
    services_write(&s, VCOS_SVC_BASE, VCOS_CMD_LOAD);
    for (int i = 0; i < 16; i++) {
        CHECK(s.data[i] == (uint8_t)(i + 10));
    }
    CHECK(s.dirty == 0);
    services_flush(&s);
    CHECK(s.dirty == 0);

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    CHECK(system(cmd) == 0);
    (void)service_read;
}

int main(void)
{
    test_apu();
    test_input();
    test_keymap();
    test_ppu();
    test_services();
    printf("runtime_tests: ok\n");
    return 0;
}

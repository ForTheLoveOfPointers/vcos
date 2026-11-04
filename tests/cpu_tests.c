#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/src/apu.h"
#include "../runtime/src/bus.h"
#include "../runtime/src/cartridge.h"
#include "../runtime/src/cpu6502.h"
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

static cartridge *make_rom(const uint8_t *code, size_t len)
{
    size_t prg_size = 0x8000;
    size_t total = 16 + prg_size;
    uint8_t *buf = calloc(1, total);
    CHECK(buf != NULL);
    buf[0] = 'N';
    buf[1] = 'E';
    buf[2] = 'S';
    buf[3] = 0x1A;
    buf[4] = 2;
    buf[5] = 0;
    buf[6] = 0x01;
    CHECK(len <= prg_size);
    memcpy(buf + 16, code, len);
    size_t vectors = 16 + 0x7FFA;
    buf[vectors + 0] = 0x00;
    buf[vectors + 1] = 0x80;
    buf[vectors + 2] = 0x00;
    buf[vectors + 3] = 0x80;
    buf[vectors + 4] = 0x00;
    buf[vectors + 5] = 0x80;
    cartridge *c = cart_from_memory(buf, total);
    free(buf);
    CHECK(c != NULL);
    return c;
}

static void setup(bus *b, cartridge *c)
{
    memset(b, 0, sizeof(*b));
    bus_init(b, c, NULL, NULL, NULL, NULL);
    bus_reset(b);
}

static void run_steps(bus *b, int steps)
{
    for (int i = 0; i < steps; i++) {
        int cycles = cpu_step(&b->cpu, b);
        bus_tick(b, cycles);
    }
}

static void test_adc(void)
{
    const uint8_t code[] = {0xA9, 0x7F, 0x18, 0x69, 0x01};
    cartridge *c = make_rom(code, sizeof(code));
    bus b;
    setup(&b, c);
    run_steps(&b, 3);
    CHECK(b.cpu.a == 0x80);
    CHECK((b.cpu.p & CPU_FLAG_C) == 0);
    CHECK((b.cpu.p & CPU_FLAG_V) != 0);
    CHECK((b.cpu.p & CPU_FLAG_N) != 0);
    CHECK((b.cpu.p & CPU_FLAG_Z) == 0);
    cart_free(c);
}

static void test_sbc(void)
{
    const uint8_t code[] = {0x38, 0xA9, 0x10, 0xE9, 0x01};
    cartridge *c = make_rom(code, sizeof(code));
    bus b;
    setup(&b, c);
    run_steps(&b, 3);
    CHECK(b.cpu.a == 0x0F);
    CHECK((b.cpu.p & CPU_FLAG_C) != 0);
    CHECK((b.cpu.p & CPU_FLAG_Z) == 0);
    cart_free(c);
}

static void test_jsr_rts(void)
{
    const uint8_t code[] = {
        0x20, 0x08, 0x80,
        0xA9, 0x42,
        0xEA, 0xEA, 0xEA,
        0x60,
    };
    cartridge *c = make_rom(code, sizeof(code));
    bus b;
    setup(&b, c);
    run_steps(&b, 3);
    CHECK(b.cpu.a == 0x42);
    CHECK(b.cpu.pc == 0x8005);
    cart_free(c);
}

static void test_branch(void)
{
    const uint8_t code[] = {
        0xA2, 0x00,
        0xE8,
        0xE0, 0x05,
        0xD0, 0xFB,
    };
    cartridge *c = make_rom(code, sizeof(code));
    bus b;
    setup(&b, c);
    run_steps(&b, 16);
    CHECK(b.cpu.x == 5);
    CHECK((b.cpu.p & CPU_FLAG_Z) != 0);
    cart_free(c);
}

static void test_memory(void)
{
    const uint8_t code[] = {
        0xA9, 0xAB,
        0x8D, 0x00, 0x02,
        0xA9, 0x00,
        0xAD, 0x00, 0x02,
    };
    cartridge *c = make_rom(code, sizeof(code));
    bus b;
    setup(&b, c);
    run_steps(&b, 4);
    CHECK(b.cpu.a == 0xAB);
    CHECK(bus_read(&b, 0x0200) == 0xAB);
    cart_free(c);
}

static void test_stack(void)
{
    const uint8_t code[] = {0xA9, 0x55, 0x48, 0xA9, 0x00, 0x68};
    cartridge *c = make_rom(code, sizeof(code));
    bus b;
    setup(&b, c);
    run_steps(&b, 4);
    CHECK(b.cpu.a == 0x55);
    cart_free(c);
}

static void test_shift(void)
{
    const uint8_t code[] = {0xA9, 0x81, 0x0A, 0x4A};
    cartridge *c = make_rom(code, sizeof(code));
    bus b;
    setup(&b, c);
    run_steps(&b, 3);
    CHECK(b.cpu.a == 0x01);
    CHECK((b.cpu.p & CPU_FLAG_C) == 0);
    cart_free(c);

    const uint8_t code2[] = {0xA9, 0x81, 0x0A};
    cartridge *c2 = make_rom(code2, sizeof(code2));
    setup(&b, c2);
    run_steps(&b, 2);
    CHECK(b.cpu.a == 0x02);
    CHECK((b.cpu.p & CPU_FLAG_C) != 0);
    cart_free(c2);
}

static void test_indexed(void)
{
    const uint8_t code[] = {0xA0, 0x02, 0xB9, 0x00, 0x03};
    cartridge *c = make_rom(code, sizeof(code));
    bus b;
    setup(&b, c);
    bus_write(&b, 0x0302, 0x77);
    run_steps(&b, 2);
    CHECK(b.cpu.a == 0x77);
    CHECK(b.cpu.y == 2);
    cart_free(c);
}

static void test_indirect(void)
{
    const uint8_t code[] = {
        0xA2, 0x04,
        0xA9, 0x00,
        0x85, 0x10,
        0xA9, 0x03,
        0x85, 0x11,
        0xA1, 0x0C,
    };
    cartridge *c = make_rom(code, sizeof(code));
    bus b;
    setup(&b, c);
    bus_write(&b, 0x0300, 0x99);
    run_steps(&b, 6);
    CHECK(b.cpu.a == 0x99);
    cart_free(c);
}

static void test_ppu(void)
{
    const uint8_t code[] = {0xEA};
    cartridge *c = make_rom(code, sizeof(code));
    ppu p;
    ppu_init(&p, c);
    ppu_reset(&p);

    ppu_write_reg(&p, 0, 0x80);
    ppu_step(&p, 241 * 341 + 1);
    CHECK((p.status & 0x80) != 0);
    CHECK((ppu_read_reg(&p, 2) & 0x80) != 0);
    CHECK((p.status & 0x80) == 0);

    ppu_write_reg(&p, 6, 0x3F);
    ppu_write_reg(&p, 6, 0x00);
    ppu_write_reg(&p, 7, 0x30);
    CHECK(p.palette[0] == 0x30);

    ppu_write_reg(&p, 6, 0x20);
    ppu_write_reg(&p, 6, 0x00);
    ppu_write_reg(&p, 7, 0x5A);
    cart_write_chr(c, 0x5A * 16, 0xF8);
    ppu_write_reg(&p, 6, 0x3F);
    ppu_write_reg(&p, 6, 0x01);
    ppu_write_reg(&p, 7, 0x30);
    ppu_write_reg(&p, 6, 0x3F);
    ppu_write_reg(&p, 6, 0x00);
    ppu_write_reg(&p, 7, 0x0F);
    ppu_write_reg(&p, 1, 0x0A);
    ppu_write_reg(&p, 5, 0x00);
    ppu_write_reg(&p, 5, 0x00);
    ppu_render(&p);
    CHECK(p.fb[0] == 0xFFFEFF);

    cart_free(c);
}

int main(void)
{
    test_adc();
    test_sbc();
    test_jsr_rts();
    test_branch();
    test_memory();
    test_stack();
    test_shift();
    test_indexed();
    test_indirect();
    test_ppu();
    printf("cpu_tests: ok\n");
    return 0;
}

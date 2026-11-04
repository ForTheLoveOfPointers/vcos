#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/src/cartridge.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

static cartridge *build(int prg_banks, int chr_banks, int flags6, int flags7,
                        uint8_t fill)
{
    size_t size = 16 + (size_t)prg_banks * 0x4000 + (size_t)chr_banks * 0x2000;
    uint8_t *buf = calloc(1, size);
    CHECK(buf != NULL);
    buf[0] = 'N';
    buf[1] = 'E';
    buf[2] = 'S';
    buf[3] = 0x1A;
    buf[4] = (uint8_t)prg_banks;
    buf[5] = (uint8_t)chr_banks;
    buf[6] = (uint8_t)flags6;
    buf[7] = (uint8_t)flags7;
    memset(buf + 16, fill, size - 16);
    cartridge *c = cart_from_memory(buf, size);
    free(buf);
    return c;
}

static void test_nrom(void)
{
    cartridge *c = build(1, 1, 0x01, 0x00, 0x5A);
    CHECK(c != NULL);
    CHECK(c->mapper == 0);
    CHECK(c->prg_size == 0x4000);
    CHECK(c->chr_size == 0x2000);
    CHECK(c->mirroring == 1);
    CHECK(cart_read_prg(c, 0x8000) == 0x5A);
    CHECK(cart_read_prg(c, 0xC000) == 0x5A);
    CHECK(cart_read_chr(c, 0x0000) == 0x5A);
    cart_free(c);
}

static void test_mapper2(void)
{
    size_t size = 16 + 4 * 0x4000;
    uint8_t *buf = calloc(1, size);
    CHECK(buf != NULL);
    buf[0] = 'N';
    buf[1] = 'E';
    buf[2] = 'S';
    buf[3] = 0x1A;
    buf[4] = 4;
    buf[5] = 0;
    buf[6] = 0x20;
    for (int bank = 0; bank < 4; bank++) {
        buf[16 + bank * 0x4000] = (uint8_t)bank;
    }
    cartridge *c = cart_from_memory(buf, size);
    free(buf);
    CHECK(c != NULL);
    CHECK(c->mapper == 2);
    CHECK(cart_read_prg(c, 0x8000) == 0);
    cart_write_mapper(c, 0x8000, 1);
    CHECK(cart_read_prg(c, 0x8000) == 1);
    CHECK(cart_read_prg(c, 0xC000) == 3);
    cart_write_mapper(c, 0x8000, 2);
    CHECK(cart_read_prg(c, 0x8000) == 2);
    cart_free(c);
}


static cartridge *build_banked(int mapper, int prg_banks, int chr_banks)
{
    size_t size = 16 + (size_t)prg_banks * 0x4000 + (size_t)chr_banks * 0x2000;
    uint8_t *buf = calloc(1, size);
    CHECK(buf != NULL);
    buf[0] = 'N';
    buf[1] = 'E';
    buf[2] = 'S';
    buf[3] = 0x1A;
    buf[4] = (uint8_t)prg_banks;
    buf[5] = (uint8_t)chr_banks;
    buf[6] = (uint8_t)((mapper & 0x0F) << 4);
    buf[7] = (uint8_t)(mapper & 0xF0);
    for (int bank = 0; bank < prg_banks; bank++) {
        buf[16 + (size_t)bank * 0x4000] = (uint8_t)bank;
    }
    for (int bank = 0; bank < chr_banks * 4; bank++) {
        buf[16 + (size_t)prg_banks * 0x4000 + (size_t)bank * 0x400] =
            (uint8_t)(bank & 0xFF);
    }
    cartridge *c = cart_from_memory(buf, size);
    free(buf);
    return c;
}

static void mmc1_write(cartridge *c, uint16_t addr, uint8_t value)
{
    for (int bit = 0; bit < 5; bit++) {
        cart_write_mapper(c, addr, (uint8_t)((value >> bit) & 1));
    }
}

static void test_mapper1(void)
{
    cartridge *c = build_banked(1, 8, 8);
    CHECK(c != NULL);
    CHECK(c->mapper == 1);

    mmc1_write(c, 0x8000, 0x0C);
    mmc1_write(c, 0xE000, 3);
    CHECK(cart_read_prg(c, 0x8000) == 3);
    CHECK(cart_read_prg(c, 0xC000) == 7);
    CHECK(c->mirroring == 2);

    mmc1_write(c, 0x8000, 0x08);
    CHECK(cart_read_prg(c, 0x8000) == 0);
    CHECK(cart_read_prg(c, 0xC000) == 3);

    mmc1_write(c, 0x8000, 0x04);
    mmc1_write(c, 0xE000, 4);
    CHECK(cart_read_prg(c, 0x8000) == 4);
    CHECK(cart_read_prg(c, 0xC000) == 5);

    mmc1_write(c, 0x8000, 0x12);
    mmc1_write(c, 0xA000, 2);
    mmc1_write(c, 0xC000, 3);
    CHECK(cart_read_chr(c, 0x0000) == 8);
    CHECK(cart_read_chr(c, 0x1000) == 12);

    cart_write_mapper(c, 0x8000, 0x80);
    CHECK(c->mmc1_shift == 0x10);
    cart_free(c);
}

static void mmc3_write(cartridge *c, uint16_t addr, uint8_t value)
{
    cart_write_mapper(c, addr, value);
}

static void test_mapper4(void)
{
    cartridge *c = build_banked(4, 4, 8);
    CHECK(c != NULL);
    CHECK(c->mapper == 4);
    for (int bank = 0; bank < 8; bank++) {
        c->prg[(size_t)bank * 0x2000] = (uint8_t)bank;
    }

    mmc3_write(c, 0x8000, 6);
    mmc3_write(c, 0x8001, 1);
    mmc3_write(c, 0x8000, 7);
    mmc3_write(c, 0x8001, 2);
    CHECK(cart_read_prg(c, 0x8000) == 1);
    CHECK(cart_read_prg(c, 0xA000) == 2);
    CHECK(cart_read_prg(c, 0xC000) == 6);
    CHECK(cart_read_prg(c, 0xE000) == 7);

    mmc3_write(c, 0x8000, 0x46);
    CHECK(cart_read_prg(c, 0x8000) == 6);
    CHECK(cart_read_prg(c, 0xC000) == 1);

    mmc3_write(c, 0x8000, 0);
    mmc3_write(c, 0x8001, 2);
    mmc3_write(c, 0x8000, 1);
    mmc3_write(c, 0x8001, 3);
    mmc3_write(c, 0x8000, 2);
    mmc3_write(c, 0x8001, 8);
    CHECK(cart_read_chr(c, 0x0000) == 4);
    CHECK(cart_read_chr(c, 0x0400) == 5);
    CHECK(cart_read_chr(c, 0x0800) == 6);
    CHECK(cart_read_chr(c, 0x1000) == 8);

    mmc3_write(c, 0xA000, 1);
    CHECK(c->mirroring == 0);
    mmc3_write(c, 0xA000, 0);
    CHECK(c->mirroring == 1);

    mmc3_write(c, 0xC000, 3);
    mmc3_write(c, 0xC001, 1);
    mmc3_write(c, 0xE001, 1);
    for (int i = 0; i < 4; i++) {
        cart_scanline(c);
    }
    CHECK(c->mmc3_irq_pending == 1);
    cart_free(c);
}

static void test_unsupported(void)
{
    cartridge *c = build(1, 1, 0x50, 0x00, 0);
    CHECK(c == NULL);
}

static void test_bad_header(void)
{
    uint8_t buf[32] = {0};
    CHECK(cart_from_memory(buf, sizeof(buf)) == NULL);
}

int main(void)
{
    test_nrom();
    test_mapper2();
    test_mapper1();
    test_mapper4();
    test_unsupported();
    test_bad_header();
    printf("cartridge_tests: ok\n");
    return 0;
}

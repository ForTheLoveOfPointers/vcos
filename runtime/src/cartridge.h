#ifndef VCOS_CARTRIDGE_H
#define VCOS_CARTRIDGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct cartridge {
    uint8_t *prg;
    size_t prg_size;
    uint8_t *chr;
    size_t chr_size;
    int chr_is_ram;
    int mapper;
    int mirroring;
    uint8_t prg_bank;
    uint8_t chr_bank;
    uint8_t prg_ram[8192];
    uint8_t mmc1_shift;
    uint8_t mmc1_control;
    uint8_t mmc1_chr0;
    uint8_t mmc1_chr1;
    uint8_t mmc1_prg;
    uint8_t mmc3_select;
    uint8_t mmc3_regs[8];
    uint8_t mmc3_irq_latch;
    uint8_t mmc3_irq_counter;
    uint8_t mmc3_irq_reload;
    uint8_t mmc3_irq_enabled;
    uint8_t mmc3_irq_pending;
} cartridge;

cartridge *cart_load(const char *path);
cartridge *cart_from_memory(const uint8_t *data, size_t size);
void cart_free(cartridge *c);
uint8_t cart_read_prg(cartridge *c, uint16_t addr);
void cart_write_prg(cartridge *c, uint16_t addr, uint8_t value);
uint8_t cart_read_chr(cartridge *c, uint16_t addr);
void cart_write_chr(cartridge *c, uint16_t addr, uint8_t value);
void cart_write_mapper(cartridge *c, uint16_t addr, uint8_t value);
void cart_scanline(cartridge *c);

#endif

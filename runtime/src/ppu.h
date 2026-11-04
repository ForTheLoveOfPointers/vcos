#ifndef VCOS_PPU_H
#define VCOS_PPU_H

#include <stdint.h>

typedef struct cartridge cartridge;

typedef struct ppu {
    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    uint8_t oam_addr;
    uint16_t v;
    uint16_t t;
    uint8_t fine_x;
    uint8_t w;
    uint8_t vram[2048];
    uint8_t palette[32];
    uint8_t oam[256];
    uint8_t read_buffer;
    uint8_t chr_ram[8192];
    int scanline;
    int dot;
    int frame_ready;
    int nmi_pending;
    int mirroring;
    uint32_t fb[256 * 240];
    uint8_t bg_index[256 * 240];
    cartridge *cart;
} ppu;

void ppu_init(ppu *p, cartridge *cart);
void ppu_reset(ppu *p);
uint8_t ppu_read_reg(ppu *p, uint16_t addr);
void ppu_write_reg(ppu *p, uint16_t addr, uint8_t value);
void ppu_step(ppu *p, int cycles);
void ppu_render(ppu *p);
void ppu_render_scanline(ppu *p, int y);

#endif

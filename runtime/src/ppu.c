#include "ppu.h"

#include <string.h>

#include "cartridge.h"

static const uint32_t nes_palette[64] = {
    0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600, 0x561D00,
    0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000, 0x000000, 0x000000,
    0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00,
    0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000, 0x000000, 0x000000,
    0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22,
    0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
    0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5,
    0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000
};

static int nt_index(ppu *p, uint16_t addr)
{
    uint16_t a = (uint16_t)(addr & 0x0FFF);
    int table = a >> 10;
    int offset = a & 0x03FF;
    int mirroring = p->cart ? p->cart->mirroring : p->mirroring;
    int physical;
    if (mirroring == 1) {
        physical = table & 1;
    } else if (mirroring == 2) {
        physical = 0;
    } else if (mirroring == 3) {
        physical = 1;
    } else {
        physical = table >> 1;
    }
    return physical * 0x400 + offset;
}

static int palette_index(ppu *p, uint16_t addr)
{
    (void)p;
    int idx = addr & 0x1F;
    if ((idx & 0x13) == 0x10) {
        idx &= 0x0F;
    }
    return idx;
}

static uint8_t chr_read(ppu *p, uint16_t addr)
{
    if (p->cart) {
        return cart_read_chr(p->cart, addr);
    }
    return p->chr_ram[addr & 0x1FFF];
}

static uint8_t mem_read(ppu *p, uint16_t addr)
{
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        return chr_read(p, addr);
    }
    if (addr < 0x3F00) {
        return p->vram[nt_index(p, addr)];
    }
    return p->palette[palette_index(p, addr)];
}

static void mem_write(ppu *p, uint16_t addr, uint8_t value)
{
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        if (p->cart && p->cart->chr_is_ram) {
            cart_write_chr(p->cart, addr, value);
        } else {
            p->chr_ram[addr & 0x1FFF] = value;
        }
        return;
    }
    if (addr < 0x3F00) {
        p->vram[nt_index(p, addr)] = value;
        return;
    }
    p->palette[palette_index(p, addr)] = (uint8_t)(value & 0x3F);
}

void ppu_init(ppu *p, cartridge *cart)
{
    memset(p, 0, sizeof(*p));
    p->cart = cart;
    if (cart) {
        p->mirroring = cart->mirroring;
    }
}

void ppu_reset(ppu *p)
{
    p->ctrl = 0;
    p->mask = 0;
    p->status = 0;
    p->oam_addr = 0;
    p->v = 0;
    p->t = 0;
    p->fine_x = 0;
    p->w = 0;
    p->read_buffer = 0;
    p->scanline = 0;
    p->dot = 0;
    p->frame_ready = 0;
    p->nmi_pending = 0;
    memset(p->palette, 0, sizeof(p->palette));
    memset(p->oam, 0, sizeof(p->oam));
}

static void scroll_write(ppu *p, uint8_t value)
{
    if (!p->w) {
        p->t = (uint16_t)((p->t & 0xFF00) | value);
        p->fine_x = (uint8_t)(value & 0x07);
        p->w = 1;
    } else {
        p->t = (uint16_t)((p->t & 0x00FF) | ((value & 0x3F) << 8));
        p->w = 0;
    }
}

static void addr_write(ppu *p, uint8_t value)
{
    if (!p->w) {
        p->t = (uint16_t)((p->t & 0x00FF) | ((value & 0x3F) << 8));
        p->w = 1;
    } else {
        p->t = (uint16_t)((p->t & 0xFF00) | value);
        p->v = p->t;
        p->w = 0;
    }
}

uint8_t ppu_read_reg(ppu *p, uint16_t addr)
{
    switch (addr & 0x7) {
    case 2: {
        uint8_t value = (uint8_t)((p->status & 0xE0) | (p->read_buffer & 0x1F));
        p->status = (uint8_t)(p->status & ~0x80);
        p->w = 0;
        return value;
    }
    case 4:
        return p->oam[p->oam_addr];
    case 7: {
        uint16_t a = (uint16_t)(p->v & 0x3FFF);
        uint8_t result;
        if (a >= 0x3F00) {
            result = (uint8_t)(p->palette[palette_index(p, a)] & 0x3F);
            p->read_buffer = p->vram[nt_index(p, (uint16_t)(a & 0x2FFF))];
        } else {
            result = p->read_buffer;
            p->read_buffer = mem_read(p, a);
        }
        p->v = (uint16_t)((p->v + ((p->ctrl & 0x04) ? 32 : 1)) & 0x7FFF);
        return result;
    }
    default:
        return p->read_buffer;
    }
}

void ppu_write_reg(ppu *p, uint16_t addr, uint8_t value)
{
    switch (addr & 0x7) {
    case 0:
        p->ctrl = value;
        p->t = (uint16_t)((p->t & ~0x0C00) | ((value & 0x03) << 10));
        break;
    case 1:
        p->mask = value;
        break;
    case 3:
        p->oam_addr = value;
        break;
    case 4:
        p->oam[p->oam_addr++] = value;
        break;
    case 5:
        scroll_write(p, value);
        break;
    case 6:
        addr_write(p, value);
        break;
    case 7:
        mem_write(p, (uint16_t)(p->v & 0x3FFF), value);
        p->v = (uint16_t)((p->v + ((p->ctrl & 0x04) ? 32 : 1)) & 0x7FFF);
        break;
    default:
        break;
    }
}

void ppu_step(ppu *p, int cycles)
{
    while (cycles > 0) {
        int remain = 341 - p->dot;
        int step = cycles < remain ? cycles : remain;
        int before = p->dot;
        p->dot += step;
        cycles -= step;
        if (before < 256 && p->dot >= 256 &&
            p->scanline >= 0 && p->scanline < 240) {
            ppu_render_scanline(p, p->scanline);
        }
        if (p->dot >= 341) {
            p->dot = 0;
            p->scanline++;
            if (p->scanline < 240 && p->cart) {
                cart_scanline(p->cart);
            }
            if (p->scanline == 241) {
                p->status |= 0x80;
                if (p->ctrl & 0x80) {
                    p->nmi_pending = 1;
                }
            }
            if (p->scanline == 261) {
                p->status &= (uint8_t)~0x60;
            }
            if (p->scanline >= 262) {
                p->scanline = 0;
                p->status &= (uint8_t)~0x40;
                p->frame_ready = 1;
            }
        }
    }
}

static uint32_t bg_color(ppu *p, int color_index)
{
    if (color_index == 0) {
        return nes_palette[p->palette[0] & 0x3F];
    }
    return nes_palette[p->palette[color_index & 0x1F] & 0x3F];
}

void ppu_render_scanline(ppu *p, int y)
{
    if (y < 0 || y >= 240) {
        return;
    }
    int bg_enabled = (p->mask & 0x08) != 0;
    uint16_t s = p->t;
    int base_nt = (s >> 10) & 3;
    int coarse_x = s & 0x1F;
    int coarse_y = (s >> 5) & 0x1F;
    int fine_y = (s >> 12) & 7;
    int scroll_x = coarse_x * 8 + p->fine_x;
    int scroll_y = coarse_y * 8 + fine_y;
    uint16_t bg_pattern = (p->ctrl & 0x10) ? 0x1000 : 0x0000;

    for (int x = 0; x < 256; x++) {
        int color = 0;
        if (bg_enabled && !(x < 8 && !(p->mask & 0x02))) {
            int sx = x + scroll_x;
            int sy = y + scroll_y;
            int nt = base_nt;
            if (sx >= 256) {
                sx -= 256;
                nt ^= 1;
            }
            while (sy >= 240) {
                sy -= 240;
                nt ^= 2;
            }
            int tx = (sx >> 3) & 31;
            int ty = (sy >> 3) % 30;
            uint16_t nt_addr = (uint16_t)((nt & 3) * 0x400 + ty * 32 + tx);
            uint8_t tile = p->vram[nt_index(p, nt_addr)];
            uint16_t attr_addr = (uint16_t)((nt & 3) * 0x400 + 0x3C0 +
                                            (ty / 4) * 8 + (tx / 4));
            uint8_t attr = p->vram[nt_index(p, attr_addr)];
            int shift = ((ty & 2) ? 4 : 0) | ((tx & 2) ? 2 : 0);
            int pal = (attr >> shift) & 3;
            int row = sy & 7;
            int bit = 7 - (sx & 7);
            uint16_t pat = (uint16_t)(bg_pattern + tile * 16 + row);
            uint8_t lo = chr_read(p, pat);
            uint8_t hi = chr_read(p, (uint16_t)(pat + 8));
            int ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            color = (ci == 0) ? 0 : pal * 4 + ci;
        }
        p->bg_index[y * 256 + x] = (uint8_t)color;
        p->fb[y * 256 + x] = bg_color(p, color);
    }

    if (!(p->mask & 0x10)) {
        return;
    }

    int height = (p->ctrl & 0x20) ? 16 : 8;
    uint16_t sprite_pattern = (p->ctrl & 0x08) ? 0x1000 : 0x0000;
    int sprites_on_line = 0;

    for (int i = 0; i < 64; i++) {
        int oy = p->oam[i * 4 + 0];
        int tile = p->oam[i * 4 + 1];
        int attr = p->oam[i * 4 + 2];
        int ox = p->oam[i * 4 + 3];
        if (oy >= 0xEF) {
            continue;
        }
        int top = oy + 1;
        if (y < top || y >= top + height) {
            continue;
        }
        sprites_on_line++;
        int hflip = (attr & 0x40) != 0;
        int vflip = (attr & 0x80) != 0;
        int behind = (attr & 0x20) != 0;
        int pal = 4 + (attr & 3);
        int py = y - top;
        int row = py;
        int t = tile;
        uint16_t pat;
        if (height == 16) {
            int r = vflip ? (15 - row) : row;
            t = (tile & 0xFE) | ((r >= 8) ? 1 : 0);
            row = r & 7;
            pat = 0x1000;
        } else {
            if (vflip) {
                row = 7 - row;
            }
            pat = sprite_pattern;
        }
        uint16_t addr = (uint16_t)(pat + t * 16 + row);
        uint8_t lo = chr_read(p, addr);
        uint8_t hi = chr_read(p, (uint16_t)(addr + 8));

        for (int px = 0; px < 8; px++) {
            if (ox + px >= 256) {
                continue;
            }
            if (ox + px < 8 && !(p->mask & 0x04)) {
                continue;
            }
            int col = hflip ? (7 - px) : px;
            int bit = 7 - col;
            int ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            if (ci == 0) {
                continue;
            }
            int x = ox + px;
            int bg = p->bg_index[y * 256 + x];
            if (behind && bg != 0) {
                continue;
            }
            if (i == 0 && bg != 0 && x < 255 && y < 239) {
                p->status |= 0x40;
            }
            p->fb[y * 256 + x] = bg_color(p, pal * 4 + ci);
        }
    }

    if (sprites_on_line > 8) {
        p->status |= 0x20;
    }
}

void ppu_render(ppu *p)
{
    for (int y = 0; y < 240; y++) {
        ppu_render_scanline(p, y);
    }
}

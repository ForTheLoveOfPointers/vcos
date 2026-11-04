#include "cartridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

static cartridge *cart_parse(const uint8_t *data, size_t size)
{
    if (size < 16 || memcmp(data, "NES\x1A", 4) != 0) {
        vcos_error("cartridge: not an iNES file");
        return NULL;
    }

    int prg_banks = data[4];
    int chr_banks = data[5];
    uint8_t flags6 = data[6];
    uint8_t flags7 = data[7];
    int mapper = (int)((flags6 >> 4) | (flags7 & 0xF0));
    int four_screen = (flags6 & 0x08) != 0;

    if (prg_banks == 0) {
        vcos_error("cartridge: no PRG banks");
        return NULL;
    }
    if (mapper != 0 && mapper != 1 && mapper != 2 && mapper != 3 && mapper != 4) {
        vcos_error("cartridge: unsupported mapper %d", mapper);
        return NULL;
    }

    size_t offset = 16;
    if (flags6 & 0x04) {
        offset += 512;
    }
    size_t prg_size = (size_t)prg_banks * 0x4000;
    size_t chr_size = (size_t)chr_banks * 0x2000;
    if (size < offset + prg_size + chr_size) {
        vcos_error("cartridge: truncated ROM (need %zu, have %zu)",
                   offset + prg_size + chr_size, size);
        return NULL;
    }

    cartridge *c = calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }

    c->mapper = mapper;
    c->mirroring = four_screen ? 1 : ((flags6 & 0x01) ? 1 : 0);
    c->prg_size = prg_size;
    c->chr_size = chr_size;
    c->prg_bank = 0;
    c->chr_bank = 0;
    c->mmc1_shift = 0x10;
    c->mmc1_control = 0x0C;
    c->mmc3_irq_latch = 0;
    c->mmc3_irq_counter = 0;

    c->prg = malloc(prg_size);
    if (!c->prg) {
        free(c);
        return NULL;
    }
    memcpy(c->prg, data + offset, prg_size);

    if (chr_size > 0) {
        c->chr = malloc(chr_size);
        if (!c->chr) {
            free(c->prg);
            free(c);
            return NULL;
        }
        memcpy(c->chr, data + offset + prg_size, chr_size);
        c->chr_is_ram = 0;
    } else {
        c->chr = calloc(1, 0x2000);
        if (!c->chr) {
            free(c->prg);
            free(c);
            return NULL;
        }
        c->chr_size = 0x2000;
        c->chr_is_ram = 1;
    }

    vcos_log("cartridge: mapper=%d prg=%zu chr=%zu mirroring=%s", c->mapper,
             c->prg_size, c->chr_size, c->mirroring ? "vertical" : "horizontal");
    return c;
}

cartridge *cart_from_memory(const uint8_t *data, size_t size)
{
    return cart_parse(data, size);
}

cartridge *cart_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        vcos_error("cartridge: cannot open %s", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len <= 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    cartridge *c = cart_parse(buf, (size_t)len);
    free(buf);
    if (c) {
        vcos_log("cartridge: loaded %s", path);
    }
    return c;
}

void cart_free(cartridge *c)
{
    if (!c) {
        return;
    }
    free(c->prg);
    free(c->chr);
    free(c);
}

static size_t prg_offset(cartridge *c, uint16_t addr)
{
    size_t banks8 = c->prg_size / 0x2000;
    if (banks8 == 0) {
        return 0;
    }
    if (c->mapper == 1) {
        size_t bank16 = c->prg_size / 0x4000;
        int mode = (c->mmc1_control >> 2) & 3;
        size_t bank;
        if (addr < 0xC000) {
            if (mode == 0 || mode == 1) {
                bank = (c->mmc1_prg & 0x0E) % (bank16 ? bank16 : 1);
            } else if (mode == 2) {
                bank = 0;
            } else {
                bank = c->mmc1_prg % (bank16 ? bank16 : 1);
            }
        } else {
            if (mode == 0 || mode == 1) {
                bank = ((c->mmc1_prg & 0x0E) + 1) % (bank16 ? bank16 : 1);
            } else if (mode == 2) {
                bank = c->mmc1_prg % (bank16 ? bank16 : 1);
            } else {
                bank = (bank16 ? bank16 - 1 : 0);
            }
        }
        return bank * 0x4000 + (addr & 0x3FFF);
    }
    if (c->mapper == 4) {
        size_t banks = banks8;
        size_t fixed_second_last = banks >= 2 ? banks - 2 : 0;
        size_t fixed_last = banks ? banks - 1 : 0;
        size_t slot = (addr - 0x8000) / 0x2000;
        size_t bank;
        int mode = (c->mmc3_select & 0x40) != 0;
        if (!mode) {
            bank = (slot == 0) ? c->mmc3_regs[6] :
                   (slot == 1) ? c->mmc3_regs[7] :
                   (slot == 2) ? fixed_second_last : fixed_last;
        } else {
            bank = (slot == 0) ? fixed_second_last :
                   (slot == 1) ? c->mmc3_regs[7] :
                   (slot == 2) ? c->mmc3_regs[6] : fixed_last;
        }
        return (bank % (banks ? banks : 1)) * 0x2000 + (addr & 0x1FFF);
    }
    if (c->mapper == 2) {
        if (addr < 0xC000) {
            return (size_t)c->prg_bank * 0x4000 + (addr - 0x8000);
        }
        return c->prg_size - 0x4000 + (addr - 0xC000);
    }
    size_t offset = (size_t)(addr - 0x8000);
    if (c->prg_size == 0x4000) {
        offset &= 0x3FFF;
    } else {
        offset &= 0x7FFF;
    }
    return offset;
}

uint8_t cart_read_prg(cartridge *c, uint16_t addr)
{
    if (!c || !c->prg || c->prg_size == 0) {
        return 0;
    }
    if (addr >= 0x6000 && addr < 0x8000) {
        return c->prg_ram[addr - 0x6000];
    }
    if (addr < 0x8000) {
        return 0;
    }
    return c->prg[prg_offset(c, addr) % c->prg_size];
}

void cart_write_prg(cartridge *c, uint16_t addr, uint8_t value)
{
    if (c && addr >= 0x6000 && addr < 0x8000) {
        c->prg_ram[addr - 0x6000] = value;
    }
}

uint8_t cart_read_chr(cartridge *c, uint16_t addr)
{
    if (!c || !c->chr || c->chr_size == 0) {
        return 0;
    }
    size_t offset = (size_t)(addr & 0x1FFF);
    size_t banks1 = c->chr_size / 0x400;
    if (c->mapper == 1) {
        if (c->mmc1_control & 0x10) {
            size_t bank = (offset < 0x1000) ? c->mmc1_chr0 : c->mmc1_chr1;
            offset = bank * 0x1000 + (offset & 0x0FFF);
        } else {
            offset = ((size_t)(c->mmc1_chr0 >> 1) * 0x2000) + (offset & 0x1FFF);
        }
    } else if (c->mapper == 4) {
        int mode = (c->mmc3_select & 0x80) != 0;
        int slot = (int)(offset / 0x400);
        size_t bank;
        if (!mode) {
            bank = (slot < 2) ? (size_t)c->mmc3_regs[0] * 2 + (size_t)slot :
                   (slot < 4) ? (size_t)c->mmc3_regs[1] * 2 + (size_t)(slot - 2) :
                   (size_t)c->mmc3_regs[2 + slot - 4];
        } else {
            bank = (slot < 2) ? c->mmc3_regs[2] + (size_t)slot :
                   (slot < 4) ? c->mmc3_regs[3] + (size_t)(slot - 2) :
                   (slot < 6) ? c->mmc3_regs[4] + (size_t)(slot - 4) :
                   (slot < 7) ? c->mmc3_regs[0] :
                   c->mmc3_regs[1];
        }
        offset = bank * 0x400 + (offset & 0x3FF);
    } else if (c->mapper == 3) {
        offset = ((size_t)(c->chr_bank & 0x0F) * 0x2000) + (offset & 0x1FFF);
    }
    if (banks1 == 0) {
        banks1 = 1;
    }
    return c->chr[offset % c->chr_size];
}

void cart_write_chr(cartridge *c, uint16_t addr, uint8_t value)
{
    if (!c || !c->chr || !c->chr_is_ram) {
        return;
    }
    c->chr[(addr & 0x1FFF) % c->chr_size] = value;
}

void cart_scanline(cartridge *c)
{
    if (!c || c->mapper != 4 || !c->mmc3_irq_enabled) {
        return;
    }
    if (c->mmc3_irq_counter == 0 || c->mmc3_irq_reload) {
        c->mmc3_irq_counter = c->mmc3_irq_latch;
        c->mmc3_irq_reload = 0;
    } else {
        c->mmc3_irq_counter--;
    }
    if (c->mmc3_irq_counter == 0) {
        c->mmc3_irq_pending = 1;
    }
}

void cart_write_mapper(cartridge *c, uint16_t addr, uint8_t value)
{
    if (!c) {
        return;
    }
    if (c->mapper == 1 && addr >= 0x8000) {
        if (value & 0x80) {
            c->mmc1_shift = 0x10;
            c->mmc1_control |= 0x0C;
            return;
        }
        int complete = (c->mmc1_shift & 1) != 0;
        c->mmc1_shift = (uint8_t)((c->mmc1_shift >> 1) | ((value & 1) << 4));
        if (!complete) {
            return;
        }
        uint8_t data = (uint8_t)(c->mmc1_shift & 0x1F);
        c->mmc1_shift = 0x10;
        switch ((addr >> 13) & 3) {
        case 0:
            c->mmc1_control = data;
            switch (data & 3) {
            case 0: c->mirroring = 2; break;
            case 1: c->mirroring = 3; break;
            case 2: c->mirroring = 1; break;
            default: c->mirroring = 0; break;
            }
            break;
        case 1:
            c->mmc1_chr0 = data;
            break;
        case 2:
            c->mmc1_chr1 = data;
            break;
        default:
            c->mmc1_prg = data & 0x1F;
            break;
        }
        return;
    }
    if (c->mapper == 4 && addr >= 0x8000) {
        if (addr < 0xA000) {
            if (!(addr & 1)) {
                c->mmc3_select = value;
            } else {
                c->mmc3_regs[c->mmc3_select & 7] = value;
            }
        } else if (addr < 0xC000) {
            if (!(addr & 1)) {
                c->mirroring = (value & 1) ? 0 : 1;
            }
        } else if (addr < 0xE000) {
            if (!(addr & 1)) {
                c->mmc3_irq_latch = value;
            } else {
                c->mmc3_irq_counter = 0;
                c->mmc3_irq_reload = 1;
            }
        } else {
            if (!(addr & 1)) {
                c->mmc3_irq_enabled = 0;
                c->mmc3_irq_pending = 0;
            } else {
                c->mmc3_irq_enabled = 1;
            }
        }
        return;
    }
    if (c->mapper == 2 && addr >= 0x8000) {
        size_t banks = c->prg_size / 0x4000;
        if (banks > 1) {
            c->prg_bank = (uint8_t)(value & (banks - 1));
        }
    } else if (c->mapper == 3 && addr >= 0x8000) {
        size_t banks = c->chr_size / 0x2000;
        if (banks > 1) {
            c->chr_bank = (uint8_t)(value & (banks - 1));
        }
    }
}

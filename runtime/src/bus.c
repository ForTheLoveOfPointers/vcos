#include "bus.h"

#include "apu.h"
#include "cartridge.h"
#include "input.h"
#include "ppu.h"
#include "services.h"

void bus_init(bus *b, cartridge *cart, ppu *p, apu *a, input *in, services *svc)
{
    b->cart = cart;
    b->ppu = p;
    b->apu = a;
    b->input = in;
    b->services = svc;
    b->nmi_pending = 0;
    b->irq_pending = 0;
    b->cycles = 0;
}

void bus_reset(bus *b)
{
    cpu_reset(&b->cpu, b);
    if (b->ppu) {
        ppu_reset(b->ppu);
    }
    if (b->apu) {
        apu_reset(b->apu);
    }
    if (b->input) {
        input_init(b->input);
    }
}

uint8_t bus_read(bus *b, uint16_t addr)
{
    if (addr < 0x2000) {
        return b->ram[addr & 0x07FF];
    }
    if (addr < 0x4000) {
        return b->ppu ? ppu_read_reg(b->ppu, (uint16_t)(addr & 0x0007)) : 0;
    }
    if (addr == 0x4015) {
        return b->apu ? apu_read_status(b->apu) : 0;
    }
    if (addr == 0x4016) {
        return b->input ? input_read(b->input, 0) : 0;
    }
    if (addr == 0x4017) {
        return b->input ? input_read(b->input, 1) : 0;
    }
    if (addr >= VCOS_SVC_BASE && addr < VCOS_SVC_BASE + 0x20 && b->services) {
        return services_read(b->services, addr);
    }
    if (addr >= 0x6000 && addr < 0x8000 && b->cart) {
        return cart_read_prg(b->cart, addr);
    }
    if (addr >= 0x8000 && b->cart) {
        return cart_read_prg(b->cart, addr);
    }
    return 0;
}

void bus_write(bus *b, uint16_t addr, uint8_t value)
{
    if (addr < 0x2000) {
        b->ram[addr & 0x07FF] = value;
        return;
    }
    if (addr < 0x4000) {
        if (b->ppu) {
            ppu_write_reg(b->ppu, (uint16_t)(addr & 0x0007), value);
        }
        return;
    }
    if (addr == 0x4014) {
        if (b->ppu) {
            uint16_t base = (uint16_t)(value << 8);
            for (int i = 0; i < 256; i++) {
                uint8_t v = bus_read(b, (uint16_t)(base + i));
                b->ppu->oam[(b->ppu->oam_addr + i) & 0xFF] = v;
            }
            b->ppu->oam_addr = (uint8_t)(b->ppu->oam_addr + 256);
        }
        return;
    }
    if (addr == 0x4016) {
        if (b->input) {
            input_write_strobe(b->input, value);
        }
        return;
    }
    if (addr >= 0x4000 && addr <= 0x4017) {
        if (b->apu) {
            apu_write(b->apu, addr, value);
        }
        return;
    }
    if (addr >= VCOS_SVC_BASE && addr < VCOS_SVC_BASE + 0x20) {
        if (b->services) {
            services_write(b->services, addr, value);
        }
        return;
    }
    if (addr >= 0x6000 && addr < 0x8000 && b->cart) {
        cart_write_prg(b->cart, addr, value);
        return;
    }
    if (addr >= 0x8000 && b->cart) {
        cart_write_mapper(b->cart, addr, value);
        return;
    }
}

void bus_tick(bus *b, int cycles)
{
    b->cycles += (uint64_t)cycles;
    if (b->ppu) {
        ppu_step(b->ppu, cycles * 3);
        if (b->ppu->nmi_pending) {
            b->ppu->nmi_pending = 0;
            b->nmi_pending = 1;
        }
    }
    if (b->apu) {
        apu_step(b->apu, cycles);
    }
    if (b->cart && b->cart->mmc3_irq_pending) {
        b->cart->mmc3_irq_pending = 0;
        b->irq_pending = 1;
    }
}

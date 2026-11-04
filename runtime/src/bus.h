#ifndef VCOS_BUS_H
#define VCOS_BUS_H

#include <stdint.h>

#include "cpu6502.h"

typedef struct cartridge cartridge;
typedef struct ppu ppu;
typedef struct apu apu;
typedef struct input input;
typedef struct services services;

typedef struct bus {
    cpu6502 cpu;
    uint8_t ram[2048];
    cartridge *cart;
    ppu *ppu;
    apu *apu;
    input *input;
    services *services;
    int nmi_pending;
    int irq_pending;
    uint64_t cycles;
} bus;

void bus_init(bus *b, cartridge *cart, ppu *p, apu *a, input *in, services *svc);
void bus_reset(bus *b);
uint8_t bus_read(bus *b, uint16_t addr);
void bus_write(bus *b, uint16_t addr, uint8_t value);
void bus_tick(bus *b, int cycles);

#endif

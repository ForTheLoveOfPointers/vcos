#ifndef VCOS_CPU6502_H
#define VCOS_CPU6502_H

#include <stdint.h>

typedef struct bus bus;

typedef struct cpu6502 {
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t s;
    uint8_t p;
    uint16_t pc;
    int jammed;
} cpu6502;

#define CPU_FLAG_C 0x01
#define CPU_FLAG_Z 0x02
#define CPU_FLAG_I 0x04
#define CPU_FLAG_D 0x08
#define CPU_FLAG_B 0x10
#define CPU_FLAG_U 0x20
#define CPU_FLAG_V 0x40
#define CPU_FLAG_N 0x80

void cpu_reset(cpu6502 *cpu, bus *b);
int cpu_step(cpu6502 *cpu, bus *b);
void cpu_nmi(cpu6502 *cpu, bus *b);
void cpu_irq(cpu6502 *cpu, bus *b);

#endif

#include "cpu6502.h"

#include <stddef.h>

#include "bus.h"

static uint8_t fetch8(cpu6502 *c, bus *b)
{
    uint8_t v = bus_read(b, c->pc);
    c->pc++;
    return v;
}

static uint16_t fetch16(cpu6502 *c, bus *b)
{
    uint8_t lo = fetch8(c, b);
    uint8_t hi = fetch8(c, b);
    return (uint16_t)(lo | (hi << 8));
}

static void push(cpu6502 *c, bus *b, uint8_t v)
{
    bus_write(b, (uint16_t)(0x0100 | c->s), v);
    c->s--;
}

static uint8_t pull(cpu6502 *c, bus *b)
{
    c->s++;
    return bus_read(b, (uint16_t)(0x0100 | c->s));
}

static void set_zn(cpu6502 *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(CPU_FLAG_Z | CPU_FLAG_N)) |
                     (v == 0 ? CPU_FLAG_Z : 0) | (v & CPU_FLAG_N));
}

static uint16_t addr_zp(cpu6502 *c, bus *b)
{
    return fetch8(c, b);
}

static uint16_t addr_zpx(cpu6502 *c, bus *b)
{
    return (uint16_t)((fetch8(c, b) + c->x) & 0xFF);
}

static uint16_t addr_zpy(cpu6502 *c, bus *b)
{
    return (uint16_t)((fetch8(c, b) + c->y) & 0xFF);
}

static uint16_t addr_abs(cpu6502 *c, bus *b)
{
    return fetch16(c, b);
}

static uint16_t addr_absx(cpu6502 *c, bus *b, int *cross)
{
    uint16_t base = fetch16(c, b);
    uint16_t addr = (uint16_t)(base + c->x);
    if (cross) {
        *cross = ((base & 0xFF00) != (addr & 0xFF00));
    }
    return addr;
}

static uint16_t addr_absy(cpu6502 *c, bus *b, int *cross)
{
    uint16_t base = fetch16(c, b);
    uint16_t addr = (uint16_t)(base + c->y);
    if (cross) {
        *cross = ((base & 0xFF00) != (addr & 0xFF00));
    }
    return addr;
}

static uint16_t addr_indx(cpu6502 *c, bus *b)
{
    uint8_t zp = (uint8_t)(fetch8(c, b) + c->x);
    uint8_t lo = bus_read(b, zp);
    uint8_t hi = bus_read(b, (uint8_t)(zp + 1));
    return (uint16_t)(lo | (hi << 8));
}

static uint16_t addr_indy(cpu6502 *c, bus *b, int *cross)
{
    uint8_t zp = fetch8(c, b);
    uint16_t base = (uint16_t)(bus_read(b, zp) | (bus_read(b, (uint8_t)(zp + 1)) << 8));
    uint16_t addr = (uint16_t)(base + c->y);
    if (cross) {
        *cross = ((base & 0xFF00) != (addr & 0xFF00));
    }
    return addr;
}

static int branch(cpu6502 *c, bus *b, int condition)
{
    int8_t offset = (int8_t)fetch8(c, b);
    if (!condition) {
        return 2;
    }
    uint16_t old_pc = c->pc;
    c->pc = (uint16_t)(c->pc + offset);
    return 3 + (((old_pc & 0xFF00) != (c->pc & 0xFF00)) ? 1 : 0);
}

static void adc(cpu6502 *c, uint8_t m)
{
    unsigned sum = (unsigned)c->a + (unsigned)m + (unsigned)(c->p & CPU_FLAG_C);
    uint8_t result = (uint8_t)sum;
    c->p = (uint8_t)((c->p & ~(CPU_FLAG_C | CPU_FLAG_V)) |
                     (sum > 0xFF ? CPU_FLAG_C : 0));
    if (((~(c->a ^ m)) & (c->a ^ result) & 0x80) != 0) {
        c->p |= CPU_FLAG_V;
    }
    c->a = result;
    set_zn(c, c->a);
}

static void sbc(cpu6502 *c, uint8_t m)
{
    unsigned diff = (unsigned)c->a - (unsigned)m - (unsigned)((c->p & CPU_FLAG_C) ? 0 : 1);
    uint8_t result = (uint8_t)diff;
    c->p = (uint8_t)((c->p & ~(CPU_FLAG_C | CPU_FLAG_V)) |
                     (diff < 0x100 ? CPU_FLAG_C : 0));
    if (((c->a ^ m) & (c->a ^ result) & 0x80) != 0) {
        c->p |= CPU_FLAG_V;
    }
    c->a = result;
    set_zn(c, c->a);
}

static void compare(cpu6502 *c, uint8_t reg, uint8_t m)
{
    unsigned diff = (unsigned)reg - (unsigned)m;
    c->p = (uint8_t)((c->p & ~CPU_FLAG_C) | (reg >= m ? CPU_FLAG_C : 0));
    set_zn(c, (uint8_t)diff);
}

static uint8_t asl(cpu6502 *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~CPU_FLAG_C) | ((v & 0x80) ? CPU_FLAG_C : 0));
    v = (uint8_t)(v << 1);
    set_zn(c, v);
    return v;
}

static uint8_t lsr(cpu6502 *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~CPU_FLAG_C) | ((v & 0x01) ? CPU_FLAG_C : 0));
    v = (uint8_t)(v >> 1);
    set_zn(c, v);
    return v;
}

static uint8_t rol(cpu6502 *c, uint8_t v)
{
    uint8_t carry = (uint8_t)(c->p & CPU_FLAG_C);
    c->p = (uint8_t)((c->p & ~CPU_FLAG_C) | ((v & 0x80) ? CPU_FLAG_C : 0));
    v = (uint8_t)((v << 1) | carry);
    set_zn(c, v);
    return v;
}

static uint8_t ror(cpu6502 *c, uint8_t v)
{
    uint8_t carry = (uint8_t)((c->p & CPU_FLAG_C) ? 0x80 : 0);
    c->p = (uint8_t)((c->p & ~CPU_FLAG_C) | ((v & 0x01) ? CPU_FLAG_C : 0));
    v = (uint8_t)((v >> 1) | carry);
    set_zn(c, v);
    return v;
}

static void do_interrupt(cpu6502 *c, bus *b, uint16_t vector, int brk)
{
    push(c, b, (uint8_t)(c->pc >> 8));
    push(c, b, (uint8_t)(c->pc & 0xFF));
    uint8_t status = (uint8_t)(c->p | CPU_FLAG_U);
    if (brk) {
        status |= CPU_FLAG_B;
    } else {
        status &= (uint8_t)~CPU_FLAG_B;
    }
    push(c, b, status);
    c->p |= CPU_FLAG_I;
    c->pc = (uint16_t)(bus_read(b, vector) | (bus_read(b, (uint16_t)(vector + 1)) << 8));
}

void cpu_reset(cpu6502 *c, bus *b)
{
    c->a = 0;
    c->x = 0;
    c->y = 0;
    c->s = 0xFD;
    c->p = CPU_FLAG_I | CPU_FLAG_U;
    c->jammed = 0;
    c->pc = (uint16_t)(bus_read(b, 0xFFFC) | (bus_read(b, 0xFFFD) << 8));
}

void cpu_nmi(cpu6502 *c, bus *b)
{
    do_interrupt(c, b, 0xFFFA, 0);
}

void cpu_irq(cpu6502 *c, bus *b)
{
    if (c->p & CPU_FLAG_I) {
        return;
    }
    do_interrupt(c, b, 0xFFFE, 0);
}

int cpu_step(cpu6502 *c, bus *b)
{
    uint8_t opcode = fetch8(c, b);
    uint16_t addr = 0;
    uint8_t m = 0;
    int cross = 0;
    int cycles = 2;

    switch (opcode) {
    case 0x69: /* ADC #imm */
        adc(c, fetch8(c, b));
        cycles = 2;
        break;
    case 0x65: /* ADC zp */
        adc(c, bus_read(b, addr_zp(c, b)));
        cycles = 3;
        break;
    case 0x75: /* ADC zp,x */
        adc(c, bus_read(b, addr_zpx(c, b)));
        cycles = 4;
        break;
    case 0x6D: /* ADC abs */
        adc(c, bus_read(b, addr_abs(c, b)));
        cycles = 4;
        break;
    case 0x7D: /* ADC abs,x */
        addr = addr_absx(c, b, &cross);
        adc(c, bus_read(b, addr));
        cycles = 4 + cross;
        break;
    case 0x79: /* ADC abs,y */
        addr = addr_absy(c, b, &cross);
        adc(c, bus_read(b, addr));
        cycles = 4 + cross;
        break;
    case 0x61: /* ADC (zp,x) */
        adc(c, bus_read(b, addr_indx(c, b)));
        cycles = 6;
        break;
    case 0x71: /* ADC (zp),y */
        addr = addr_indy(c, b, &cross);
        adc(c, bus_read(b, addr));
        cycles = 5 + cross;
        break;

    case 0x29: /* AND #imm */
        c->a &= fetch8(c, b);
        set_zn(c, c->a);
        cycles = 2;
        break;
    case 0x25:
        c->a &= bus_read(b, addr_zp(c, b));
        set_zn(c, c->a);
        cycles = 3;
        break;
    case 0x35:
        c->a &= bus_read(b, addr_zpx(c, b));
        set_zn(c, c->a);
        cycles = 4;
        break;
    case 0x2D:
        c->a &= bus_read(b, addr_abs(c, b));
        set_zn(c, c->a);
        cycles = 4;
        break;
    case 0x3D:
        addr = addr_absx(c, b, &cross);
        c->a &= bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 4 + cross;
        break;
    case 0x39:
        addr = addr_absy(c, b, &cross);
        c->a &= bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 4 + cross;
        break;
    case 0x21:
        c->a &= bus_read(b, addr_indx(c, b));
        set_zn(c, c->a);
        cycles = 6;
        break;
    case 0x31:
        addr = addr_indy(c, b, &cross);
        c->a &= bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 5 + cross;
        break;

    case 0x0A: /* ASL A */
        c->a = asl(c, c->a);
        cycles = 2;
        break;
    case 0x06:
        addr = addr_zp(c, b);
        bus_write(b, addr, asl(c, bus_read(b, addr)));
        cycles = 5;
        break;
    case 0x16:
        addr = addr_zpx(c, b);
        bus_write(b, addr, asl(c, bus_read(b, addr)));
        cycles = 6;
        break;
    case 0x0E:
        addr = addr_abs(c, b);
        bus_write(b, addr, asl(c, bus_read(b, addr)));
        cycles = 6;
        break;
    case 0x1E:
        addr = addr_absx(c, b, NULL);
        bus_write(b, addr, asl(c, bus_read(b, addr)));
        cycles = 7;
        break;

    case 0x90: cycles = branch(c, b, !(c->p & CPU_FLAG_C)); break;
    case 0xB0: cycles = branch(c, b, (c->p & CPU_FLAG_C) != 0); break;
    case 0xF0: cycles = branch(c, b, (c->p & CPU_FLAG_Z) != 0); break;
    case 0x30: cycles = branch(c, b, (c->p & CPU_FLAG_N) != 0); break;
    case 0xD0: cycles = branch(c, b, (c->p & CPU_FLAG_Z) == 0); break;
    case 0x10: cycles = branch(c, b, (c->p & CPU_FLAG_N) == 0); break;
    case 0x50: cycles = branch(c, b, (c->p & CPU_FLAG_V) == 0); break;
    case 0x70: cycles = branch(c, b, (c->p & CPU_FLAG_V) != 0); break;

    case 0x24: /* BIT zp */
        m = bus_read(b, addr_zp(c, b));
        c->p = (uint8_t)((c->p & ~(CPU_FLAG_Z | CPU_FLAG_N | CPU_FLAG_V)) |
                         ((c->a & m) == 0 ? CPU_FLAG_Z : 0) |
                         (m & (CPU_FLAG_N | CPU_FLAG_V)));
        cycles = 3;
        break;
    case 0x2C: /* BIT abs */
        m = bus_read(b, addr_abs(c, b));
        c->p = (uint8_t)((c->p & ~(CPU_FLAG_Z | CPU_FLAG_N | CPU_FLAG_V)) |
                         ((c->a & m) == 0 ? CPU_FLAG_Z : 0) |
                         (m & (CPU_FLAG_N | CPU_FLAG_V)));
        cycles = 4;
        break;

    case 0x00: /* BRK */
        c->pc++;
        do_interrupt(c, b, 0xFFFE, 1);
        cycles = 7;
        break;

    case 0x18: c->p &= (uint8_t)~CPU_FLAG_C; cycles = 2; break;
    case 0xD8: c->p &= (uint8_t)~CPU_FLAG_D; cycles = 2; break;
    case 0x58: c->p &= (uint8_t)~CPU_FLAG_I; cycles = 2; break;
    case 0xB8: c->p &= (uint8_t)~CPU_FLAG_V; cycles = 2; break;
    case 0x38: c->p |= CPU_FLAG_C; cycles = 2; break;
    case 0xF8: c->p |= CPU_FLAG_D; cycles = 2; break;
    case 0x78: c->p |= CPU_FLAG_I; cycles = 2; break;

    case 0xC9: compare(c, c->a, fetch8(c, b)); cycles = 2; break;
    case 0xC5: compare(c, c->a, bus_read(b, addr_zp(c, b))); cycles = 3; break;
    case 0xD5: compare(c, c->a, bus_read(b, addr_zpx(c, b))); cycles = 4; break;
    case 0xCD: compare(c, c->a, bus_read(b, addr_abs(c, b))); cycles = 4; break;
    case 0xDD:
        addr = addr_absx(c, b, &cross);
        compare(c, c->a, bus_read(b, addr));
        cycles = 4 + cross;
        break;
    case 0xD9:
        addr = addr_absy(c, b, &cross);
        compare(c, c->a, bus_read(b, addr));
        cycles = 4 + cross;
        break;
    case 0xC1: compare(c, c->a, bus_read(b, addr_indx(c, b))); cycles = 6; break;
    case 0xD1:
        addr = addr_indy(c, b, &cross);
        compare(c, c->a, bus_read(b, addr));
        cycles = 5 + cross;
        break;

    case 0xE0: compare(c, c->x, fetch8(c, b)); cycles = 2; break;
    case 0xE4: compare(c, c->x, bus_read(b, addr_zp(c, b))); cycles = 3; break;
    case 0xEC: compare(c, c->x, bus_read(b, addr_abs(c, b))); cycles = 4; break;
    case 0xC0: compare(c, c->y, fetch8(c, b)); cycles = 2; break;
    case 0xC4: compare(c, c->y, bus_read(b, addr_zp(c, b))); cycles = 3; break;
    case 0xCC: compare(c, c->y, bus_read(b, addr_abs(c, b))); cycles = 4; break;

    case 0xC6:
        addr = addr_zp(c, b);
        m = (uint8_t)(bus_read(b, addr) - 1);
        bus_write(b, addr, m);
        set_zn(c, m);
        cycles = 5;
        break;
    case 0xD6:
        addr = addr_zpx(c, b);
        m = (uint8_t)(bus_read(b, addr) - 1);
        bus_write(b, addr, m);
        set_zn(c, m);
        cycles = 6;
        break;
    case 0xCE:
        addr = addr_abs(c, b);
        m = (uint8_t)(bus_read(b, addr) - 1);
        bus_write(b, addr, m);
        set_zn(c, m);
        cycles = 6;
        break;
    case 0xDE:
        addr = addr_absx(c, b, NULL);
        m = (uint8_t)(bus_read(b, addr) - 1);
        bus_write(b, addr, m);
        set_zn(c, m);
        cycles = 7;
        break;

    case 0xCA: c->x--; set_zn(c, c->x); cycles = 2; break;
    case 0x88: c->y--; set_zn(c, c->y); cycles = 2; break;

    case 0x49:
        c->a ^= fetch8(c, b);
        set_zn(c, c->a);
        cycles = 2;
        break;
    case 0x45:
        c->a ^= bus_read(b, addr_zp(c, b));
        set_zn(c, c->a);
        cycles = 3;
        break;
    case 0x55:
        c->a ^= bus_read(b, addr_zpx(c, b));
        set_zn(c, c->a);
        cycles = 4;
        break;
    case 0x4D:
        c->a ^= bus_read(b, addr_abs(c, b));
        set_zn(c, c->a);
        cycles = 4;
        break;
    case 0x5D:
        addr = addr_absx(c, b, &cross);
        c->a ^= bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 4 + cross;
        break;
    case 0x59:
        addr = addr_absy(c, b, &cross);
        c->a ^= bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 4 + cross;
        break;
    case 0x41:
        c->a ^= bus_read(b, addr_indx(c, b));
        set_zn(c, c->a);
        cycles = 6;
        break;
    case 0x51:
        addr = addr_indy(c, b, &cross);
        c->a ^= bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 5 + cross;
        break;

    case 0xE6:
        addr = addr_zp(c, b);
        m = (uint8_t)(bus_read(b, addr) + 1);
        bus_write(b, addr, m);
        set_zn(c, m);
        cycles = 5;
        break;
    case 0xF6:
        addr = addr_zpx(c, b);
        m = (uint8_t)(bus_read(b, addr) + 1);
        bus_write(b, addr, m);
        set_zn(c, m);
        cycles = 6;
        break;
    case 0xEE:
        addr = addr_abs(c, b);
        m = (uint8_t)(bus_read(b, addr) + 1);
        bus_write(b, addr, m);
        set_zn(c, m);
        cycles = 6;
        break;
    case 0xFE:
        addr = addr_absx(c, b, NULL);
        m = (uint8_t)(bus_read(b, addr) + 1);
        bus_write(b, addr, m);
        set_zn(c, m);
        cycles = 7;
        break;

    case 0xE8: c->x++; set_zn(c, c->x); cycles = 2; break;
    case 0xC8: c->y++; set_zn(c, c->y); cycles = 2; break;

    case 0x4C: /* JMP abs */
        c->pc = addr_abs(c, b);
        cycles = 3;
        break;
    case 0x6C: { /* JMP indirect, with 6502 page-wrap bug */
        uint16_t ptr = addr_abs(c, b);
        uint8_t lo = bus_read(b, ptr);
        uint8_t hi = bus_read(b, (uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0x00FF)));
        c->pc = (uint16_t)(lo | (hi << 8));
        cycles = 5;
        break;
    }
    case 0x20: { /* JSR */
        uint16_t target = addr_abs(c, b);
        push(c, b, (uint8_t)((c->pc - 1) >> 8));
        push(c, b, (uint8_t)((c->pc - 1) & 0xFF));
        c->pc = target;
        cycles = 6;
        break;
    }

    case 0xA9:
        c->a = fetch8(c, b);
        set_zn(c, c->a);
        cycles = 2;
        break;
    case 0xA5:
        c->a = bus_read(b, addr_zp(c, b));
        set_zn(c, c->a);
        cycles = 3;
        break;
    case 0xB5:
        c->a = bus_read(b, addr_zpx(c, b));
        set_zn(c, c->a);
        cycles = 4;
        break;
    case 0xAD:
        c->a = bus_read(b, addr_abs(c, b));
        set_zn(c, c->a);
        cycles = 4;
        break;
    case 0xBD:
        addr = addr_absx(c, b, &cross);
        c->a = bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 4 + cross;
        break;
    case 0xB9:
        addr = addr_absy(c, b, &cross);
        c->a = bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 4 + cross;
        break;
    case 0xA1:
        c->a = bus_read(b, addr_indx(c, b));
        set_zn(c, c->a);
        cycles = 6;
        break;
    case 0xB1:
        addr = addr_indy(c, b, &cross);
        c->a = bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 5 + cross;
        break;

    case 0xA2:
        c->x = fetch8(c, b);
        set_zn(c, c->x);
        cycles = 2;
        break;
    case 0xA6:
        c->x = bus_read(b, addr_zp(c, b));
        set_zn(c, c->x);
        cycles = 3;
        break;
    case 0xB6:
        c->x = bus_read(b, addr_zpy(c, b));
        set_zn(c, c->x);
        cycles = 4;
        break;
    case 0xAE:
        c->x = bus_read(b, addr_abs(c, b));
        set_zn(c, c->x);
        cycles = 4;
        break;
    case 0xBE:
        addr = addr_absy(c, b, &cross);
        c->x = bus_read(b, addr);
        set_zn(c, c->x);
        cycles = 4 + cross;
        break;

    case 0xA0:
        c->y = fetch8(c, b);
        set_zn(c, c->y);
        cycles = 2;
        break;
    case 0xA4:
        c->y = bus_read(b, addr_zp(c, b));
        set_zn(c, c->y);
        cycles = 3;
        break;
    case 0xB4:
        c->y = bus_read(b, addr_zpx(c, b));
        set_zn(c, c->y);
        cycles = 4;
        break;
    case 0xAC:
        c->y = bus_read(b, addr_abs(c, b));
        set_zn(c, c->y);
        cycles = 4;
        break;
    case 0xBC:
        addr = addr_absx(c, b, &cross);
        c->y = bus_read(b, addr);
        set_zn(c, c->y);
        cycles = 4 + cross;
        break;

    case 0x4A: c->a = lsr(c, c->a); cycles = 2; break;
    case 0x46:
        addr = addr_zp(c, b);
        bus_write(b, addr, lsr(c, bus_read(b, addr)));
        cycles = 5;
        break;
    case 0x56:
        addr = addr_zpx(c, b);
        bus_write(b, addr, lsr(c, bus_read(b, addr)));
        cycles = 6;
        break;
    case 0x4E:
        addr = addr_abs(c, b);
        bus_write(b, addr, lsr(c, bus_read(b, addr)));
        cycles = 6;
        break;
    case 0x5E:
        addr = addr_absx(c, b, NULL);
        bus_write(b, addr, lsr(c, bus_read(b, addr)));
        cycles = 7;
        break;

    case 0xEA: cycles = 2; break;

    case 0x09:
        c->a |= fetch8(c, b);
        set_zn(c, c->a);
        cycles = 2;
        break;
    case 0x05:
        c->a |= bus_read(b, addr_zp(c, b));
        set_zn(c, c->a);
        cycles = 3;
        break;
    case 0x15:
        c->a |= bus_read(b, addr_zpx(c, b));
        set_zn(c, c->a);
        cycles = 4;
        break;
    case 0x0D:
        c->a |= bus_read(b, addr_abs(c, b));
        set_zn(c, c->a);
        cycles = 4;
        break;
    case 0x1D:
        addr = addr_absx(c, b, &cross);
        c->a |= bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 4 + cross;
        break;
    case 0x19:
        addr = addr_absy(c, b, &cross);
        c->a |= bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 4 + cross;
        break;
    case 0x01:
        c->a |= bus_read(b, addr_indx(c, b));
        set_zn(c, c->a);
        cycles = 6;
        break;
    case 0x11:
        addr = addr_indy(c, b, &cross);
        c->a |= bus_read(b, addr);
        set_zn(c, c->a);
        cycles = 5 + cross;
        break;

    case 0x48: push(c, b, c->a); cycles = 3; break;
    case 0x08: push(c, b, (uint8_t)(c->p | CPU_FLAG_B | CPU_FLAG_U)); cycles = 3; break;
    case 0x68:
        c->a = pull(c, b);
        set_zn(c, c->a);
        cycles = 4;
        break;
    case 0x28:
        c->p = (uint8_t)((pull(c, b) & ~CPU_FLAG_B) | CPU_FLAG_U);
        cycles = 4;
        break;

    case 0x2A: c->a = rol(c, c->a); cycles = 2; break;
    case 0x26:
        addr = addr_zp(c, b);
        bus_write(b, addr, rol(c, bus_read(b, addr)));
        cycles = 5;
        break;
    case 0x36:
        addr = addr_zpx(c, b);
        bus_write(b, addr, rol(c, bus_read(b, addr)));
        cycles = 6;
        break;
    case 0x2E:
        addr = addr_abs(c, b);
        bus_write(b, addr, rol(c, bus_read(b, addr)));
        cycles = 6;
        break;
    case 0x3E:
        addr = addr_absx(c, b, NULL);
        bus_write(b, addr, rol(c, bus_read(b, addr)));
        cycles = 7;
        break;

    case 0x6A: c->a = ror(c, c->a); cycles = 2; break;
    case 0x66:
        addr = addr_zp(c, b);
        bus_write(b, addr, ror(c, bus_read(b, addr)));
        cycles = 5;
        break;
    case 0x76:
        addr = addr_zpx(c, b);
        bus_write(b, addr, ror(c, bus_read(b, addr)));
        cycles = 6;
        break;
    case 0x6E:
        addr = addr_abs(c, b);
        bus_write(b, addr, ror(c, bus_read(b, addr)));
        cycles = 6;
        break;
    case 0x7E:
        addr = addr_absx(c, b, NULL);
        bus_write(b, addr, ror(c, bus_read(b, addr)));
        cycles = 7;
        break;

    case 0x40: /* RTI */
        c->p = (uint8_t)((pull(c, b) & ~CPU_FLAG_B) | CPU_FLAG_U);
        {
            uint8_t lo = pull(c, b);
            uint8_t hi = pull(c, b);
            c->pc = (uint16_t)(lo | (hi << 8));
        }
        cycles = 6;
        break;
    case 0x60: { /* RTS */
        uint8_t lo = pull(c, b);
        uint8_t hi = pull(c, b);
        c->pc = (uint16_t)((lo | (hi << 8)) + 1);
        cycles = 6;
        break;
    }

    case 0xE9:
        sbc(c, fetch8(c, b));
        cycles = 2;
        break;
    case 0xE5:
        sbc(c, bus_read(b, addr_zp(c, b)));
        cycles = 3;
        break;
    case 0xF5:
        sbc(c, bus_read(b, addr_zpx(c, b)));
        cycles = 4;
        break;
    case 0xED:
        sbc(c, bus_read(b, addr_abs(c, b)));
        cycles = 4;
        break;
    case 0xFD:
        addr = addr_absx(c, b, &cross);
        sbc(c, bus_read(b, addr));
        cycles = 4 + cross;
        break;
    case 0xF9:
        addr = addr_absy(c, b, &cross);
        sbc(c, bus_read(b, addr));
        cycles = 4 + cross;
        break;
    case 0xE1:
        sbc(c, bus_read(b, addr_indx(c, b)));
        cycles = 6;
        break;
    case 0xF1:
        addr = addr_indy(c, b, &cross);
        sbc(c, bus_read(b, addr));
        cycles = 5 + cross;
        break;

    case 0x85: bus_write(b, addr_zp(c, b), c->a); cycles = 3; break;
    case 0x95: bus_write(b, addr_zpx(c, b), c->a); cycles = 4; break;
    case 0x8D: bus_write(b, addr_abs(c, b), c->a); cycles = 4; break;
    case 0x9D: bus_write(b, addr_absx(c, b, NULL), c->a); cycles = 5; break;
    case 0x99: bus_write(b, addr_absy(c, b, NULL), c->a); cycles = 5; break;
    case 0x81: bus_write(b, addr_indx(c, b), c->a); cycles = 6; break;
    case 0x91: bus_write(b, addr_indy(c, b, NULL), c->a); cycles = 6; break;

    case 0x86: bus_write(b, addr_zp(c, b), c->x); cycles = 3; break;
    case 0x96: bus_write(b, addr_zpy(c, b), c->x); cycles = 4; break;
    case 0x8E: bus_write(b, addr_abs(c, b), c->x); cycles = 4; break;

    case 0x84: bus_write(b, addr_zp(c, b), c->y); cycles = 3; break;
    case 0x94: bus_write(b, addr_zpx(c, b), c->y); cycles = 4; break;
    case 0x8C: bus_write(b, addr_abs(c, b), c->y); cycles = 4; break;

    case 0xAA: c->x = c->a; set_zn(c, c->x); cycles = 2; break;
    case 0xA8: c->y = c->a; set_zn(c, c->y); cycles = 2; break;
    case 0xBA: c->x = c->s; set_zn(c, c->x); cycles = 2; break;
    case 0x8A: c->a = c->x; set_zn(c, c->a); cycles = 2; break;
    case 0x9A: c->s = c->x; cycles = 2; break;
    case 0x98: c->a = c->y; set_zn(c, c->a); cycles = 2; break;

    default:
        c->jammed = 0;
        cycles = 2;
        break;
    }

    return cycles;
}

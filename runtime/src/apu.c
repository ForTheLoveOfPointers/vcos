#include "apu.h"

#include <string.h>

#define CPU_CLOCK 1789773u

static const uint8_t duty_table[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 0, 0, 0, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 0, 0},
};

static const uint8_t triangle_table[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

static void reset_envelope(apu *a, int channel)
{
    a->env_vol[channel] = 15;
    a->env_phase[channel] = 0;
}

void apu_init(apu *a)
{
    memset(a, 0, sizeof(*a));
    a->noise_lfsr = 1;
    reset_envelope(a, 0);
    reset_envelope(a, 1);
}

void apu_reset(apu *a)
{
    memset(a->regs, 0, sizeof(a->regs));
    a->status = 0;
    a->cycles = 0;
    a->phase[0] = a->phase[1] = 0;
    a->tri_phase = 0;
    a->noise_phase = 0;
    a->noise_lfsr = 1;
    reset_envelope(a, 0);
    reset_envelope(a, 1);
}

static void update_status(apu *a)
{
    a->status = (uint8_t)(a->regs[0x15] & 0x0F);
}

void apu_write(apu *a, uint16_t addr, uint8_t value)
{
    if (addr < 0x4000 || addr > 0x4017) {
        return;
    }
    a->regs[addr - 0x4000] = value;
    if (addr == 0x4003 || addr == 0x4007) {
        int ch = (addr == 0x4003) ? 0 : 1;
        if (!(a->regs[ch * 4] & 0x20)) {
            reset_envelope(a, ch);
        }
    }
    if (addr == 0x4015) {
        update_status(a);
    }
}

uint8_t apu_read_status(apu *a)
{
    return a->status;
}

void apu_step(apu *a, int cycles)
{
    a->cycles += (uint64_t)cycles;
}

static uint8_t envelope(apu *a, int ch, uint8_t reg, int sample_rate)
{
    if (reg & 0x10) {
        return (uint8_t)(reg & 0x0F);
    }
    a->env_phase[ch] += 240u;
    if (a->env_phase[ch] >= (uint32_t)sample_rate) {
        a->env_phase[ch] -= (uint32_t)sample_rate;
        if (a->env_vol[ch] > 0) {
            a->env_vol[ch]--;
        }
    }
    return a->env_vol[ch];
}

void apu_render(apu *a, int16_t *samples, int count, int sample_rate)
{
    if (!samples || count <= 0 || sample_rate <= 0) {
        return;
    }
    uint32_t timer1 = (uint32_t)(a->regs[2] | ((a->regs[3] & 0x07) << 8));
    uint32_t timer2 = (uint32_t)(a->regs[6] | ((a->regs[7] & 0x07) << 8));
    uint32_t timer3 = (uint32_t)(a->regs[10] | ((a->regs[11] & 0x07) << 8));
    uint32_t timer4 = (uint32_t)(a->regs[14] | ((a->regs[15] & 0x07) << 8));
    uint32_t duty1 = (a->regs[0] >> 6) & 3;
    uint32_t duty2 = (a->regs[4] >> 6) & 3;
    int pulse1_on = (a->regs[0x15] & 0x01) != 0;
    int pulse2_on = (a->regs[0x15] & 0x02) != 0;
    int tri_on = (a->regs[0x15] & 0x04) != 0;
    int noise_on = (a->regs[0x15] & 0x08) != 0;
    uint32_t f1 = CPU_CLOCK / (16u * (timer1 + 1u));
    uint32_t f2 = CPU_CLOCK / (16u * (timer2 + 1u));
    uint32_t f3 = CPU_CLOCK / (32u * (timer3 + 1u));
    uint32_t noise_period = (timer4 < 8) ? (timer4 + 1u) : 8u;
    uint32_t noise_freq = CPU_CLOCK / (16u * noise_period);

    for (int i = 0; i < count; i++) {
        int p1 = 0;
        int p2 = 0;
        int tri = 0;
        int noise = 0;
        if (pulse1_on && (a->regs[0] & 0x0F)) {
            a->phase[0] += (uint32_t)(((uint64_t)f1 * 8u * 65536u) / (uint32_t)sample_rate);
            uint32_t idx = (a->phase[0] >> 16) & 7;
            p1 = duty_table[duty1][idx] ? envelope(a, 0, a->regs[0], sample_rate) : 0;
        }
        if (pulse2_on && (a->regs[4] & 0x0F)) {
            a->phase[1] += (uint32_t)(((uint64_t)f2 * 8u * 65536u) / (uint32_t)sample_rate);
            uint32_t idx = (a->phase[1] >> 16) & 7;
            p2 = duty_table[duty2][idx] ? envelope(a, 1, a->regs[4], sample_rate) : 0;
        }
        if (tri_on) {
            a->tri_phase += (uint32_t)(((uint64_t)f3 * 32u * 65536u) / (uint32_t)sample_rate);
            tri = triangle_table[(a->tri_phase >> 16) & 31];
            if ((a->regs[8] & 0x7F) == 0) {
                tri = 0;
            }
        }
        if (noise_on && !(a->regs[12] & 0x10)) {
            a->noise_phase += (uint32_t)(((uint64_t)noise_freq * 65536u) / (uint32_t)sample_rate);
            if (a->noise_phase >= 65536u) {
                a->noise_phase -= 65536u;
                uint32_t bit = (a->noise_lfsr ^ (a->noise_lfsr >> 1)) & 1u;
                a->noise_lfsr = (a->noise_lfsr >> 1) | (bit << 14);
            }
            noise = (a->noise_lfsr & 1u) ? (int)(a->regs[12] & 0x0F) : 0;
        }
        int32_t mix = p1 * 900 + p2 * 900 + tri * 700 + noise * 500;
        if (mix > 32767) {
            mix = 32767;
        }
        if (mix < -32768) {
            mix = -32768;
        }
        samples[i] = (int16_t)mix;
    }
}

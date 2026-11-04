#ifndef VCOS_APU_H
#define VCOS_APU_H

#include <stdint.h>

typedef struct apu {
    uint8_t regs[0x18];
    uint8_t status;
    uint64_t cycles;
    uint32_t phase[2];
    uint32_t tri_phase;
    uint32_t noise_phase;
    uint32_t noise_lfsr;
    uint8_t env_vol[2];
    uint32_t env_phase[2];
} apu;

void apu_init(apu *a);
void apu_reset(apu *a);
void apu_write(apu *a, uint16_t addr, uint8_t value);
uint8_t apu_read_status(apu *a);
void apu_step(apu *a, int cycles);
void apu_render(apu *a, int16_t *samples, int count, int sample_rate);

#endif

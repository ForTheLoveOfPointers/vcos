#ifndef VCOS_SHA256_H
#define VCOS_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
} vcos_sha256_ctx;

void vcos_sha256_init(vcos_sha256_ctx *ctx);
void vcos_sha256_update(vcos_sha256_ctx *ctx, const void *data, size_t len);
void vcos_sha256_final(vcos_sha256_ctx *ctx, uint8_t out[32]);
void vcos_sha256_hex(const void *data, size_t len, char out[65]);

#endif

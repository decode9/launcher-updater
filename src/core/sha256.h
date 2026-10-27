/*
 * sha256.h - Portable SHA-256 (FIPS 180-4), incremental. Used for the certificate pin, blob
 * verification and file checks. No allocation, no libc.
 */
#ifndef LAUNCHER_CORE_SHA256_H
#define LAUNCHER_CORE_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_BYTES 32
#define SHA256_HEX 64

typedef struct {
    uint32_t state[8];
    uint64_t total;   /* bytes hashed so far */
    uint8_t block[64];
    uint32_t used;    /* bytes waiting in block */
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const void *data, size_t size);
void sha256_final(sha256_ctx *ctx, uint8_t out[SHA256_BYTES]);
void sha256(const void *data, size_t size, uint8_t out[SHA256_BYTES]);

#endif

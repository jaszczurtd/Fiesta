/*
 * Portable SHA-256 + HMAC-SHA256 implementation for SerialConfigurator.
 *
 * Provided unconditionally by every crypto backend (the JaszczurHAL
 * backend does not currently expose SHA-256 either, so both backends
 * call directly into this file). The implementation is a
 * straightforward translation of FIPS 180-4 (SHA-256) and RFC 2104
 * (HMAC), validated against the standard test vectors from FIPS 180-2
 * Appendix B and RFC 4231.
 *
 * Used by the SerialConfigurator authentication handshake to derive a
 * per-device key from (UID, compile-time salt) and to compute the
 * challenge-response MAC. Not intended as a general-purpose crypto
 * library.
 */

#include "sc_crypto.h"

#include <string.h>

#define SC_SHA256_BLOCK_BYTES 64u

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[SC_SHA256_BLOCK_BYTES];
    size_t buffer_len;
} ScSha256Ctx;

static const uint32_t k_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rotr32(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static void sha256_init(ScSha256Ctx *ctx)
{
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bit_count = 0u;
    ctx->buffer_len = 0u;
}

static void sha256_compress(ScSha256Ctx *ctx, const uint8_t block[SC_SHA256_BLOCK_BYTES])
{
    uint32_t w[64];
    for (size_t i = 0u; i < 16u; ++i) {
        const size_t base = i * 4u;
        w[i] = ((uint32_t)block[base] << 24) |
               ((uint32_t)block[base + 1u] << 16) |
               ((uint32_t)block[base + 2u] << 8) |
               (uint32_t)block[base + 3u];
    }
    for (size_t i = 16u; i < 64u; ++i) {
        const uint32_t s0 = rotr32(w[i - 15u], 7) ^ rotr32(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        const uint32_t s1 = rotr32(w[i - 2u], 17) ^ rotr32(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (size_t i = 0u; i < 64u; ++i) {
        const uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + S1 + ch + k_sha256_k[i] + w[i];
        const uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_update(ScSha256Ctx *ctx, const uint8_t *data, size_t len)
{
    if (len == 0u) {
        return;
    }
    ctx->bit_count += (uint64_t)len * 8u;

    if (ctx->buffer_len > 0u) {
        const size_t needed = SC_SHA256_BLOCK_BYTES - ctx->buffer_len;
        const size_t take = (len < needed) ? len : needed;
        memcpy(&ctx->buffer[ctx->buffer_len], data, take);
        ctx->buffer_len += take;
        data += take;
        len -= take;
        if (ctx->buffer_len == SC_SHA256_BLOCK_BYTES) {
            sha256_compress(ctx, ctx->buffer);
            ctx->buffer_len = 0u;
        }
    }

    while (len >= SC_SHA256_BLOCK_BYTES) {
        sha256_compress(ctx, data);
        data += SC_SHA256_BLOCK_BYTES;
        len -= SC_SHA256_BLOCK_BYTES;
    }

    if (len > 0u) {
        memcpy(ctx->buffer, data, len);
        ctx->buffer_len = len;
    }
}

static void sha256_final(ScSha256Ctx *ctx, uint8_t out_digest[SC_CRYPTO_SHA256_DIGEST_BYTES])
{
    const uint64_t bit_count = ctx->bit_count;
    uint8_t pad = 0x80u;
    sha256_update(ctx, &pad, 1u);

    const uint8_t zero = 0x00u;
    while (ctx->buffer_len != (SC_SHA256_BLOCK_BYTES - 8u)) {
        sha256_update(ctx, &zero, 1u);
    }

    uint8_t length_be[8];
    for (size_t i = 0u; i < 8u; ++i) {
        length_be[i] = (uint8_t)(bit_count >> (56u - i * 8u));
    }
    sha256_update(ctx, length_be, sizeof(length_be));

    for (size_t i = 0u; i < 8u; ++i) {
        const uint32_t v = ctx->state[i];
        out_digest[i * 4u] = (uint8_t)(v >> 24);
        out_digest[i * 4u + 1u] = (uint8_t)(v >> 16);
        out_digest[i * 4u + 2u] = (uint8_t)(v >> 8);
        out_digest[i * 4u + 3u] = (uint8_t)v;
    }
}

static void bytes_to_hex(
    const uint8_t *bytes,
    size_t bytes_len,
    char *output,
    size_t out_size
)
{
    static const char hex[] = "0123456789abcdef";
    if (output == NULL || out_size == 0u) {
        return;
    }
    if (bytes_len * 2u + 1u > out_size) {
        output[0] = '\0';
        return;
    }
    for (size_t i = 0u; i < bytes_len; ++i) {
        output[i * 2u] = hex[(bytes[i] >> 4) & 0x0Fu];
        output[i * 2u + 1u] = hex[bytes[i] & 0x0Fu];
    }
    output[bytes_len * 2u] = '\0';
}

bool sc_crypto_sha256(
    const uint8_t *input,
    size_t input_len,
    uint8_t out_digest[SC_CRYPTO_SHA256_DIGEST_BYTES]
)
{
    if (out_digest == NULL) {
        return false;
    }
    if (input == NULL && input_len > 0u) {
        return false;
    }
    ScSha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, input, input_len);
    sha256_final(&ctx, out_digest);
    return true;
}

bool sc_crypto_sha256_hex(
    const uint8_t *input,
    size_t input_len,
    char *output,
    size_t out_size
)
{
    if (output == NULL || out_size < SC_CRYPTO_SHA256_HEX_BUF_SIZE) {
        if (output != NULL && out_size > 0u) {
            output[0] = '\0';
        }
        return false;
    }
    uint8_t digest[SC_CRYPTO_SHA256_DIGEST_BYTES];
    if (!sc_crypto_sha256(input, input_len, digest)) {
        output[0] = '\0';
        return false;
    }
    bytes_to_hex(digest, sizeof(digest), output, out_size);
    return true;
}

bool sc_crypto_hmac_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *message,
    size_t message_len,
    uint8_t out_mac[SC_CRYPTO_SHA256_DIGEST_BYTES]
)
{
    if (out_mac == NULL) {
        return false;
    }
    if (key == NULL && key_len > 0u) {
        return false;
    }
    if (message == NULL && message_len > 0u) {
        return false;
    }

    uint8_t key_block[SC_CRYPTO_HMAC_SHA256_BLOCK_BYTES];
    memset(key_block, 0, sizeof(key_block));

    if (key_len > SC_CRYPTO_HMAC_SHA256_BLOCK_BYTES) {
        if (!sc_crypto_sha256(key, key_len, key_block)) {
            return false;
        }
    } else if (key_len > 0u) {
        memcpy(key_block, key, key_len);
    }

    uint8_t inner_pad[SC_CRYPTO_HMAC_SHA256_BLOCK_BYTES];
    uint8_t outer_pad[SC_CRYPTO_HMAC_SHA256_BLOCK_BYTES];
    for (size_t i = 0u; i < SC_CRYPTO_HMAC_SHA256_BLOCK_BYTES; ++i) {
        inner_pad[i] = key_block[i] ^ 0x36u;
        outer_pad[i] = key_block[i] ^ 0x5cu;
    }

    ScSha256Ctx inner_ctx;
    sha256_init(&inner_ctx);
    sha256_update(&inner_ctx, inner_pad, sizeof(inner_pad));
    sha256_update(&inner_ctx, message, message_len);
    uint8_t inner_digest[SC_CRYPTO_SHA256_DIGEST_BYTES];
    sha256_final(&inner_ctx, inner_digest);

    ScSha256Ctx outer_ctx;
    sha256_init(&outer_ctx);
    sha256_update(&outer_ctx, outer_pad, sizeof(outer_pad));
    sha256_update(&outer_ctx, inner_digest, sizeof(inner_digest));
    sha256_final(&outer_ctx, out_mac);
    return true;
}

bool sc_crypto_hmac_sha256_hex(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *message,
    size_t message_len,
    char *output,
    size_t out_size
)
{
    if (output == NULL || out_size < SC_CRYPTO_SHA256_HEX_BUF_SIZE) {
        if (output != NULL && out_size > 0u) {
            output[0] = '\0';
        }
        return false;
    }
    uint8_t mac[SC_CRYPTO_SHA256_DIGEST_BYTES];
    if (!sc_crypto_hmac_sha256(key, key_len, message, message_len, mac)) {
        output[0] = '\0';
        return false;
    }
    bytes_to_hex(mac, sizeof(mac), output, out_size);
    return true;
}

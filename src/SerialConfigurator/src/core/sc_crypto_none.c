#include "sc_crypto.h"

#include <string.h>

#ifndef SC_CRYPTO_BACKEND_NAME
#define SC_CRYPTO_BACKEND_NAME "none"
#endif

bool sc_crypto_available(void)
{
    return false;
}

const char *sc_crypto_backend_name(void)
{
    return SC_CRYPTO_BACKEND_NAME;
}

bool sc_crypto_chacha20_block(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
    uint32_t counter,
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES],
    uint8_t out_block[SC_CRYPTO_CHACHA20_BLOCK_BYTES]
)
{
    (void)key;
    (void)counter;
    (void)nonce;
    (void)out_block;
    return false;
}

bool sc_crypto_chacha20_xor(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
    uint32_t counter,
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES],
    const uint8_t *input,
    size_t input_len,
    uint8_t *output
)
{
    (void)key;
    (void)counter;
    (void)nonce;
    (void)input;
    (void)input_len;
    (void)output;
    return false;
}

bool sc_crypto_chacha20_poly1305_encrypt(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES],
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *plaintext,
    size_t text_len,
    uint8_t *ciphertext,
    uint8_t tag[SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES]
)
{
    (void)key;
    (void)nonce;
    (void)aad;
    (void)aad_len;
    (void)plaintext;
    (void)text_len;
    (void)ciphertext;
    (void)tag;
    return false;
}

bool sc_crypto_chacha20_poly1305_decrypt(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES],
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *ciphertext,
    size_t text_len,
    const uint8_t tag[SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES],
    uint8_t *plaintext
)
{
    (void)key;
    (void)nonce;
    (void)aad;
    (void)aad_len;
    (void)ciphertext;
    (void)text_len;
    (void)tag;
    (void)plaintext;
    return false;
}

bool sc_crypto_md5(
    const uint8_t *input,
    size_t input_len,
    uint8_t out_digest[SC_CRYPTO_MD5_DIGEST_BYTES]
)
{
    (void)input;
    (void)input_len;
    (void)out_digest;
    return false;
}

bool sc_crypto_md5_hex(
    const uint8_t *input,
    size_t input_len,
    char *output,
    size_t out_size
)
{
    (void)input;
    (void)input_len;
    if (output != 0 && out_size > 0u) {
        output[0] = '\0';
    }
    return false;
}

size_t sc_crypto_base64_encoded_len(size_t input_len)
{
    return ((input_len + 2u) / 3u) * 4u;
}

size_t sc_crypto_base64_decoded_max_len(size_t input_len)
{
    return (input_len / 4u) * 3u;
}

static const char k_b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool sc_crypto_base64_encode(
    const uint8_t *input,
    size_t input_len,
    char *output,
    size_t out_size,
    size_t *out_len
)
{
    if (out_len != 0) {
        *out_len = 0u;
    }
    if (output == 0 || out_size == 0u) {
        return false;
    }
    const size_t needed = sc_crypto_base64_encoded_len(input_len);
    if (needed + 1u > out_size) {
        output[0] = '\0';
        return false;
    }
    if (input == 0 && input_len > 0u) {
        output[0] = '\0';
        return false;
    }

    size_t o = 0u;
    size_t i = 0u;
    while (i + 3u <= input_len) {
        const uint32_t v = ((uint32_t)input[i] << 16) |
                           ((uint32_t)input[i + 1u] << 8) |
                           (uint32_t)input[i + 2u];
        output[o++] = k_b64_alphabet[(v >> 18) & 0x3Fu];
        output[o++] = k_b64_alphabet[(v >> 12) & 0x3Fu];
        output[o++] = k_b64_alphabet[(v >> 6) & 0x3Fu];
        output[o++] = k_b64_alphabet[v & 0x3Fu];
        i += 3u;
    }
    const size_t rem = input_len - i;
    if (rem == 1u) {
        const uint32_t v = (uint32_t)input[i] << 16;
        output[o++] = k_b64_alphabet[(v >> 18) & 0x3Fu];
        output[o++] = k_b64_alphabet[(v >> 12) & 0x3Fu];
        output[o++] = '=';
        output[o++] = '=';
    } else if (rem == 2u) {
        const uint32_t v = ((uint32_t)input[i] << 16) |
                           ((uint32_t)input[i + 1u] << 8);
        output[o++] = k_b64_alphabet[(v >> 18) & 0x3Fu];
        output[o++] = k_b64_alphabet[(v >> 12) & 0x3Fu];
        output[o++] = k_b64_alphabet[(v >> 6) & 0x3Fu];
        output[o++] = '=';
    }
    output[o] = '\0';
    if (out_len != 0) {
        *out_len = o;
    }
    return true;
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return (c - 'a') + 26;
    }
    if (c >= '0' && c <= '9') {
        return (c - '0') + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

bool sc_crypto_base64_decode(
    const char *input,
    size_t input_len,
    uint8_t *output,
    size_t out_size,
    size_t *out_len
)
{
    if (out_len != 0) {
        *out_len = 0u;
    }
    if (input == 0 || output == 0) {
        return false;
    }
    if ((input_len % 4u) != 0u) {
        return false;
    }

    size_t o = 0u;
    for (size_t i = 0u; i < input_len; i += 4u) {
        const char c0 = input[i];
        const char c1 = input[i + 1u];
        const char c2 = input[i + 2u];
        const char c3 = input[i + 3u];
        const int v0 = b64_value(c0);
        const int v1 = b64_value(c1);
        if (v0 < 0 || v1 < 0) {
            return false;
        }
        if (o >= out_size) {
            return false;
        }
        output[o++] = (uint8_t)(((uint32_t)v0 << 2) | ((uint32_t)v1 >> 4));
        if (c2 == '=') {
            if (i + 4u != input_len || c3 != '=') {
                return false;
            }
            break;
        }
        const int v2 = b64_value(c2);
        if (v2 < 0) {
            return false;
        }
        if (o >= out_size) {
            return false;
        }
        output[o++] = (uint8_t)((((uint32_t)v1 & 0x0Fu) << 4) | ((uint32_t)v2 >> 2));
        if (c3 == '=') {
            if (i + 4u != input_len) {
                return false;
            }
            break;
        }
        const int v3 = b64_value(c3);
        if (v3 < 0) {
            return false;
        }
        if (o >= out_size) {
            return false;
        }
        output[o++] = (uint8_t)((((uint32_t)v2 & 0x03u) << 6) | (uint32_t)v3);
    }
    if (out_len != 0) {
        *out_len = o;
    }
    return true;
}

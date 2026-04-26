#include "sc_crypto.h"

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
    (void)input_len;
    return 0u;
}

size_t sc_crypto_base64_decoded_max_len(size_t input_len)
{
    (void)input_len;
    return 0u;
}

bool sc_crypto_base64_encode(
    const uint8_t *input,
    size_t input_len,
    char *output,
    size_t out_size,
    size_t *out_len
)
{
    (void)input;
    (void)input_len;
    if (out_len != 0) {
        *out_len = 0u;
    }
    if (output != 0 && out_size > 0u) {
        output[0] = '\0';
    }
    return false;
}

bool sc_crypto_base64_decode(
    const char *input,
    size_t input_len,
    uint8_t *output,
    size_t out_size,
    size_t *out_len
)
{
    (void)input;
    (void)input_len;
    (void)output;
    (void)out_size;
    if (out_len != 0) {
        *out_len = 0u;
    }
    return false;
}

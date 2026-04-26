#include "sc_crypto.h"

#include <hal/hal_crypto.h>

static_assert(
    SC_CRYPTO_CHACHA20_KEY_BYTES == HAL_CHACHA20_KEY_BYTES,
    "ChaCha20 key size mismatch"
);
static_assert(
    SC_CRYPTO_CHACHA20_NONCE_BYTES == HAL_CHACHA20_NONCE_BYTES,
    "ChaCha20 nonce size mismatch"
);
static_assert(
    SC_CRYPTO_CHACHA20_BLOCK_BYTES == HAL_CHACHA20_BLOCK_BYTES,
    "ChaCha20 block size mismatch"
);
static_assert(
    SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES == HAL_CHACHA20_POLY1305_TAG_BYTES,
    "ChaCha20-Poly1305 tag size mismatch"
);
static_assert(
    SC_CRYPTO_MD5_DIGEST_BYTES == HAL_MD5_DIGEST_BYTES,
    "MD5 digest size mismatch"
);
static_assert(
    SC_CRYPTO_MD5_HEX_BUF_SIZE == HAL_MD5_HEX_BUF_SIZE,
    "MD5 hex buffer size mismatch"
);

#ifndef SC_CRYPTO_BACKEND_NAME
#define SC_CRYPTO_BACKEND_NAME "jaszczurhal"
#endif

extern "C" {

bool sc_crypto_available(void)
{
    return true;
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
    return hal_chacha20_block(key, counter, nonce, out_block);
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
    return hal_chacha20_xor(key, counter, nonce, input, input_len, output);
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
    return hal_chacha20_poly1305_encrypt(
        key,
        nonce,
        aad,
        aad_len,
        plaintext,
        text_len,
        ciphertext,
        tag
    );
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
    return hal_chacha20_poly1305_decrypt(
        key,
        nonce,
        aad,
        aad_len,
        ciphertext,
        text_len,
        tag,
        plaintext
    );
}

bool sc_crypto_md5(
    const uint8_t *input,
    size_t input_len,
    uint8_t out_digest[SC_CRYPTO_MD5_DIGEST_BYTES]
)
{
    return hal_md5(input, input_len, out_digest);
}

bool sc_crypto_md5_hex(
    const uint8_t *input,
    size_t input_len,
    char *output,
    size_t out_size
)
{
    return hal_md5_hex(input, input_len, output, out_size);
}

size_t sc_crypto_base64_encoded_len(size_t input_len)
{
    return hal_base64_encoded_len(input_len);
}

size_t sc_crypto_base64_decoded_max_len(size_t input_len)
{
    return hal_base64_decoded_max_len(input_len);
}

bool sc_crypto_base64_encode(
    const uint8_t *input,
    size_t input_len,
    char *output,
    size_t out_size,
    size_t *out_len
)
{
    return hal_base64_encode(input, input_len, output, out_size, out_len);
}

bool sc_crypto_base64_decode(
    const char *input,
    size_t input_len,
    uint8_t *output,
    size_t out_size,
    size_t *out_len
)
{
    return hal_base64_decode(input, input_len, output, out_size, out_len);
}

} /* extern "C" */

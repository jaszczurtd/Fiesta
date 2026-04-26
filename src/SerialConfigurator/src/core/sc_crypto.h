#ifndef SC_CRYPTO_H
#define SC_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC_CRYPTO_CHACHA20_KEY_BYTES 32u
#define SC_CRYPTO_CHACHA20_NONCE_BYTES 12u
#define SC_CRYPTO_CHACHA20_BLOCK_BYTES 64u
#define SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES 16u
#define SC_CRYPTO_MD5_DIGEST_BYTES 16u
#define SC_CRYPTO_MD5_HEX_BUF_SIZE 33u
#define SC_CRYPTO_SHA256_DIGEST_BYTES 32u
#define SC_CRYPTO_SHA256_HEX_BUF_SIZE 65u
#define SC_CRYPTO_HMAC_SHA256_BLOCK_BYTES 64u

bool sc_crypto_available(void);
const char *sc_crypto_backend_name(void);

bool sc_crypto_chacha20_block(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
    uint32_t counter,
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES],
    uint8_t out_block[SC_CRYPTO_CHACHA20_BLOCK_BYTES]
);
bool sc_crypto_chacha20_xor(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
    uint32_t counter,
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES],
    const uint8_t *input,
    size_t input_len,
    uint8_t *output
);
bool sc_crypto_chacha20_poly1305_encrypt(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES],
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *plaintext,
    size_t text_len,
    uint8_t *ciphertext,
    uint8_t tag[SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES]
);
bool sc_crypto_chacha20_poly1305_decrypt(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES],
    const uint8_t *aad,
    size_t aad_len,
    const uint8_t *ciphertext,
    size_t text_len,
    const uint8_t tag[SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES],
    uint8_t *plaintext
);
bool sc_crypto_md5(
    const uint8_t *input,
    size_t input_len,
    uint8_t out_digest[SC_CRYPTO_MD5_DIGEST_BYTES]
);
bool sc_crypto_md5_hex(
    const uint8_t *input,
    size_t input_len,
    char *output,
    size_t out_size
);
size_t sc_crypto_base64_encoded_len(size_t input_len);
size_t sc_crypto_base64_decoded_max_len(size_t input_len);
bool sc_crypto_base64_encode(
    const uint8_t *input,
    size_t input_len,
    char *output,
    size_t out_size,
    size_t *out_len
);
bool sc_crypto_base64_decode(
    const char *input,
    size_t input_len,
    uint8_t *output,
    size_t out_size,
    size_t *out_len
);

/*
 * SHA-256 / HMAC-SHA256.
 *
 * Provided unconditionally by every backend (portable implementation
 * lives in sc_sha256.c). Used by the SerialConfigurator authentication
 * handshake (Phase 3): per-device key is derived as
 *   HMAC-SHA256(key=salt, message=uid)
 * and the challenge response is
 *   HMAC-SHA256(key=device_key, message=challenge || session_id).
 */
bool sc_crypto_sha256(
    const uint8_t *input,
    size_t input_len,
    uint8_t out_digest[SC_CRYPTO_SHA256_DIGEST_BYTES]
);
bool sc_crypto_sha256_hex(
    const uint8_t *input,
    size_t input_len,
    char *output,
    size_t out_size
);
bool sc_crypto_hmac_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *message,
    size_t message_len,
    uint8_t out_mac[SC_CRYPTO_SHA256_DIGEST_BYTES]
);
bool sc_crypto_hmac_sha256_hex(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *message,
    size_t message_len,
    char *output,
    size_t out_size
);

#ifdef __cplusplus
}
#endif

#endif /* SC_CRYPTO_H */

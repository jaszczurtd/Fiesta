/*
 * SerialConfigurator crypto entry points backed by JaszczurHAL hal_crypto.
 *
 * Base64, MD5, SHA-256 / HMAC-SHA256 and ChaCha20-Poly1305 are computed by
 * the HAL sources compiled into serial_configurator_core. This file adapts
 * the C-callable sc_crypto API and pins the shared size constants at
 * compile time; it holds no crypto implementation of its own.
 */

#include "sc_crypto.h"

#include <hal/security/hal_crypto.h>

static_assert(SC_CRYPTO_CHACHA20_KEY_BYTES == HAL_CHACHA20_KEY_BYTES,
              "ChaCha20 key size mismatch");
static_assert(SC_CRYPTO_CHACHA20_NONCE_BYTES == HAL_CHACHA20_NONCE_BYTES,
              "ChaCha20 nonce size mismatch");
static_assert(SC_CRYPTO_CHACHA20_BLOCK_BYTES == HAL_CHACHA20_BLOCK_BYTES,
              "ChaCha20 block size mismatch");
static_assert(SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES ==
                  HAL_CHACHA20_POLY1305_TAG_BYTES,
              "ChaCha20-Poly1305 tag size mismatch");
static_assert(SC_CRYPTO_MD5_DIGEST_BYTES == HAL_MD5_DIGEST_BYTES,
              "MD5 digest size mismatch");
static_assert(SC_CRYPTO_MD5_HEX_BUF_SIZE == HAL_MD5_HEX_BUF_SIZE,
              "MD5 hex buffer size mismatch");
static_assert(SC_CRYPTO_SHA256_DIGEST_BYTES == HAL_SHA256_DIGEST_BYTES,
              "SHA-256 digest size mismatch");
static_assert(SC_CRYPTO_SHA256_HEX_BUF_SIZE == HAL_SHA256_HEX_BUF_SIZE,
              "SHA-256 hex buffer size mismatch");
static_assert(SC_CRYPTO_HMAC_SHA256_BLOCK_BYTES == HAL_HMAC_SHA256_BLOCK_BYTES,
              "HMAC-SHA256 block size mismatch");

extern "C" {

bool sc_crypto_chacha20_block(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES], uint32_t counter,
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES],
    uint8_t out_block[SC_CRYPTO_CHACHA20_BLOCK_BYTES]) {
  return hal_chacha20_block(key, counter, nonce, out_block);
}

bool sc_crypto_chacha20_xor(const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
                            uint32_t counter,
                            const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES],
                            const uint8_t *input, size_t input_len,
                            uint8_t *output) {
  return hal_chacha20_xor(key, counter, nonce, input, input_len, output);
}

bool sc_crypto_chacha20_poly1305_encrypt(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES], const uint8_t *aad,
    size_t aad_len, const uint8_t *plaintext, size_t text_len,
    uint8_t *ciphertext, uint8_t tag[SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES]) {
  return hal_chacha20_poly1305_encrypt(key, nonce, aad, aad_len, plaintext,
                                       text_len, ciphertext, tag);
}

bool sc_crypto_chacha20_poly1305_decrypt(
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES],
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES], const uint8_t *aad,
    size_t aad_len, const uint8_t *ciphertext, size_t text_len,
    const uint8_t tag[SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES],
    uint8_t *plaintext) {
  return hal_chacha20_poly1305_decrypt(key, nonce, aad, aad_len, ciphertext,
                                       text_len, tag, plaintext);
}

bool sc_crypto_md5(const uint8_t *input, size_t input_len,
                   uint8_t out_digest[SC_CRYPTO_MD5_DIGEST_BYTES]) {
  return hal_md5(input, input_len, out_digest);
}

bool sc_crypto_md5_hex(const uint8_t *input, size_t input_len, char *output,
                       size_t out_size) {
  return hal_md5_hex(input, input_len, output, out_size);
}

size_t sc_crypto_base64_encoded_len(size_t input_len) {
  return hal_base64_encoded_len(input_len);
}

size_t sc_crypto_base64_decoded_max_len(size_t input_len) {
  return hal_base64_decoded_max_len(input_len);
}

bool sc_crypto_base64_encode(const uint8_t *input, size_t input_len,
                             char *output, size_t out_size, size_t *out_len) {
  return hal_base64_encode(input, input_len, output, out_size, out_len);
}

bool sc_crypto_base64_decode(const char *input, size_t input_len,
                             uint8_t *output, size_t out_size,
                             size_t *out_len) {
  return hal_base64_decode(input, input_len, output, out_size, out_len);
}

bool sc_crypto_sha256(const uint8_t *input, size_t input_len,
                      uint8_t out_digest[SC_CRYPTO_SHA256_DIGEST_BYTES]) {
  return hal_sha256(input, input_len, out_digest);
}

bool sc_crypto_sha256_hex(const uint8_t *input, size_t input_len, char *output,
                          size_t out_size) {
  return hal_sha256_hex(input, input_len, output, out_size);
}

bool sc_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                           const uint8_t *message, size_t message_len,
                           uint8_t out_mac[SC_CRYPTO_SHA256_DIGEST_BYTES]) {
  return hal_hmac_sha256(key, key_len, message, message_len, out_mac);
}

bool sc_crypto_hmac_sha256_hex(const uint8_t *key, size_t key_len,
                               const uint8_t *message, size_t message_len,
                               char *output, size_t out_size) {
  return hal_hmac_sha256_hex(key, key_len, message, message_len, output,
                             out_size);
}

} /* extern "C" */

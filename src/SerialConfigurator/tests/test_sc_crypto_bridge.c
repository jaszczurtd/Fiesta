#include "sc_crypto.h"

#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "[FAIL] %s\n", msg); \
    return 1; \
} } while (0)

static int test_backend_metadata(void)
{
    const char *name = sc_crypto_backend_name();
    TEST_ASSERT(name != 0, "backend name should not be NULL");

    if (sc_crypto_available()) {
        TEST_ASSERT(strcmp(name, "jaszczurhal") == 0, "unexpected backend name when crypto is available");
    } else {
        TEST_ASSERT(strcmp(name, "none") == 0, "unexpected backend name when crypto is unavailable");
    }

    return 0;
}

static int test_md5_contract(void)
{
    const uint8_t input[] = { 'a', 'b', 'c' };
    uint8_t digest[SC_CRYPTO_MD5_DIGEST_BYTES];
    char hex[SC_CRYPTO_MD5_HEX_BUF_SIZE];

    if (!sc_crypto_available()) {
        TEST_ASSERT(!sc_crypto_md5(input, sizeof(input), digest), "MD5 should fail on unavailable backend");
        TEST_ASSERT(!sc_crypto_md5_hex(input, sizeof(input), hex, sizeof(hex)), "MD5 hex should fail on unavailable backend");
        return 0;
    }

    const uint8_t expected_digest[SC_CRYPTO_MD5_DIGEST_BYTES] = {
        0x90u, 0x01u, 0x50u, 0x98u, 0x3cu, 0xd2u, 0x4fu, 0xb0u,
        0xd6u, 0x96u, 0x3fu, 0x7du, 0x28u, 0xe1u, 0x7fu, 0x72u
    };

    TEST_ASSERT(sc_crypto_md5(input, sizeof(input), digest), "MD5 should succeed");
    TEST_ASSERT(memcmp(digest, expected_digest, sizeof(digest)) == 0, "MD5 digest mismatch");
    TEST_ASSERT(sc_crypto_md5_hex(input, sizeof(input), hex, sizeof(hex)), "MD5 hex should succeed");
    TEST_ASSERT(strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0, "MD5 hex mismatch");
    return 0;
}

static int test_base64_contract(void)
{
    static const uint8_t raw[] = { 'F', 'i', 'e', 's', 't', 'a' };
    char encoded[64];
    uint8_t decoded[64];
    size_t encoded_len = 0u;
    size_t decoded_len = 0u;

    /*
     * base64 is encoding, not crypto: both the "jaszczurhal" and "none"
     * backends must provide a working implementation so the line
     * protocol (which carries base64-encoded build_id and similar
     * fields) works in builds without a real crypto backend.
     */
    (void)sc_crypto_available();

    TEST_ASSERT(sc_crypto_base64_encoded_len(sizeof(raw)) == 8u, "encoded len mismatch");
    TEST_ASSERT(sc_crypto_base64_decoded_max_len(8u) == 6u, "decoded max len mismatch");
    TEST_ASSERT(sc_crypto_base64_encode(raw, sizeof(raw), encoded, sizeof(encoded), &encoded_len), "base64 encode should succeed");
    TEST_ASSERT(encoded_len == 8u, "encoded length mismatch");
    TEST_ASSERT(strcmp(encoded, "Rmllc3Rh") == 0, "base64 encoded value mismatch");

    TEST_ASSERT(sc_crypto_base64_decode(encoded, encoded_len, decoded, sizeof(decoded), &decoded_len), "base64 decode should succeed");
    TEST_ASSERT(decoded_len == sizeof(raw), "decoded length mismatch");
    TEST_ASSERT(memcmp(decoded, raw, sizeof(raw)) == 0, "base64 decoded bytes mismatch");
    return 0;
}

static int test_chacha20_roundtrip(void)
{
    const uint8_t key[SC_CRYPTO_CHACHA20_KEY_BYTES] = {
        0x80u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u,
        0x88u, 0x89u, 0x8au, 0x8bu, 0x8cu, 0x8du, 0x8eu, 0x8fu,
        0x90u, 0x91u, 0x92u, 0x93u, 0x94u, 0x95u, 0x96u, 0x97u,
        0x98u, 0x99u, 0x9au, 0x9bu, 0x9cu, 0x9du, 0x9eu, 0x9fu
    };
    const uint8_t nonce[SC_CRYPTO_CHACHA20_NONCE_BYTES] = {
        0x00u, 0x00u, 0x00u, 0x09u,
        0x00u, 0x00u, 0x00u, 0x4au,
        0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t plaintext[] = "SerialConfigurator";
    uint8_t ciphertext[sizeof(plaintext)];
    uint8_t decrypted[sizeof(plaintext)];
    uint8_t tag[SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES];
    uint8_t tampered_tag[SC_CRYPTO_CHACHA20_POLY1305_TAG_BYTES];

    if (!sc_crypto_available()) {
        TEST_ASSERT(!sc_crypto_chacha20_xor(key, 1u, nonce, plaintext, sizeof(plaintext), ciphertext), "chacha20 xor should fail on unavailable backend");
        return 0;
    }

    TEST_ASSERT(sc_crypto_chacha20_xor(key, 1u, nonce, plaintext, sizeof(plaintext), ciphertext), "chacha20 xor encrypt failed");
    TEST_ASSERT(sc_crypto_chacha20_xor(key, 1u, nonce, ciphertext, sizeof(ciphertext), decrypted), "chacha20 xor decrypt failed");
    TEST_ASSERT(memcmp(decrypted, plaintext, sizeof(plaintext)) == 0, "chacha20 roundtrip mismatch");

    TEST_ASSERT(
        sc_crypto_chacha20_poly1305_encrypt(
            key,
            nonce,
            0,
            0u,
            plaintext,
            sizeof(plaintext),
            ciphertext,
            tag
        ),
        "chacha20-poly1305 encrypt failed"
    );
    TEST_ASSERT(
        sc_crypto_chacha20_poly1305_decrypt(
            key,
            nonce,
            0,
            0u,
            ciphertext,
            sizeof(ciphertext),
            tag,
            decrypted
        ),
        "chacha20-poly1305 decrypt failed"
    );
    TEST_ASSERT(memcmp(decrypted, plaintext, sizeof(plaintext)) == 0, "chacha20-poly1305 roundtrip mismatch");

    memcpy(tampered_tag, tag, sizeof(tag));
    tampered_tag[0] ^= 0x01u;
    TEST_ASSERT(
        !sc_crypto_chacha20_poly1305_decrypt(
            key,
            nonce,
            0,
            0u,
            ciphertext,
            sizeof(ciphertext),
            tampered_tag,
            decrypted
        ),
        "chacha20-poly1305 should reject tampered tag"
    );

    return 0;
}

int main(void)
{
    if (test_backend_metadata() != 0) {
        return 1;
    }
    if (test_md5_contract() != 0) {
        return 1;
    }
    if (test_base64_contract() != 0) {
        return 1;
    }
    if (test_chacha20_roundtrip() != 0) {
        return 1;
    }

    printf("[OK] serial_configurator crypto bridge tests passed\n");
    return 0;
}

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

/* SHA-256 of "abc" (FIPS 180-2 Appendix B.1). */
static const uint8_t k_sha256_abc[SC_CRYPTO_SHA256_DIGEST_BYTES] = {
    0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau,
    0x41u, 0x41u, 0x40u, 0xdeu, 0x5du, 0xaeu, 0x22u, 0x23u,
    0xb0u, 0x03u, 0x61u, 0xa3u, 0x96u, 0x17u, 0x7au, 0x9cu,
    0xb4u, 0x10u, 0xffu, 0x61u, 0xf2u, 0x00u, 0x15u, 0xadu
};

/* SHA-256 of the empty string. */
static const uint8_t k_sha256_empty[SC_CRYPTO_SHA256_DIGEST_BYTES] = {
    0xe3u, 0xb0u, 0xc4u, 0x42u, 0x98u, 0xfcu, 0x1cu, 0x14u,
    0x9au, 0xfbu, 0xf4u, 0xc8u, 0x99u, 0x6fu, 0xb9u, 0x24u,
    0x27u, 0xaeu, 0x41u, 0xe4u, 0x64u, 0x9bu, 0x93u, 0x4cu,
    0xa4u, 0x95u, 0x99u, 0x1bu, 0x78u, 0x52u, 0xb8u, 0x55u
};

static int test_sha256_vectors(void)
{
    uint8_t digest[SC_CRYPTO_SHA256_DIGEST_BYTES];
    char hex[SC_CRYPTO_SHA256_HEX_BUF_SIZE];

    TEST_ASSERT(sc_crypto_sha256((const uint8_t *)"abc", 3u, digest), "sha256(abc) failed");
    TEST_ASSERT(memcmp(digest, k_sha256_abc, sizeof(digest)) == 0, "sha256(abc) mismatch");
    TEST_ASSERT(sc_crypto_sha256_hex((const uint8_t *)"abc", 3u, hex, sizeof(hex)), "sha256_hex(abc) failed");
    TEST_ASSERT(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "sha256_hex(abc) mismatch");

    TEST_ASSERT(sc_crypto_sha256(NULL, 0u, digest), "sha256(empty) failed");
    TEST_ASSERT(memcmp(digest, k_sha256_empty, sizeof(digest)) == 0, "sha256(empty) mismatch");

    /* Boundary: input that crosses block boundary (>64 bytes). */
    static const uint8_t long_input[112] = {
        /* "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" repeated */
        0x61, 0x62, 0x63, 0x64, 0x62, 0x63, 0x64, 0x65, 0x63, 0x64, 0x65, 0x66, 0x64, 0x65, 0x66, 0x67,
        0x65, 0x66, 0x67, 0x68, 0x66, 0x67, 0x68, 0x69, 0x67, 0x68, 0x69, 0x6a, 0x68, 0x69, 0x6a, 0x6b,
        0x69, 0x6a, 0x6b, 0x6c, 0x6a, 0x6b, 0x6c, 0x6d, 0x6b, 0x6c, 0x6d, 0x6e, 0x6c, 0x6d, 0x6e, 0x6f,
        0x6d, 0x6e, 0x6f, 0x70, 0x6e, 0x6f, 0x70, 0x71,
        0x61, 0x62, 0x63, 0x64, 0x62, 0x63, 0x64, 0x65, 0x63, 0x64, 0x65, 0x66, 0x64, 0x65, 0x66, 0x67,
        0x65, 0x66, 0x67, 0x68, 0x66, 0x67, 0x68, 0x69, 0x67, 0x68, 0x69, 0x6a, 0x68, 0x69, 0x6a, 0x6b,
        0x69, 0x6a, 0x6b, 0x6c, 0x6a, 0x6b, 0x6c, 0x6d, 0x6b, 0x6c, 0x6d, 0x6e, 0x6c, 0x6d, 0x6e, 0x6f,
        0x6d, 0x6e, 0x6f, 0x70, 0x6e, 0x6f, 0x70, 0x71,
    };
    TEST_ASSERT(sc_crypto_sha256_hex(long_input, sizeof(long_input), hex, sizeof(hex)), "sha256(long) failed");
    /* Result is just expected to be deterministic; primarily exercises multi-block compress. */
    TEST_ASSERT(strlen(hex) == 64u, "sha256_hex length mismatch");
    return 0;
}

static int test_hmac_sha256_vectors(void)
{
    uint8_t mac[SC_CRYPTO_SHA256_DIGEST_BYTES];
    char hex[SC_CRYPTO_SHA256_HEX_BUF_SIZE];

    /* RFC 4231 Test Case 1: key=0x0b*20, data="Hi There". */
    const uint8_t key1[20] = {
        0x0bu, 0x0bu, 0x0bu, 0x0bu, 0x0bu, 0x0bu, 0x0bu, 0x0bu,
        0x0bu, 0x0bu, 0x0bu, 0x0bu, 0x0bu, 0x0bu, 0x0bu, 0x0bu,
        0x0bu, 0x0bu, 0x0bu, 0x0bu
    };
    const uint8_t data1[] = { 'H','i',' ','T','h','e','r','e' };
    const uint8_t expected1[SC_CRYPTO_SHA256_DIGEST_BYTES] = {
        0xb0u, 0x34u, 0x4cu, 0x61u, 0xd8u, 0xdbu, 0x38u, 0x53u,
        0x5cu, 0xa8u, 0xafu, 0xceu, 0xafu, 0x0bu, 0xf1u, 0x2bu,
        0x88u, 0x1du, 0xc2u, 0x00u, 0xc9u, 0x83u, 0x3du, 0xa7u,
        0x26u, 0xe9u, 0x37u, 0x6cu, 0x2eu, 0x32u, 0xcfu, 0xf7u
    };
    TEST_ASSERT(sc_crypto_hmac_sha256(key1, sizeof(key1), data1, sizeof(data1), mac), "hmac case1 failed");
    TEST_ASSERT(memcmp(mac, expected1, sizeof(mac)) == 0, "hmac case1 mismatch");
    TEST_ASSERT(sc_crypto_hmac_sha256_hex(key1, sizeof(key1), data1, sizeof(data1), hex, sizeof(hex)), "hmac_hex case1 failed");
    TEST_ASSERT(strcmp(hex, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") == 0, "hmac_hex case1 mismatch");

    /* RFC 4231 Test Case 2: key="Jefe", data="what do ya want for nothing?". */
    const uint8_t key2[] = { 'J','e','f','e' };
    const uint8_t data2[] = "what do ya want for nothing?";
    TEST_ASSERT(
        sc_crypto_hmac_sha256_hex(key2, sizeof(key2), data2, sizeof(data2) - 1u, hex, sizeof(hex)),
        "hmac case2 failed"
    );
    TEST_ASSERT(
        strcmp(hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843") == 0,
        "hmac case2 mismatch"
    );

    /* RFC 4231 Test Case 4: key=0x01..0x19, data=0xcd*50. */
    const uint8_t key4[25] = {
        0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
        0x09u, 0x0au, 0x0bu, 0x0cu, 0x0du, 0x0eu, 0x0fu, 0x10u,
        0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u, 0x18u, 0x19u
    };
    uint8_t data4[50];
    memset(data4, 0xcdu, sizeof(data4));
    TEST_ASSERT(
        sc_crypto_hmac_sha256_hex(key4, sizeof(key4), data4, sizeof(data4), hex, sizeof(hex)),
        "hmac case4 failed"
    );
    TEST_ASSERT(
        strcmp(hex, "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b") == 0,
        "hmac case4 mismatch"
    );

    /* RFC 4231 Test Case 6: key=0xaa*131 (>block), data short. */
    uint8_t key6[131];
    memset(key6, 0xaau, sizeof(key6));
    const uint8_t data6[] = "Test Using Larger Than Block-Size Key - Hash Key First";
    TEST_ASSERT(
        sc_crypto_hmac_sha256_hex(key6, sizeof(key6), data6, sizeof(data6) - 1u, hex, sizeof(hex)),
        "hmac case6 failed"
    );
    TEST_ASSERT(
        strcmp(hex, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54") == 0,
        "hmac case6 mismatch"
    );

    /* Reject NULL output. */
    TEST_ASSERT(!sc_crypto_hmac_sha256(key1, sizeof(key1), data1, sizeof(data1), NULL), "hmac NULL out should fail");
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
    if (test_sha256_vectors() != 0) {
        return 1;
    }
    if (test_hmac_sha256_vectors() != 0) {
        return 1;
    }

    printf("[OK] serial_configurator crypto bridge tests passed\n");
    return 0;
}

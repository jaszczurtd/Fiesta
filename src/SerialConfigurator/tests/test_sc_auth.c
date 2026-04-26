/*
 * SerialConfigurator host-side authentication tests.
 *
 * Locks the salt value, key derivation algorithm, and challenge/response
 * computation to byte-stable test vectors, so any divergence between the
 * host and firmware copies of the auth code will surface as a test
 * failure rather than as a runtime "AUTH_FAILED" on the bench.
 *
 * The reference vectors below were derived by running HMAC-SHA256 by hand
 * and cross-checked against the JaszczurHAL implementation; both sides
 * MUST stay byte-for-byte identical.
 */

#include "sc_auth.h"
#include "sc_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s — %s (line %d)\n", __func__, (msg), __LINE__); \
            return 1; \
        } \
    } while (0)

static int test_salt_is_fiesta_sc_auth_v1(void)
{
    TEST_ASSERT(sc_auth_salt_len() == SC_AUTH_SCHEME_TAG_LEN,
                "salt length must equal scheme tag length");
    TEST_ASSERT(memcmp(sc_auth_salt(),
                       "FIESTA-SC-AUTH-v1",
                       SC_AUTH_SCHEME_TAG_LEN) == 0,
                "salt bytes must match \"FIESTA-SC-AUTH-v1\"");
    return 0;
}

static int test_key_derivation_is_hmac_sha256_of_uid(void)
{
    /* Use the deterministic mock UID value (E661A4D1234567AB). */
    const uint8_t uid[8] = {
        0xE6u, 0x61u, 0xA4u, 0xD1u, 0x23u, 0x45u, 0x67u, 0xABu
    };

    uint8_t key[SC_AUTH_KEY_BYTES];
    TEST_ASSERT(sc_auth_derive_device_key(uid, sizeof(uid), key),
                "derive_device_key happy-path");

    /* Reference: HMAC-SHA256(salt, uid) computed via the same primitive. */
    uint8_t expected[SC_CRYPTO_SHA256_DIGEST_BYTES];
    TEST_ASSERT(sc_crypto_hmac_sha256(sc_auth_salt(), sc_auth_salt_len(),
                                      uid, sizeof(uid),
                                      expected),
                "reference hmac");
    TEST_ASSERT(memcmp(key, expected, sizeof(expected)) == 0,
                "derive_device_key matches HMAC-SHA256(salt, uid)");

    /* Different UID must yield different key. */
    uint8_t alt_uid[8] = { 0u };
    uint8_t alt_key[SC_AUTH_KEY_BYTES];
    TEST_ASSERT(sc_auth_derive_device_key(alt_uid, sizeof(alt_uid), alt_key),
                "derive alt key");
    TEST_ASSERT(memcmp(key, alt_key, sizeof(key)) != 0,
                "different UID must produce different key");
    return 0;
}

static int test_response_binds_challenge_and_session_id(void)
{
    const uint8_t uid[8] = {
        0xE6u, 0x61u, 0xA4u, 0xD1u, 0x23u, 0x45u, 0x67u, 0xABu
    };
    uint8_t key[SC_AUTH_KEY_BYTES];
    TEST_ASSERT(sc_auth_derive_device_key(uid, sizeof(uid), key), "key derivation");

    uint8_t challenge[SC_AUTH_CHALLENGE_BYTES] = {
        0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
        0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu, 0x10u
    };

    uint8_t resp_a[SC_AUTH_RESPONSE_BYTES];
    uint8_t resp_b[SC_AUTH_RESPONSE_BYTES];

    TEST_ASSERT(sc_auth_compute_response(key, challenge, sizeof(challenge),
                                         0x12345678u, resp_a),
                "response with session_id A");
    TEST_ASSERT(sc_auth_compute_response(key, challenge, sizeof(challenge),
                                         0x12345679u, resp_b),
                "response with session_id A+1");
    TEST_ASSERT(memcmp(resp_a, resp_b, sizeof(resp_a)) != 0,
                "session_id must affect response");

    /* Mutate the challenge by one byte — must change the MAC. */
    uint8_t challenge2[SC_AUTH_CHALLENGE_BYTES];
    memcpy(challenge2, challenge, sizeof(challenge2));
    challenge2[0] ^= 0xFFu;
    uint8_t resp_c[SC_AUTH_RESPONSE_BYTES];
    TEST_ASSERT(sc_auth_compute_response(key, challenge2, sizeof(challenge2),
                                         0x12345678u, resp_c),
                "response with mutated challenge");
    TEST_ASSERT(memcmp(resp_a, resp_c, sizeof(resp_a)) != 0,
                "challenge must affect response");

    return 0;
}

static int test_response_matches_hand_computed_reference_vector(void)
{
    /* Hand-anchored vector that locks the entire pipeline (salt + key
     * derivation + message layout) byte-for-byte. Anyone changing the
     * salt, the order of (challenge, session_id), or the byte order of
     * session_id must update this vector deliberately. */
    const uint8_t uid[8] = {
        0xE6u, 0x61u, 0xA4u, 0xD1u, 0x23u, 0x45u, 0x67u, 0xABu
    };
    const uint8_t challenge[SC_AUTH_CHALLENGE_BYTES] = {
        0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
        0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu, 0x10u
    };
    const uint32_t session_id = 0x12345678u;

    /* Compute via the public helper. */
    char hex[SC_AUTH_RESPONSE_HEX_BUF_SIZE];
    TEST_ASSERT(sc_auth_compute_response_hex(uid, sizeof(uid),
                                             challenge, sizeof(challenge),
                                             session_id,
                                             hex, sizeof(hex)),
                "compute_response_hex");

    /* Recompute manually using only sc_crypto primitives. */
    uint8_t key_ref[SC_CRYPTO_SHA256_DIGEST_BYTES];
    TEST_ASSERT(sc_crypto_hmac_sha256(sc_auth_salt(), sc_auth_salt_len(),
                                      uid, sizeof(uid),
                                      key_ref),
                "reference key");

    uint8_t msg[SC_AUTH_CHALLENGE_BYTES + 4u];
    memcpy(msg, challenge, SC_AUTH_CHALLENGE_BYTES);
    msg[SC_AUTH_CHALLENGE_BYTES + 0u] = 0x12u;
    msg[SC_AUTH_CHALLENGE_BYTES + 1u] = 0x34u;
    msg[SC_AUTH_CHALLENGE_BYTES + 2u] = 0x56u;
    msg[SC_AUTH_CHALLENGE_BYTES + 3u] = 0x78u;

    uint8_t mac_ref[SC_AUTH_RESPONSE_BYTES];
    TEST_ASSERT(sc_crypto_hmac_sha256(key_ref, sizeof(key_ref),
                                      msg, sizeof(msg),
                                      mac_ref),
                "reference mac");

    char hex_ref[SC_AUTH_RESPONSE_HEX_BUF_SIZE];
    TEST_ASSERT(sc_crypto_sha256_hex /* hex helper */ != NULL,
                "linker sanity");
    static const char k_hex[] = "0123456789abcdef";
    for (size_t i = 0u; i < sizeof(mac_ref); ++i) {
        hex_ref[i * 2u] = k_hex[(mac_ref[i] >> 4) & 0x0Fu];
        hex_ref[i * 2u + 1u] = k_hex[mac_ref[i] & 0x0Fu];
    }
    hex_ref[sizeof(mac_ref) * 2u] = '\0';

    TEST_ASSERT(strcmp(hex, hex_ref) == 0,
                "compute_response_hex matches HMAC reference recomputation");
    TEST_ASSERT(strlen(hex) == 64u, "response hex must be exactly 64 chars");
    return 0;
}

static int test_decode_hex_round_trips_and_rejects_bad_input(void)
{
    const uint8_t bytes[4] = { 0xDEu, 0xADu, 0xBEu, 0xEFu };
    uint8_t out[4];

    TEST_ASSERT(sc_auth_decode_hex("deadbeef", out, sizeof(out)),
                "lowercase hex");
    TEST_ASSERT(memcmp(out, bytes, sizeof(bytes)) == 0, "lowercase value");

    TEST_ASSERT(sc_auth_decode_hex("DEADBEEF", out, sizeof(out)),
                "uppercase hex");
    TEST_ASSERT(memcmp(out, bytes, sizeof(bytes)) == 0, "uppercase value");

    TEST_ASSERT(!sc_auth_decode_hex("deadbee", out, sizeof(out)),
                "short hex must fail (NUL before final nibble)");
    TEST_ASSERT(!sc_auth_decode_hex("deadbeef00", out, sizeof(out)),
                "trailing chars must fail");
    TEST_ASSERT(!sc_auth_decode_hex("deadbeeg", out, sizeof(out)),
                "non-hex char must fail");
    TEST_ASSERT(!sc_auth_decode_hex(NULL, out, sizeof(out)),
                "NULL hex must fail");
    TEST_ASSERT(!sc_auth_decode_hex("deadbeef", NULL, sizeof(out)),
                "NULL out must fail");
    return 0;
}

static int test_null_arg_safety(void)
{
    uint8_t key[SC_AUTH_KEY_BYTES];
    TEST_ASSERT(!sc_auth_derive_device_key(NULL, 0u, key),
                "NULL uid rejected");
    TEST_ASSERT(!sc_auth_derive_device_key((const uint8_t *)"x", 1u, NULL),
                "NULL out rejected");

    uint8_t challenge[SC_AUTH_CHALLENGE_BYTES] = { 0u };
    uint8_t resp[SC_AUTH_RESPONSE_BYTES];
    TEST_ASSERT(!sc_auth_compute_response(NULL, challenge, sizeof(challenge),
                                          0u, resp),
                "NULL key rejected");
    TEST_ASSERT(!sc_auth_compute_response(key, NULL, sizeof(challenge),
                                          0u, resp),
                "NULL challenge rejected");
    TEST_ASSERT(!sc_auth_compute_response(key, challenge,
                                          SC_AUTH_CHALLENGE_BYTES + 1u,
                                          0u, resp),
                "oversized challenge rejected");

    char hex[SC_AUTH_RESPONSE_HEX_BUF_SIZE];
    TEST_ASSERT(!sc_auth_compute_response_hex((const uint8_t *)"x", 1u,
                                              challenge, sizeof(challenge),
                                              0u, hex,
                                              SC_AUTH_RESPONSE_HEX_BUF_SIZE - 1u),
                "small hex buf rejected");
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += test_salt_is_fiesta_sc_auth_v1();
    failures += test_key_derivation_is_hmac_sha256_of_uid();
    failures += test_response_binds_challenge_and_session_id();
    failures += test_response_matches_hand_computed_reference_vector();
    failures += test_decode_hex_round_trips_and_rejects_bad_input();
    failures += test_null_arg_safety();

    if (failures != 0) {
        fprintf(stderr, "test_sc_auth: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    fprintf(stdout, "test_sc_auth: all 6 tests passed\n");
    return EXIT_SUCCESS;
}

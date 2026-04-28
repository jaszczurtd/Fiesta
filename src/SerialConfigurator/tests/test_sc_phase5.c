/*
 * Phase 5 host orchestration: authenticate + reboot_to_bootloader.
 *
 * Drives sc_core_authenticate / sc_core_reboot_to_bootloader through a
 * small mock transport that mirrors the firmware-side state machine
 * (authenticated flag, pending challenge consumed by AUTH_PROVE).
 *
 * The expected challenge response is computed in the test using the
 * production sc_auth helpers, so any divergence between the host
 * orchestrator and the firmware-shared salt / MAC layout shows up here.
 */

#include "sc_auth.h"
#include "sc_core.h"
#include "sc_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s - %s (line %d)\n", __func__, (msg), __LINE__); \
            return 1; \
        } \
    } while (0)

#define TEST_ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            fprintf(stderr, \
                    "FAIL: %s - %s (line %d): got %d, want %d\n", \
                    __func__, (msg), __LINE__, (int)(a), (int)(b)); \
            return 1; \
        } \
    } while (0)

/* The mock UID matches the JaszczurHAL mock default so the tests stay
 * stable if/when the host reuses any cross-vector. */
static const uint8_t k_uid[8] = {
    0xE6u, 0x61u, 0xA4u, 0xD1u, 0x23u, 0x45u, 0x67u, 0xABu
};
static const uint32_t k_session_id = 0x12345678u;
static const uint8_t k_challenge[SC_AUTH_CHALLENGE_BYTES] = {
    0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
    0x18u, 0x19u, 0x1Au, 0x1Bu, 0x1Cu, 0x1Du, 0x1Eu, 0x1Fu
};

typedef struct {
    /* Tunable replies. */
    char hello_reply[256];
    char auth_begin_reply[256];
    /* Derived/observed state. */
    bool authenticated;
    bool challenge_consumed;
    bool reboot_requested;
    /* Tunable reboot outcome (set BEFORE calling reboot helper). */
    bool reboot_force_unauthorized;
    bool reboot_force_unexpected;
} MockState;

static void mock_state_init(MockState *st)
{
    memset(st, 0, sizeof(*st));
    /* Default HELLO carries the canonical UID + session id used by the
     * tests below. */
    snprintf(st->hello_reply, sizeof(st->hello_reply),
             "OK HELLO module=ECU proto=1 session=%lu fw=1.0.0 build=dev "
             "uid=E661A4D1234567AB",
             (unsigned long)k_session_id);
    /* Default AUTH_BEGIN reply: hex of k_challenge. */
    char hex[SC_AUTH_CHALLENGE_BYTES * 2u + 1u];
    static const char k_hex_table[] = "0123456789abcdef";
    for (size_t i = 0u; i < SC_AUTH_CHALLENGE_BYTES; ++i) {
        hex[i * 2u] = k_hex_table[(k_challenge[i] >> 4) & 0x0Fu];
        hex[i * 2u + 1u] = k_hex_table[k_challenge[i] & 0x0Fu];
    }
    hex[SC_AUTH_CHALLENGE_BYTES * 2u] = '\0';
    snprintf(st->auth_begin_reply, sizeof(st->auth_begin_reply),
             "SC_OK AUTH_CHALLENGE %s", hex);
}

static bool mock_list(void *ctx, ScTransportCandidateList *list,
                      char *err, size_t err_size)
{
    (void)ctx; (void)err; (void)err_size;
    list->count = 0u;
    list->truncated = false;
    return true;
}

static bool mock_resolve(void *ctx, const char *candidate, char *out,
                         size_t out_size, char *err, size_t err_size)
{
    (void)ctx; (void)err; (void)err_size;
    snprintf(out, out_size, "%s", candidate);
    return true;
}

static bool mock_hello(void *ctx, const char *path, char *response,
                       size_t response_size, char *err, size_t err_size)
{
    (void)path; (void)err; (void)err_size;
    MockState *st = (MockState *)ctx;
    /* Real firmware behaviour: HELLO mints a new session and clears auth. */
    st->authenticated = false;
    st->challenge_consumed = false;
    snprintf(response, response_size, "%s", st->hello_reply);
    return true;
}

static bool mock_send(void *ctx, const char *path, const char *cmd,
                      char *response, size_t response_size,
                      char *err, size_t err_size)
{
    (void)path; (void)err; (void)err_size;
    MockState *st = (MockState *)ctx;

    if (strcmp(cmd, "SC_AUTH_BEGIN") == 0) {
        st->challenge_consumed = false;
        snprintf(response, response_size, "%s", st->auth_begin_reply);
        return true;
    }

    if (strncmp(cmd, "SC_AUTH_PROVE ", 14u) == 0) {
        const char *provided_hex = cmd + 14u;
        char expected_hex[SC_AUTH_RESPONSE_HEX_BUF_SIZE];
        if (!sc_auth_compute_response_hex(k_uid, sizeof(k_uid),
                                          k_challenge, sizeof(k_challenge),
                                          k_session_id,
                                          expected_hex, sizeof(expected_hex))) {
            snprintf(response, response_size, "SC_AUTH_FAILED mac_compute");
            return true;
        }
        st->challenge_consumed = true;
        if (strcmp(provided_hex, expected_hex) == 0) {
            st->authenticated = true;
            snprintf(response, response_size, "SC_OK AUTH_OK");
        } else {
            snprintf(response, response_size, "SC_AUTH_FAILED bad_mac");
        }
        return true;
    }

    if (strcmp(cmd, "SC_REBOOT_BOOTLOADER") == 0) {
        if (st->reboot_force_unauthorized || !st->authenticated) {
            snprintf(response, response_size, "SC_NOT_AUTHORIZED");
            return true;
        }
        if (st->reboot_force_unexpected) {
            snprintf(response, response_size, "SC_OK SOMETHING_ELSE");
            return true;
        }
        st->reboot_requested = true;
        snprintf(response, response_size, "SC_OK REBOOT");
        return true;
    }

    snprintf(response, response_size, "SC_UNKNOWN_CMD");
    return true;
}

static ScTransport make_transport(MockState *st)
{
    static const ScTransportOps ops = {
        .list_candidates = mock_list,
        .resolve_device_path = mock_resolve,
        .send_hello = mock_hello,
        .send_sc_command = mock_send,
    };
    ScTransport t;
    sc_transport_init_custom(&t, &ops, st);
    return t;
}

/* ── tests ──────────────────────────────────────────────────────────────── */

static int test_authenticate_happy_path(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);

    char err[512] = {0};
    const ScAuthStatus rc = sc_core_authenticate(&t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_AUTH_OK, rc, "happy path returns OK");
    TEST_ASSERT(st.authenticated, "firmware-mock state authenticated");
    TEST_ASSERT(st.challenge_consumed, "challenge consumed by PROVE");
    return 0;
}

static int test_authenticate_rejects_missing_uid_in_hello(void)
{
    MockState st;
    mock_state_init(&st);
    snprintf(st.hello_reply, sizeof(st.hello_reply),
             "OK HELLO module=ECU proto=1 session=42 fw=1.0.0 build=dev");
    ScTransport t = make_transport(&st);

    char err[512] = {0};
    const ScAuthStatus rc = sc_core_authenticate(&t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_AUTH_ERR_HELLO_PARSE, rc, "missing uid -> HELLO_PARSE");
    TEST_ASSERT(!st.authenticated, "no auth without uid");
    return 0;
}

static int test_authenticate_rejects_bad_uid_length(void)
{
    MockState st;
    mock_state_init(&st);
    snprintf(st.hello_reply, sizeof(st.hello_reply),
             "OK HELLO module=ECU proto=1 session=42 fw=1.0.0 build=dev "
             "uid=DEADBEEF");
    ScTransport t = make_transport(&st);

    char err[512] = {0};
    const ScAuthStatus rc = sc_core_authenticate(&t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_AUTH_ERR_HELLO_PARSE, rc, "8-char uid is rejected");
    return 0;
}

static int test_authenticate_rejects_bad_challenge_format(void)
{
    MockState st;
    mock_state_init(&st);
    snprintf(st.auth_begin_reply, sizeof(st.auth_begin_reply),
             "SC_OK AUTH_CHALLENGE shorthex");
    ScTransport t = make_transport(&st);

    char err[512] = {0};
    const ScAuthStatus rc = sc_core_authenticate(&t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_AUTH_ERR_BAD_CHALLENGE, rc, "short challenge rejected");
    return 0;
}

static int test_authenticate_rejects_when_firmware_returns_failed(void)
{
    MockState st;
    mock_state_init(&st);
    /* Force the firmware-mock to advertise a different challenge than the
     * one we use to compute the expected response, so any AUTH_PROVE the
     * orchestrator sends mismatches. */
    snprintf(st.auth_begin_reply, sizeof(st.auth_begin_reply),
             "SC_OK AUTH_CHALLENGE 00112233445566778899aabbccddeeff");
    ScTransport t = make_transport(&st);

    char err[512] = {0};
    const ScAuthStatus rc = sc_core_authenticate(&t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_AUTH_ERR_AUTH_REJECTED, rc, "wrong MAC rejected");
    TEST_ASSERT(!st.authenticated, "firmware-mock not authenticated");
    return 0;
}

static int test_reboot_succeeds_after_authenticate(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);

    char err[512] = {0};
    TEST_ASSERT_EQ(SC_AUTH_OK,
                   sc_core_authenticate(&t, "/dev/mock", err, sizeof(err)),
                   "auth precondition");

    err[0] = '\0';
    const ScRebootStatus rc =
        sc_core_reboot_to_bootloader(&t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_REBOOT_OK, rc, "reboot OK");
    TEST_ASSERT(st.reboot_requested, "firmware-mock saw reboot request");
    return 0;
}

static int test_reboot_without_auth_returns_not_authorized(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);

    char err[512] = {0};
    const ScRebootStatus rc =
        sc_core_reboot_to_bootloader(&t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_REBOOT_ERR_NOT_AUTHORIZED, rc,
                   "reboot before auth -> NOT_AUTHORIZED");
    TEST_ASSERT(!st.reboot_requested, "firmware-mock did not act");
    return 0;
}

static int test_reboot_unexpected_reply_is_flagged(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);

    char err[512] = {0};
    TEST_ASSERT_EQ(SC_AUTH_OK,
                   sc_core_authenticate(&t, "/dev/mock", err, sizeof(err)),
                   "auth precondition");

    st.reboot_force_unexpected = true;
    err[0] = '\0';
    const ScRebootStatus rc =
        sc_core_reboot_to_bootloader(&t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_REBOOT_ERR_UNEXPECTED_REPLY, rc,
                   "garbage reply -> UNEXPECTED_REPLY");
    return 0;
}

static int test_status_strings_are_stable(void)
{
    TEST_ASSERT(strcmp(sc_auth_status_name(SC_AUTH_OK), "OK") == 0, "auth OK");
    TEST_ASSERT(strcmp(sc_auth_status_name(SC_AUTH_ERR_AUTH_REJECTED),
                       "AUTH_REJECTED") == 0,
                "auth rejected");
    TEST_ASSERT(strcmp(sc_reboot_status_name(SC_REBOOT_ERR_NOT_AUTHORIZED),
                       "NOT_AUTHORIZED") == 0,
                "reboot not-authorized");
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += test_authenticate_happy_path();
    failures += test_authenticate_rejects_missing_uid_in_hello();
    failures += test_authenticate_rejects_bad_uid_length();
    failures += test_authenticate_rejects_bad_challenge_format();
    failures += test_authenticate_rejects_when_firmware_returns_failed();
    failures += test_reboot_succeeds_after_authenticate();
    failures += test_reboot_without_auth_returns_not_authorized();
    failures += test_reboot_unexpected_reply_is_flagged();
    failures += test_status_strings_are_stable();

    if (failures != 0) {
        fprintf(stderr, "test_sc_phase5: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    fprintf(stdout, "test_sc_phase5: all 9 tests passed\n");
    return EXIT_SUCCESS;
}

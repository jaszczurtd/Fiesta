/*
 * Phase 8.5 host orchestration: SET_PARAM / COMMIT_PARAMS / REVERT_PARAMS.
 *
 * Drives sc_core_set_param / sc_core_commit_params / sc_core_revert_params
 * through a mock transport that mirrors the firmware-side Phase 8 state
 * machine (authenticated flag, staging vs active mirror, COMMIT-time
 * cross-field validation, RO descriptor rejection). Tests the parser
 * paths in sc_core.c that map firmware reply tokens to the host enum
 * codes; reason tokens for COMMIT_FAILED stay verbatim in error[] so a
 * GUI / shell pipeline can render them without a per-token table.
 */

#include "sc_auth.h"
#include "sc_core.h"
#include "sc_protocol.h"
#include "sc_transport.h"

#include <stdint.h>
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

/* Match the deterministic mock UID baked into hal_mock so the auth
 * handshake works against the production sc_auth helpers. */
static const uint8_t k_uid[8] = {
    0xE6u, 0x61u, 0xA4u, 0xD1u, 0x23u, 0x45u, 0x67u, 0xABu
};
static const uint32_t k_session_id = 0x12345678u;
static const uint8_t k_challenge[SC_AUTH_CHALLENGE_BYTES] = {
    0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
    0x18u, 0x19u, 0x1Au, 0x1Bu, 0x1Cu, 0x1Du, 0x1Eu, 0x1Fu
};

/* One writable id (fan_coolant_start_c, range [70, 130]) and one
 * read-only id (coolant_warn_c). Enough to exercise every reply path
 * without porting the full ECU descriptor table. */
#define MOCK_WRITABLE_ID  "fan_coolant_start_c"
#define MOCK_WRITABLE_MIN 70
#define MOCK_WRITABLE_MAX 130
#define MOCK_READ_ONLY_ID "coolant_warn_c"

typedef struct {
    char hello_reply[256];
    char auth_begin_reply[256];

    bool authenticated;
    int16_t staging_value;
    int16_t active_value;
    /* Tunable failure switches. */
    bool force_commit_failure;
    bool send_should_fail;
    bool hello_should_fail;
} MockState;

static void mock_state_init(MockState *st)
{
    memset(st, 0, sizeof(*st));
    /* Pre-seed both mirrors at the writable-id default 95. */
    st->staging_value = 95;
    st->active_value = 95;

    snprintf(st->hello_reply, sizeof(st->hello_reply),
             "OK HELLO module=" SC_MODULE_TOKEN_ECU
             " proto=1 session=%lu fw=1.0.0 build=dev "
             "uid=E661A4D1234567AB",
             (unsigned long)k_session_id);

    static const char k_hex_table[] = "0123456789abcdef";
    char hex[SC_AUTH_CHALLENGE_BYTES * 2u + 1u];
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
    (void)path;
    MockState *st = (MockState *)ctx;
    if (st->hello_should_fail) {
        snprintf(err, err_size, "mock HELLO transport down");
        return false;
    }
    /* Real firmware behaviour: HELLO mints a new session and clears auth. */
    st->authenticated = false;
    snprintf(response, response_size, "%s", st->hello_reply);
    return true;
}

/* Parse "fan_coolant_start_c 110" -> id_out + value_out. Returns false
 * on malformed payload (non-numeric, missing fields). */
static bool parse_set_param_args(const char *args, char *id_out,
                                 size_t id_out_size, int *value_out)
{
    while (*args == ' ') {
        ++args;
    }
    size_t i = 0u;
    while (args[i] != '\0' && args[i] != ' ' && i + 1u < id_out_size) {
        id_out[i] = args[i];
        ++i;
    }
    id_out[i] = '\0';
    if (args[i] != ' ') {
        return false;
    }
    const char *value_start = args + i;
    while (*value_start == ' ') {
        ++value_start;
    }
    if (*value_start == '\0') {
        return false;
    }
    char *end = NULL;
    const long parsed = strtol(value_start, &end, 10);
    if (end == value_start || (*end != '\0' && *end != ' ')) {
        return false;
    }
    *value_out = (int)parsed;
    return true;
}

static bool mock_send(void *ctx, const char *path, const char *cmd,
                      char *response, size_t response_size,
                      char *err, size_t err_size)
{
    (void)path;
    MockState *st = (MockState *)ctx;

    if (st->send_should_fail) {
        snprintf(err, err_size, "mock send transport down");
        return false;
    }

    if (strcmp(cmd, "SC_AUTH_BEGIN") == 0) {
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
        if (strcmp(provided_hex, expected_hex) == 0) {
            st->authenticated = true;
            snprintf(response, response_size, "SC_OK AUTH_OK");
        } else {
            snprintf(response, response_size, "SC_AUTH_FAILED bad_mac");
        }
        return true;
    }

    if (strncmp(cmd, SC_CMD_SET_PARAM " ",
                sizeof(SC_CMD_SET_PARAM)) == 0) {
        if (!st->authenticated) {
            snprintf(response, response_size, "SC_NOT_AUTHORIZED");
            return true;
        }
        char id[64];
        int value = 0;
        if (!parse_set_param_args(cmd + sizeof(SC_CMD_SET_PARAM), id,
                                  sizeof(id), &value)) {
            snprintf(response, response_size, "SC_BAD_REQUEST malformed");
            return true;
        }
        if (strcmp(id, MOCK_READ_ONLY_ID) == 0) {
            snprintf(response, response_size,
                     "SC_BAD_REQUEST read_only id=%s", id);
            return true;
        }
        if (strcmp(id, MOCK_WRITABLE_ID) != 0) {
            snprintf(response, response_size, "SC_INVALID_PARAM_ID id=%s", id);
            return true;
        }
        if (value < MOCK_WRITABLE_MIN || value > MOCK_WRITABLE_MAX) {
            snprintf(response, response_size,
                     "SC_BAD_REQUEST out_of_range id=%s min=%d max=%d",
                     id, MOCK_WRITABLE_MIN, MOCK_WRITABLE_MAX);
            return true;
        }
        st->staging_value = (int16_t)value;
        snprintf(response, response_size,
                 "SC_OK PARAM_SET id=%s staged=%d active=%d",
                 id, value, (int)st->active_value);
        return true;
    }

    if (strcmp(cmd, SC_CMD_COMMIT_PARAMS) == 0) {
        if (!st->authenticated) {
            snprintf(response, response_size, "SC_NOT_AUTHORIZED");
            return true;
        }
        if (st->force_commit_failure) {
            snprintf(response, response_size,
                     "SC_COMMIT_FAILED reason=fan_coolant_hysteresis");
            return true;
        }
        st->active_value = st->staging_value;
        snprintf(response, response_size, "SC_OK PARAMS_COMMITTED count=1");
        return true;
    }

    if (strcmp(cmd, SC_CMD_REVERT_PARAMS) == 0) {
        if (!st->authenticated) {
            snprintf(response, response_size, "SC_NOT_AUTHORIZED");
            return true;
        }
        st->staging_value = st->active_value;
        snprintf(response, response_size, "SC_OK PARAMS_REVERTED");
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

/* Reusable: drive the auth handshake the mock expects. */
static int authenticate_mock(MockState *st, ScTransport *t)
{
    char err[512] = {0};
    const ScAuthStatus rc = sc_core_authenticate(t, "/dev/mock",
                                                 err, sizeof(err));
    if (rc != SC_AUTH_OK) {
        fprintf(stderr, "auth precondition failed: %s - %s\n",
                sc_auth_status_name(rc), err);
        return 1;
    }
    if (!st->authenticated) {
        fprintf(stderr, "auth precondition: mock state not authenticated\n");
        return 1;
    }
    return 0;
}

/* ── tests ──────────────────────────────────────────────────────────────── */

static int test_set_param_happy_path(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);
    if (authenticate_mock(&st, &t) != 0) return 1;

    char err[512] = {0};
    const ScSetParamStatus rc = sc_core_set_param(
        &t, "/dev/mock", MOCK_WRITABLE_ID, 110, err, sizeof(err));
    TEST_ASSERT_EQ(SC_SET_PARAM_OK, rc, "happy path returns OK");
    TEST_ASSERT_EQ(110, (int)st.staging_value, "staging mirror updated");
    TEST_ASSERT_EQ(95, (int)st.active_value, "active mirror untouched");
    return 0;
}

static int test_set_param_without_auth_returns_not_authorized(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);

    char err[512] = {0};
    const ScSetParamStatus rc = sc_core_set_param(
        &t, "/dev/mock", MOCK_WRITABLE_ID, 110, err, sizeof(err));
    TEST_ASSERT_EQ(SC_SET_PARAM_ERR_NOT_AUTHORIZED, rc,
                   "no auth -> NOT_AUTHORIZED");
    TEST_ASSERT(strstr(err, "SC_NOT_AUTHORIZED") != NULL,
                "error buffer carries firmware reply");
    TEST_ASSERT_EQ(95, (int)st.staging_value, "staging untouched");
    return 0;
}

static int test_set_param_unknown_id_maps_to_invalid_id(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);
    if (authenticate_mock(&st, &t) != 0) return 1;

    char err[512] = {0};
    const ScSetParamStatus rc = sc_core_set_param(
        &t, "/dev/mock", "no_such_param", 0, err, sizeof(err));
    TEST_ASSERT_EQ(SC_SET_PARAM_ERR_INVALID_ID, rc,
                   "unknown id -> INVALID_ID");
    TEST_ASSERT(strstr(err, "SC_INVALID_PARAM_ID") != NULL,
                "error buffer carries firmware reply");
    return 0;
}

static int test_set_param_read_only_maps_to_read_only(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);
    if (authenticate_mock(&st, &t) != 0) return 1;

    char err[512] = {0};
    const ScSetParamStatus rc = sc_core_set_param(
        &t, "/dev/mock", MOCK_READ_ONLY_ID, 100, err, sizeof(err));
    TEST_ASSERT_EQ(SC_SET_PARAM_ERR_READ_ONLY, rc,
                   "RO descriptor -> READ_ONLY");
    TEST_ASSERT(strstr(err, "read_only") != NULL,
                "error buffer carries reason token");
    return 0;
}

static int test_set_param_out_of_range_carries_min_max(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);
    if (authenticate_mock(&st, &t) != 0) return 1;

    char err[512] = {0};
    const ScSetParamStatus rc = sc_core_set_param(
        &t, "/dev/mock", MOCK_WRITABLE_ID, 200, err, sizeof(err));
    TEST_ASSERT_EQ(SC_SET_PARAM_ERR_OUT_OF_RANGE, rc,
                   "above max -> OUT_OF_RANGE");
    TEST_ASSERT(strstr(err, "min=70") != NULL,
                "error buffer carries min");
    TEST_ASSERT(strstr(err, "max=130") != NULL,
                "error buffer carries max");
    return 0;
}

static int test_set_param_transport_failure_returns_transport(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);
    if (authenticate_mock(&st, &t) != 0) return 1;

    st.send_should_fail = true;
    char err[512] = {0};
    const ScSetParamStatus rc = sc_core_set_param(
        &t, "/dev/mock", MOCK_WRITABLE_ID, 110, err, sizeof(err));
    TEST_ASSERT_EQ(SC_SET_PARAM_ERR_TRANSPORT, rc,
                   "send failure -> TRANSPORT");
    return 0;
}

static int test_commit_params_happy_path(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);
    if (authenticate_mock(&st, &t) != 0) return 1;

    /* Stage a value first so commit has something to promote. */
    char err[512] = {0};
    TEST_ASSERT_EQ(SC_SET_PARAM_OK,
                   sc_core_set_param(&t, "/dev/mock", MOCK_WRITABLE_ID, 105,
                                     err, sizeof(err)),
                   "set precondition");

    err[0] = '\0';
    const ScCommitParamsStatus rc = sc_core_commit_params(
        &t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_COMMIT_PARAMS_OK, rc, "commit happy path");
    TEST_ASSERT_EQ(105, (int)st.active_value, "active updated by commit");
    return 0;
}

static int test_commit_failed_carries_reason_in_error(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);
    if (authenticate_mock(&st, &t) != 0) return 1;

    st.force_commit_failure = true;

    char err[512] = {0};
    const ScCommitParamsStatus rc = sc_core_commit_params(
        &t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_COMMIT_PARAMS_ERR_COMMIT_FAILED, rc,
                   "rule violation -> COMMIT_FAILED");
    TEST_ASSERT(strstr(err, "fan_coolant_hysteresis") != NULL,
                "error buffer carries reason token verbatim");
    /* Active mirror untouched on commit failure. */
    TEST_ASSERT_EQ(95, (int)st.active_value,
                   "active untouched on commit failure");
    return 0;
}

static int test_commit_without_auth_returns_not_authorized(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);

    char err[512] = {0};
    const ScCommitParamsStatus rc = sc_core_commit_params(
        &t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_COMMIT_PARAMS_ERR_NOT_AUTHORIZED, rc,
                   "no auth -> NOT_AUTHORIZED");
    return 0;
}

static int test_revert_params_happy_path(void)
{
    MockState st;
    mock_state_init(&st);
    ScTransport t = make_transport(&st);
    if (authenticate_mock(&st, &t) != 0) return 1;

    /* Mutate staging away from active, then revert. */
    char err[512] = {0};
    TEST_ASSERT_EQ(SC_SET_PARAM_OK,
                   sc_core_set_param(&t, "/dev/mock", MOCK_WRITABLE_ID, 120,
                                     err, sizeof(err)),
                   "set precondition");
    TEST_ASSERT_EQ(120, (int)st.staging_value, "staging shifted");

    err[0] = '\0';
    const ScRevertParamsStatus rc = sc_core_revert_params(
        &t, "/dev/mock", err, sizeof(err));
    TEST_ASSERT_EQ(SC_REVERT_PARAMS_OK, rc, "revert happy path");
    TEST_ASSERT_EQ(95, (int)st.staging_value,
                   "staging reset from active by revert");
    return 0;
}

static int test_status_strings_are_stable(void)
{
    TEST_ASSERT(strcmp(sc_set_param_status_name(SC_SET_PARAM_OK),
                       "OK") == 0, "set OK");
    TEST_ASSERT(strcmp(sc_set_param_status_name(SC_SET_PARAM_ERR_OUT_OF_RANGE),
                       "OUT_OF_RANGE") == 0, "set OOR");
    TEST_ASSERT(strcmp(sc_set_param_status_name(SC_SET_PARAM_ERR_READ_ONLY),
                       "READ_ONLY") == 0, "set RO");
    TEST_ASSERT(strcmp(sc_commit_params_status_name(
                           SC_COMMIT_PARAMS_ERR_COMMIT_FAILED),
                       "COMMIT_FAILED") == 0, "commit failed");
    TEST_ASSERT(strcmp(sc_revert_params_status_name(SC_REVERT_PARAMS_OK),
                       "OK") == 0, "revert OK");
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += test_set_param_happy_path();
    failures += test_set_param_without_auth_returns_not_authorized();
    failures += test_set_param_unknown_id_maps_to_invalid_id();
    failures += test_set_param_read_only_maps_to_read_only();
    failures += test_set_param_out_of_range_carries_min_max();
    failures += test_set_param_transport_failure_returns_transport();
    failures += test_commit_params_happy_path();
    failures += test_commit_failed_carries_reason_in_error();
    failures += test_commit_without_auth_returns_not_authorized();
    failures += test_revert_params_happy_path();
    failures += test_status_strings_are_stable();

    if (failures != 0) {
        fprintf(stderr, "test_sc_phase8_host: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    fprintf(stdout, "test_sc_phase8_host: all 11 tests passed\n");
    return EXIT_SUCCESS;
}

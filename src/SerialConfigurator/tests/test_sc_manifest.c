/*
 * SerialConfigurator firmware-manifest tests (Phase 4).
 *
 * Locks the JSON shape, hard-reject behaviour, and the contract of the
 * SHA-256 artifact match. The tests exercise both the in-memory parser
 * and the file-loading wrapper using temporary files in /tmp.
 */

#include "sc_crypto.h"
#include "sc_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s — %s (line %d)\n", __func__, (msg), __LINE__); \
            return 1; \
        } \
    } while (0)

#define TEST_ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            fprintf(stderr, \
                    "FAIL: %s — %s (line %d): got %d, want %d\n", \
                    __func__, (msg), __LINE__, (int)(a), (int)(b)); \
            return 1; \
        } \
    } while (0)

/* "abc" SHA-256 — FIPS 180-2 Appendix B.1. */
static const char *k_abc_sha256_hex =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

static const char *k_minimal_manifest_json =
    "{"
    "  \"module_name\": \"ECU\","
    "  \"fw_version\": \"0.1.0\","
    "  \"build_id\":   \"2026-04-26 12:00:00\","
    "  \"sha256\":     \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\""
    "}";

static int test_parse_minimal_valid_manifest(void)
{
    sc_manifest_t m;
    const sc_manifest_status_t st = sc_manifest_parse(k_minimal_manifest_json,
                                                      strlen(k_minimal_manifest_json),
                                                      &m);
    TEST_ASSERT_EQ(SC_MANIFEST_OK, st, "parse minimal manifest");
    TEST_ASSERT(strcmp(m.module_name, "ECU") == 0, "module_name");
    TEST_ASSERT(strcmp(m.fw_version, "0.1.0") == 0, "fw_version");
    TEST_ASSERT(strcmp(m.build_id, "2026-04-26 12:00:00") == 0, "build_id");
    TEST_ASSERT(strcmp(m.sha256_hex, k_abc_sha256_hex) == 0, "sha256_hex");
    TEST_ASSERT(!m.has_signature, "no signature on minimal manifest");
    return 0;
}

static int test_parse_with_signature(void)
{
    const char *json =
        "{"
        "\"module_name\":\"ECU\","
        "\"fw_version\":\"0.1.0\","
        "\"build_id\":\"2026-04-26 12:00:00\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\","
        "\"signature\":\"AAAAB3NzaC1lZDI1NTE5AAAAIH\""
        "}";
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_OK,
                   sc_manifest_parse(json, strlen(json), &m),
                   "parse with signature");
    TEST_ASSERT(m.has_signature, "signature flag set");
    TEST_ASSERT(strcmp(m.signature, "AAAAB3NzaC1lZDI1NTE5AAAAIH") == 0,
                "signature stored verbatim");
    return 0;
}

static int test_signature_verify_returns_not_supported(void)
{
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_OK,
                   sc_manifest_parse(k_minimal_manifest_json,
                                     strlen(k_minimal_manifest_json), &m),
                   "parse for sig test");
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_SIGNATURE_NOT_SUPPORTED,
                   sc_manifest_verify_signature(&m),
                   "signature verification stub");
    return 0;
}

static int test_missing_required_field_is_rejected(void)
{
    const char *json =
        "{"
        "\"module_name\":\"ECU\","
        "\"fw_version\":\"0.1.0\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\""
        "}";
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_MISSING_FIELD,
                   sc_manifest_parse(json, strlen(json), &m),
                   "missing build_id");
    return 0;
}

static int test_empty_object_is_rejected(void)
{
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_MISSING_FIELD,
                   sc_manifest_parse("{}", 2u, &m),
                   "empty object");
    return 0;
}

static int test_duplicate_field_is_rejected(void)
{
    const char *json =
        "{"
        "\"module_name\":\"ECU\","
        "\"module_name\":\"Clocks\","
        "\"fw_version\":\"0.1.0\","
        "\"build_id\":\"x\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\""
        "}";
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_DUPLICATE_FIELD,
                   sc_manifest_parse(json, strlen(json), &m),
                   "duplicate module_name");
    return 0;
}

static int test_unknown_field_is_rejected(void)
{
    const char *json =
        "{"
        "\"module_name\":\"ECU\","
        "\"fw_version\":\"0.1.0\","
        "\"build_id\":\"x\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\","
        "\"trojan\":\"bad\""
        "}";
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_UNKNOWN_FIELD,
                   sc_manifest_parse(json, strlen(json), &m),
                   "unknown field");
    return 0;
}

static int test_bad_sha256_format_is_rejected(void)
{
    /* Wrong length (63 chars). */
    const char *json_short =
        "{"
        "\"module_name\":\"ECU\","
        "\"fw_version\":\"0.1.0\","
        "\"build_id\":\"x\","
        "\"sha256\":\"a7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\""
        "}";
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_BAD_SHA256_FORMAT,
                   sc_manifest_parse(json_short, strlen(json_short), &m),
                   "63-char sha256");

    /* Uppercase hex must be rejected — manifest contract is lowercase. */
    const char *json_upper =
        "{"
        "\"module_name\":\"ECU\","
        "\"fw_version\":\"0.1.0\","
        "\"build_id\":\"x\","
        "\"sha256\":\"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\""
        "}";
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_BAD_SHA256_FORMAT,
                   sc_manifest_parse(json_upper, strlen(json_upper), &m),
                   "uppercase sha256");

    /* Non-hex char. */
    const char *json_nonhex =
        "{"
        "\"module_name\":\"ECU\","
        "\"fw_version\":\"0.1.0\","
        "\"build_id\":\"x\","
        "\"sha256\":\"za7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\""
        "}";
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_BAD_SHA256_FORMAT,
                   sc_manifest_parse(json_nonhex, strlen(json_nonhex), &m),
                   "non-hex sha256");
    return 0;
}

static int test_empty_field_is_rejected(void)
{
    const char *json =
        "{"
        "\"module_name\":\"\","
        "\"fw_version\":\"0.1.0\","
        "\"build_id\":\"x\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\""
        "}";
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_FIELD_EMPTY,
                   sc_manifest_parse(json, strlen(json), &m),
                   "empty module_name");
    return 0;
}

static int test_field_too_long_is_rejected(void)
{
    /* 33-char module_name (cap is 32). */
    const char *json =
        "{"
        "\"module_name\":\"AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDD\","
        "\"fw_version\":\"0.1.0\","
        "\"build_id\":\"x\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\""
        "}";
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_FIELD_TOO_LONG,
                   sc_manifest_parse(json, strlen(json), &m),
                   "33-char module_name");
    return 0;
}

static int test_trailing_garbage_is_rejected(void)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%sxxx", k_minimal_manifest_json);
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_BAD_JSON,
                   sc_manifest_parse(buf, strlen(buf), &m),
                   "trailing garbage after closing brace");
    return 0;
}

static int test_unsupported_json_features_are_rejected(void)
{
    /* Numeric value where string is expected. */
    const char *json_num =
        "{"
        "\"module_name\":\"ECU\","
        "\"fw_version\":1,"
        "\"build_id\":\"x\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\""
        "}";
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_BAD_JSON,
                   sc_manifest_parse(json_num, strlen(json_num), &m),
                   "numeric value");

    /* Nested object. */
    const char *json_obj =
        "{"
        "\"module_name\":{\"nested\":\"x\"},"
        "\"fw_version\":\"0.1.0\","
        "\"build_id\":\"x\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\""
        "}";
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_BAD_JSON,
                   sc_manifest_parse(json_obj, strlen(json_obj), &m),
                   "nested object value");
    return 0;
}

static int test_module_match_check(void)
{
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_OK,
                   sc_manifest_parse(k_minimal_manifest_json,
                                     strlen(k_minimal_manifest_json), &m),
                   "parse for module match");
    TEST_ASSERT_EQ(SC_MANIFEST_OK,
                   sc_manifest_check_module_match(&m, "ECU"),
                   "match exact");
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_MODULE_MISMATCH,
                   sc_manifest_check_module_match(&m, "Clocks"),
                   "wrong module rejected");
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_MODULE_MISMATCH,
                   sc_manifest_check_module_match(&m, "ecu"),
                   "case-sensitive: lowercase rejected");
    return 0;
}

/* ── file IO and SHA-256 verification ────────────────────────────────────── */

static bool write_tempfile(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    const size_t n = fwrite(data, 1u, len, f);
    (void)fclose(f);
    return n == len;
}

static int test_load_file_round_trip(void)
{
    char path[] = "/tmp/sc_manifest_test_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0, "mkstemp");
    (void)close(fd);

    TEST_ASSERT(write_tempfile(path,
                               k_minimal_manifest_json,
                               strlen(k_minimal_manifest_json)),
                "write tempfile");

    sc_manifest_t m;
    const sc_manifest_status_t st = sc_manifest_load_file(path, &m);
    (void)remove(path);

    TEST_ASSERT_EQ(SC_MANIFEST_OK, st, "load_file");
    TEST_ASSERT(strcmp(m.module_name, "ECU") == 0, "loaded module_name");
    return 0;
}

static int test_load_file_missing_returns_open_error(void)
{
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_FILE_OPEN,
                   sc_manifest_load_file("/nonexistent/path/manifest.json", &m),
                   "missing file");
    return 0;
}

static int test_artifact_hash_match(void)
{
    /* Use the deterministic FIPS vector "abc" so we can hard-code its hash. */
    char path[] = "/tmp/sc_artifact_test_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0, "mkstemp");
    (void)close(fd);

    TEST_ASSERT(write_tempfile(path, "abc", 3u), "write artifact");

    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_OK,
                   sc_manifest_parse(k_minimal_manifest_json,
                                     strlen(k_minimal_manifest_json), &m),
                   "parse for artifact match");
    const sc_manifest_status_t st = sc_manifest_verify_artifact(&m, path);
    (void)remove(path);
    TEST_ASSERT_EQ(SC_MANIFEST_OK, st, "artifact matches manifest sha256");
    return 0;
}

static int test_artifact_hash_mismatch(void)
{
    char path[] = "/tmp/sc_artifact_test_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0, "mkstemp");
    (void)close(fd);

    /* "abd" has a different SHA-256 than "abc". */
    TEST_ASSERT(write_tempfile(path, "abd", 3u), "write artifact");

    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_OK,
                   sc_manifest_parse(k_minimal_manifest_json,
                                     strlen(k_minimal_manifest_json), &m),
                   "parse for artifact mismatch");
    const sc_manifest_status_t st = sc_manifest_verify_artifact(&m, path);
    (void)remove(path);
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_ARTIFACT_HASH_MISMATCH, st,
                   "artifact mismatches manifest sha256");
    return 0;
}

static int test_artifact_missing_returns_open_error(void)
{
    sc_manifest_t m;
    TEST_ASSERT_EQ(SC_MANIFEST_OK,
                   sc_manifest_parse(k_minimal_manifest_json,
                                     strlen(k_minimal_manifest_json), &m),
                   "parse");
    TEST_ASSERT_EQ(SC_MANIFEST_ERR_FILE_OPEN,
                   sc_manifest_verify_artifact(&m,
                                               "/nonexistent/artifact.uf2"),
                   "missing artifact rejected");
    return 0;
}

static int test_status_strings_are_stable(void)
{
    TEST_ASSERT(strcmp(sc_manifest_status_str(SC_MANIFEST_OK), "OK") == 0,
                "OK string");
    TEST_ASSERT(strcmp(sc_manifest_status_str(SC_MANIFEST_ERR_ARTIFACT_HASH_MISMATCH),
                       "ARTIFACT_HASH_MISMATCH") == 0,
                "mismatch string");
    TEST_ASSERT(strcmp(sc_manifest_status_str(SC_MANIFEST_ERR_BAD_SHA256_FORMAT),
                       "BAD_SHA256_FORMAT") == 0,
                "bad sha256 string");
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += test_parse_minimal_valid_manifest();
    failures += test_parse_with_signature();
    failures += test_signature_verify_returns_not_supported();
    failures += test_missing_required_field_is_rejected();
    failures += test_empty_object_is_rejected();
    failures += test_duplicate_field_is_rejected();
    failures += test_unknown_field_is_rejected();
    failures += test_bad_sha256_format_is_rejected();
    failures += test_empty_field_is_rejected();
    failures += test_field_too_long_is_rejected();
    failures += test_trailing_garbage_is_rejected();
    failures += test_unsupported_json_features_are_rejected();
    failures += test_module_match_check();
    failures += test_load_file_round_trip();
    failures += test_load_file_missing_returns_open_error();
    failures += test_artifact_hash_match();
    failures += test_artifact_hash_mismatch();
    failures += test_artifact_missing_returns_open_error();
    failures += test_status_strings_are_stable();

    if (failures != 0) {
        fprintf(stderr, "test_sc_manifest: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    fprintf(stdout, "test_sc_manifest: all 19 tests passed\n");
    return EXIT_SUCCESS;
}

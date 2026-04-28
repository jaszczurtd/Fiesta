/*
 * Phase 6.5 - coverage for sc_core_flash, the end-to-end flashing
 * orchestrator.
 *
 * Each test pre-creates an `mkdtemp` fixture set:
 *   - src dir with a minimal valid UF2 (one block, RP2040 family, with
 *     proper magic/end magic and a stable byte payload),
 *   - optional manifest JSON pointing at the UF2 by sha256,
 *   - bootsel parent containing the RPI-RP2 child (so the watcher
 *     hits on the first iteration),
 *   - by-id parent containing a usb-Jaszczur_Fiesta_<MOD>_<UID>-if00
 *     entry (so the re-enumeration wait hits on the first iteration).
 *
 * Pre-creating the BOOTSEL + by-id entries means the test never has
 * to spawn worker threads - the polling latency itself is covered by
 * the dedicated 6.3 / 6.4 suites. This suite focuses on the
 * orchestrator's branching logic.
 *
 * The mock transport mirrors the firmware-side state machine borrowed
 * from test_sc_phase5.c: HELLO returns a configured identity string,
 * SC_AUTH_BEGIN issues a known challenge, SC_AUTH_PROVE validates the
 * response against the canonical mock UID, SC_REBOOT_BOOTLOADER ACKs
 * once auth is complete. The mock additionally tracks how many HELLOs
 * were observed so the test can switch the post-flash identity (for
 * the fw_version-mismatch case).
 */

#include "sc_auth.h"
#include "sc_core.h"
#include "sc_crypto.h"
#include "sc_fiesta_module_tokens.h"
#include "sc_flash.h"
#include "sc_manifest.h"
#include "sc_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TEST_ASSERT(cond, msg)                                                \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "[FAIL] %s - %s (line %d)\n",                     \
                    __func__, (msg), __LINE__);                               \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/* ── Mock UID + state ───────────────────────────────────────────── */

static const uint8_t k_uid[8] = {
    0xE6u, 0x61u, 0xA4u, 0xD1u, 0x23u, 0x45u, 0x67u, 0xABu
};
static const char *const k_uid_hex = "E661A4D1234567AB";
static const uint32_t k_session_id_pre = 0x12345678u;
static const uint32_t k_session_id_post = 0x12345679u;
static const uint8_t k_challenge[16] = {
    0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
    0x18u, 0x19u, 0x1Au, 0x1Bu, 0x1Cu, 0x1Du, 0x1Eu, 0x1Fu
};

typedef struct {
    char hello_pre_reply[256];
    char hello_post_reply[256];
    char auth_begin_reply[256];
    bool authenticated;
    bool reboot_force_unauthorized;
    int hello_call_count; /* increments on every send_hello */
    int sc_command_call_count;
} MockState;

static void challenge_hex(char *out, size_t out_size) {
    static const char k_hex_table[] = "0123456789abcdef";
    if (out_size < sizeof(k_challenge) * 2u + 1u) return;
    for (size_t i = 0u; i < sizeof(k_challenge); ++i) {
        out[i * 2u] = k_hex_table[(k_challenge[i] >> 4) & 0x0Fu];
        out[i * 2u + 1u] = k_hex_table[k_challenge[i] & 0x0Fu];
    }
    out[sizeof(k_challenge) * 2u] = '\0';
}

static void mock_state_init(MockState *st, const char *fw_version_post) {
    memset(st, 0, sizeof(*st));
    snprintf(st->hello_pre_reply, sizeof(st->hello_pre_reply),
             "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=%lu fw=1.0.0 build=dev "
             "uid=%s",
             (unsigned long)k_session_id_pre, k_uid_hex);
    snprintf(st->hello_post_reply, sizeof(st->hello_post_reply),
             "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=%lu fw=%s build=dev "
             "uid=%s",
             (unsigned long)k_session_id_post,
             (fw_version_post != NULL) ? fw_version_post : "1.0.0",
             k_uid_hex);
    char hex[33];
    challenge_hex(hex, sizeof(hex));
    snprintf(st->auth_begin_reply, sizeof(st->auth_begin_reply),
             "SC_OK AUTH_CHALLENGE %s", hex);
}

static bool mock_list(void *ctx, ScTransportCandidateList *list,
                      char *err, size_t err_size) {
    (void)ctx; (void)err; (void)err_size;
    list->count = 0u;
    list->truncated = false;
    return true;
}
static bool mock_resolve(void *ctx, const char *candidate, char *out,
                         size_t out_size, char *err, size_t err_size) {
    (void)ctx; (void)err; (void)err_size;
    snprintf(out, out_size, "%s", candidate);
    return true;
}

static bool mock_hello(void *ctx, const char *path, char *response,
                       size_t response_size, char *err, size_t err_size) {
    (void)path; (void)err; (void)err_size;
    MockState *st = (MockState *)ctx;
    st->authenticated = false;
    st->hello_call_count++;
    if (st->hello_call_count == 1) {
        snprintf(response, response_size, "%s", st->hello_pre_reply);
    } else {
        snprintf(response, response_size, "%s", st->hello_post_reply);
    }
    return true;
}

static bool mock_send(void *ctx, const char *path, const char *cmd,
                      char *response, size_t response_size,
                      char *err, size_t err_size) {
    (void)path; (void)err; (void)err_size;
    MockState *st = (MockState *)ctx;
    st->sc_command_call_count++;

    if (strcmp(cmd, "SC_AUTH_BEGIN") == 0) {
        snprintf(response, response_size, "%s", st->auth_begin_reply);
        return true;
    }
    if (strncmp(cmd, "SC_AUTH_PROVE ", 14u) == 0) {
        const char *provided = cmd + 14u;
        char expected[SC_AUTH_RESPONSE_HEX_BUF_SIZE];
        if (!sc_auth_compute_response_hex(k_uid, sizeof(k_uid),
                                          k_challenge, sizeof(k_challenge),
                                          k_session_id_pre,
                                          expected, sizeof(expected))) {
            snprintf(response, response_size, "SC_AUTH_FAILED mac_compute");
            return true;
        }
        if (strcmp(provided, expected) == 0) {
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
        snprintf(response, response_size, "SC_OK REBOOT");
        return true;
    }
    snprintf(response, response_size, "SC_UNKNOWN_CMD");
    return true;
}

static ScTransport make_transport(MockState *st) {
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

/* ── Filesystem fixtures ────────────────────────────────────────── */

typedef struct {
    char src_dir[256];
    char src_uf2_path[512];
    char manifest_path[512];
    char bootsel_parent[256];
    char bootsel_drive[512];
    char by_id_parent[256];
    char by_id_entry[512];
    uint8_t uf2[512];
    char uf2_sha256_hex[65];
} FsFixture;

static int make_temp_dir(const char *tag, char *out, size_t out_size) {
    if (out_size < 32u) return -1;
    (void)snprintf(out, out_size, "/tmp/sc_flash_%s_XXXXXX", tag);
    return (mkdtemp(out) != NULL) ? 0 : -1;
}

static int write_file(const char *path, const void *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return -1;
    const size_t put = fwrite(buf, 1u, len, f);
    (void)fclose(f);
    return (put == len) ? 0 : -1;
}

/* Build a minimal valid UF2 (one block) with a stable payload so the
 * sha256 is deterministic per test. Writes the bytes into @p out.fs */
static void build_minimal_uf2(uint8_t out[512]) {
    memset(out, 0, 512);
    /* magic1 */
    out[0] = 0x55; out[1] = 0x46; out[2] = 0x32; out[3] = 0x0A;
    /* magic2 */
    out[4] = 0x57; out[5] = 0x51; out[6] = 0x5D; out[7] = 0x9E;
    /* flags (8-11), target_addr (12-15), payload_size (16-19): all 0/256. */
    out[16] = 0x00; out[17] = 0x01; /* payload_size = 256 LE */
    /* block_no (20-23) = 0, total_blocks (24-27) = 1 */
    out[24] = 0x01;
    /* family id (28-31) = RP2040 0xE48BFF56 */
    out[28] = 0x56; out[29] = 0xFF; out[30] = 0x8B; out[31] = 0xE4;
    /* payload bytes - leave as 0; family only checked at offset 28-31. */
    /* end_magic (508-511) = 0x0AB16F30 */
    out[508] = 0x30; out[509] = 0x6F; out[510] = 0xB1; out[511] = 0x0A;
}

/* Construct the entire fixture set: temp dirs, UF2 file, BOOTSEL drive
 * directory, by-id entry, optional manifest. Caller passes a manifest
 * fw_version string to embed; pass NULL to skip the manifest write. */
static int fs_fixture_setup(FsFixture *fx,
                            const char *manifest_module,
                            const char *manifest_fw,
                            const char *manifest_sha_override) {
    memset(fx, 0, sizeof(*fx));

    if (make_temp_dir("src", fx->src_dir, sizeof(fx->src_dir)) != 0) return -1;
    (void)snprintf(fx->src_uf2_path, sizeof(fx->src_uf2_path),
                   "%s/firmware.uf2", fx->src_dir);
    build_minimal_uf2(fx->uf2);
    if (write_file(fx->src_uf2_path, fx->uf2, sizeof(fx->uf2)) != 0) return -1;

    /* Compute the artifact's true sha256 so the happy-path manifest
     * matches it and the artifact-mismatch test can override. */
    if (!sc_crypto_sha256_hex(fx->uf2, sizeof(fx->uf2),
                              fx->uf2_sha256_hex,
                              sizeof(fx->uf2_sha256_hex))) {
        return -1;
    }

    if (manifest_module != NULL && manifest_fw != NULL) {
        (void)snprintf(fx->manifest_path, sizeof(fx->manifest_path),
                       "%s/manifest.json", fx->src_dir);
        const char *sha = (manifest_sha_override != NULL)
                              ? manifest_sha_override
                              : fx->uf2_sha256_hex;
        char json[1024];
        const int n = snprintf(json, sizeof(json),
            "{\"module_name\":\"%s\","
            "\"fw_version\":\"%s\","
            "\"build_id\":\"test-build\","
            "\"sha256\":\"%s\"}",
            manifest_module, manifest_fw, sha);
        if (n <= 0 || (size_t)n >= sizeof(json)) return -1;
        if (write_file(fx->manifest_path, json, (size_t)n) != 0) return -1;
    }

    if (make_temp_dir("bs", fx->bootsel_parent, sizeof(fx->bootsel_parent)) != 0) return -1;
    (void)snprintf(fx->bootsel_drive, sizeof(fx->bootsel_drive),
                   "%s/RPI-RP2", fx->bootsel_parent);
    if (mkdir(fx->bootsel_drive, 0755) != 0) return -1;

    if (make_temp_dir("byid", fx->by_id_parent, sizeof(fx->by_id_parent)) != 0) return -1;
    (void)snprintf(fx->by_id_entry, sizeof(fx->by_id_entry),
                   "%s/usb-Jaszczur_Fiesta_" SC_MODULE_TOKEN_ECU "_%s-if00",
                   fx->by_id_parent, k_uid_hex);
    if (write_file(fx->by_id_entry, "", 0u) != 0) return -1;

    return 0;
}

static void fs_fixture_teardown(FsFixture *fx) {
    /* sc_flash_copy_uf2 lands a firmware.uf2 inside bootsel_drive. */
    char dest[640];
    (void)snprintf(dest, sizeof(dest), "%s/firmware.uf2", fx->bootsel_drive);
    (void)unlink(dest);
    (void)rmdir(fx->bootsel_drive);
    (void)rmdir(fx->bootsel_parent);
    (void)unlink(fx->by_id_entry);
    (void)rmdir(fx->by_id_parent);
    if (fx->manifest_path[0] != '\0') {
        (void)unlink(fx->manifest_path);
    }
    (void)unlink(fx->src_uf2_path);
    (void)rmdir(fx->src_dir);
}

static ScFlashOptions tight_options(const FsFixture *fx) {
    ScFlashOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.bootsel_parents[0] = fx->bootsel_parent;
    opts.bootsel_parents[1] = NULL;
    opts.by_id_parent = fx->by_id_parent;
    opts.bootsel_timeout_ms = 500u;
    opts.reenum_timeout_ms = 500u;
    opts.grace_after_reenum_ms = 1u;
    return opts;
}

/* ── Tests ───────────────────────────────────────────────────────── */

static int test_happy_path_with_manifest(void) {
    FsFixture fx;
    TEST_ASSERT(fs_fixture_setup(&fx, SC_MODULE_TOKEN_ECU, "1.0.0", NULL) == 0,
                "fs_fixture_setup");
    MockState st;
    mock_state_init(&st, "1.0.0");
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    char err[512] = {0};
    const ScFlashStatus rc = sc_core_flash(
        &t, /*module_index=*/0, /*device_path=*/"/dev/mock", k_uid_hex,
        fx.src_uf2_path, fx.manifest_path, &opts,
        NULL, NULL, err, sizeof(err));

    TEST_ASSERT(rc == SC_FLASH_STATUS_OK, sc_flash_status_name(rc));
    TEST_ASSERT(st.hello_call_count == 2, "pre-flash + post-flash HELLO");
    /* st.authenticated is cleared by the post-flash HELLO (firmware
     * mints a new session_id on each HELLO and drops the auth state
     * with it; the mock mirrors that). What we care about here is
     * that it was raised mid-flow, which the AUTH_PROVE branch in
     * mock_send sets - and which is implicit in the OK status. */
    fs_fixture_teardown(&fx);
    return 0;
}

static int test_happy_path_no_manifest(void) {
    FsFixture fx;
    TEST_ASSERT(fs_fixture_setup(&fx, NULL, NULL, NULL) == 0, "fixture");
    MockState st;
    mock_state_init(&st, "9.9.9"); /* any fw, no manifest to check against */
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    const ScFlashStatus rc = sc_core_flash(
        &t, 0u, "/dev/mock", k_uid_hex,
        fx.src_uf2_path, NULL, &opts,
        NULL, NULL, NULL, 0u);
    TEST_ASSERT(rc == SC_FLASH_STATUS_OK, sc_flash_status_name(rc));
    fs_fixture_teardown(&fx);
    return 0;
}

static int test_format_check_rejects_corrupt_uf2(void) {
    FsFixture fx;
    TEST_ASSERT(fs_fixture_setup(&fx, NULL, NULL, NULL) == 0, "fixture");
    /* Overwrite the magic with garbage so the format check fails. */
    uint8_t bad[512];
    memcpy(bad, fx.uf2, sizeof(bad));
    bad[0] = 0xFF;
    TEST_ASSERT(write_file(fx.src_uf2_path, bad, sizeof(bad)) == 0, "rewrite");

    MockState st;
    mock_state_init(&st, "1.0.0");
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    char err[512] = {0};
    const ScFlashStatus rc = sc_core_flash(
        &t, 0u, "/dev/mock", k_uid_hex, fx.src_uf2_path, NULL,
        &opts, NULL, NULL, err, sizeof(err));
    TEST_ASSERT(rc == SC_FLASH_STATUS_FORMAT_REJECTED,
                sc_flash_status_name(rc));
    TEST_ASSERT(st.hello_call_count == 0,
                "format check refuses BEFORE auth");
    fs_fixture_teardown(&fx);
    return 0;
}

static int test_manifest_module_mismatch(void) {
    FsFixture fx;
    /* Manifest declares Clocks but module_index=0 (ECU). */
    TEST_ASSERT(fs_fixture_setup(&fx, SC_MODULE_TOKEN_CLOCKS, "1.0.0", NULL) == 0,
                "fixture");
    MockState st;
    mock_state_init(&st, "1.0.0");
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    char err[512] = {0};
    const ScFlashStatus rc = sc_core_flash(
        &t, /*module_index=*/0, "/dev/mock", k_uid_hex,
        fx.src_uf2_path, fx.manifest_path, &opts,
        NULL, NULL, err, sizeof(err));
    TEST_ASSERT(rc == SC_FLASH_STATUS_MANIFEST_MODULE_MISMATCH,
                sc_flash_status_name(rc));
    TEST_ASSERT(st.hello_call_count == 0,
                "module mismatch refuses BEFORE auth");
    fs_fixture_teardown(&fx);
    return 0;
}

static int test_manifest_artifact_mismatch(void) {
    FsFixture fx;
    /* Manifest declares an obviously-wrong sha256. */
    static const char *const k_bad_sha =
        "0000000000000000000000000000000000000000000000000000000000000000";
    TEST_ASSERT(fs_fixture_setup(&fx, SC_MODULE_TOKEN_ECU, "1.0.0", k_bad_sha) == 0,
                "fixture");
    MockState st;
    mock_state_init(&st, "1.0.0");
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    const ScFlashStatus rc = sc_core_flash(
        &t, 0u, "/dev/mock", k_uid_hex,
        fx.src_uf2_path, fx.manifest_path, &opts,
        NULL, NULL, NULL, 0u);
    TEST_ASSERT(rc == SC_FLASH_STATUS_MANIFEST_ARTIFACT_MISMATCH,
                sc_flash_status_name(rc));
    TEST_ASSERT(st.hello_call_count == 0, "artifact mismatch is pre-auth");
    fs_fixture_teardown(&fx);
    return 0;
}

static int test_auth_failure_propagates(void) {
    FsFixture fx;
    TEST_ASSERT(fs_fixture_setup(&fx, NULL, NULL, NULL) == 0, "fixture");
    MockState st;
    mock_state_init(&st, "1.0.0");
    /* Sabotage the AUTH_BEGIN reply so PROVE will fail. */
    snprintf(st.auth_begin_reply, sizeof(st.auth_begin_reply),
             "SC_AUTH_FAILED key_derivation");
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    const ScFlashStatus rc = sc_core_flash(
        &t, 0u, "/dev/mock", k_uid_hex,
        fx.src_uf2_path, NULL, &opts,
        NULL, NULL, NULL, 0u);
    TEST_ASSERT(rc == SC_FLASH_STATUS_AUTH_FAILED,
                sc_flash_status_name(rc));
    fs_fixture_teardown(&fx);
    return 0;
}

static int test_reboot_failure_propagates(void) {
    FsFixture fx;
    TEST_ASSERT(fs_fixture_setup(&fx, NULL, NULL, NULL) == 0, "fixture");
    MockState st;
    mock_state_init(&st, "1.0.0");
    st.reboot_force_unauthorized = true;
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    const ScFlashStatus rc = sc_core_flash(
        &t, 0u, "/dev/mock", k_uid_hex,
        fx.src_uf2_path, NULL, &opts,
        NULL, NULL, NULL, 0u);
    TEST_ASSERT(rc == SC_FLASH_STATUS_REBOOT_FAILED,
                sc_flash_status_name(rc));
    fs_fixture_teardown(&fx);
    return 0;
}

static int test_bootsel_timeout_when_drive_never_appears(void) {
    FsFixture fx;
    TEST_ASSERT(fs_fixture_setup(&fx, NULL, NULL, NULL) == 0, "fixture");
    /* Remove the pre-created BOOTSEL drive directory. */
    (void)rmdir(fx.bootsel_drive);

    MockState st;
    mock_state_init(&st, "1.0.0");
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    const ScFlashStatus rc = sc_core_flash(
        &t, 0u, "/dev/mock", k_uid_hex,
        fx.src_uf2_path, NULL, &opts,
        NULL, NULL, NULL, 0u);
    TEST_ASSERT(rc == SC_FLASH_STATUS_BOOTSEL_TIMEOUT,
                sc_flash_status_name(rc));
    fs_fixture_teardown(&fx);
    return 0;
}

static int test_reenum_timeout_when_byid_entry_missing(void) {
    FsFixture fx;
    TEST_ASSERT(fs_fixture_setup(&fx, NULL, NULL, NULL) == 0, "fixture");
    /* Wipe the by-id entry so the re-enum waiter starves. */
    (void)unlink(fx.by_id_entry);

    MockState st;
    mock_state_init(&st, "1.0.0");
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    const ScFlashStatus rc = sc_core_flash(
        &t, 0u, "/dev/mock", k_uid_hex,
        fx.src_uf2_path, NULL, &opts,
        NULL, NULL, NULL, 0u);
    TEST_ASSERT(rc == SC_FLASH_STATUS_REENUM_TIMEOUT,
                sc_flash_status_name(rc));
    fs_fixture_teardown(&fx);
    return 0;
}

static int test_post_flash_fw_mismatch_with_manifest(void) {
    FsFixture fx;
    TEST_ASSERT(fs_fixture_setup(&fx, SC_MODULE_TOKEN_ECU, "1.0.0", NULL) == 0, "fixture");
    MockState st;
    /* Post-flash HELLO claims a different fw_version than the manifest. */
    mock_state_init(&st, "9.9.9");
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    char err[512] = {0};
    const ScFlashStatus rc = sc_core_flash(
        &t, 0u, "/dev/mock", k_uid_hex,
        fx.src_uf2_path, fx.manifest_path, &opts,
        NULL, NULL, err, sizeof(err));
    TEST_ASSERT(rc == SC_FLASH_STATUS_POST_FLASH_FW_MISMATCH,
                sc_flash_status_name(rc));
    TEST_ASSERT(strstr(err, "9.9.9") != NULL,
                "diagnostic mentions device fw");
    fs_fixture_teardown(&fx);
    return 0;
}

typedef struct {
    bool seen[8];
    size_t copy_calls;
    uint64_t copy_last_total;
} progress_cap_t;

static void on_orchestrator_progress(ScFlashPhase phase,
                                     uint64_t bytes_written,
                                     uint64_t bytes_total, void *user) {
    progress_cap_t *cap = (progress_cap_t *)user;
    if ((int)phase >= 0 && (int)phase < 8) {
        cap->seen[(int)phase] = true;
    }
    if (phase == SC_FLASH_PHASE_COPY) {
        cap->copy_calls++;
        cap->copy_last_total = bytes_total;
        (void)bytes_written;
    }
}

static int test_progress_callback_phases_flow(void) {
    FsFixture fx;
    TEST_ASSERT(fs_fixture_setup(&fx, NULL, NULL, NULL) == 0, "fixture");
    MockState st;
    mock_state_init(&st, "1.0.0");
    ScTransport t = make_transport(&st);
    const ScFlashOptions opts = tight_options(&fx);

    progress_cap_t cap;
    memset(&cap, 0, sizeof(cap));

    const ScFlashStatus rc = sc_core_flash(
        &t, 0u, "/dev/mock", k_uid_hex,
        fx.src_uf2_path, NULL, &opts,
        on_orchestrator_progress, &cap, NULL, 0u);
    TEST_ASSERT(rc == SC_FLASH_STATUS_OK, sc_flash_status_name(rc));
    /* Without a manifest the manifest-verify phase is skipped. */
    TEST_ASSERT(cap.seen[SC_FLASH_PHASE_FORMAT_CHECK], "format-check phase");
    TEST_ASSERT(cap.seen[SC_FLASH_PHASE_AUTHENTICATE], "auth phase");
    TEST_ASSERT(cap.seen[SC_FLASH_PHASE_REBOOT_TO_BOOTLOADER], "reboot phase");
    TEST_ASSERT(cap.seen[SC_FLASH_PHASE_WAIT_BOOTSEL], "bootsel phase");
    TEST_ASSERT(cap.seen[SC_FLASH_PHASE_COPY], "copy phase");
    TEST_ASSERT(cap.seen[SC_FLASH_PHASE_WAIT_REENUM], "reenum phase");
    TEST_ASSERT(cap.seen[SC_FLASH_PHASE_POST_FLASH_HELLO],
                "post-flash hello phase");
    TEST_ASSERT(cap.copy_calls > 0u, "copy phase fired progress");
    TEST_ASSERT(cap.copy_last_total == 512u,
                "copy progress reports total bytes");
    fs_fixture_teardown(&fx);
    return 0;
}

static int test_status_and_phase_names_stable(void) {
    TEST_ASSERT(strcmp(sc_flash_status_name(SC_FLASH_STATUS_OK), "OK") == 0,
                "OK status");
    TEST_ASSERT(strcmp(sc_flash_status_name(SC_FLASH_STATUS_BOOTSEL_TIMEOUT),
                       "BOOTSEL_TIMEOUT") == 0, "BOOTSEL_TIMEOUT status");
    TEST_ASSERT(strcmp(sc_flash_status_name(
                       SC_FLASH_STATUS_MANIFEST_MODULE_MISMATCH),
                       "MANIFEST_MODULE_MISMATCH") == 0,
                "MANIFEST_MODULE_MISMATCH status");
    TEST_ASSERT(strcmp(sc_flash_phase_name(SC_FLASH_PHASE_COPY), "COPY") == 0,
                "COPY phase");
    TEST_ASSERT(strcmp(sc_flash_phase_name(SC_FLASH_PHASE_POST_FLASH_HELLO),
                       "POST_FLASH_HELLO") == 0, "POST_FLASH_HELLO phase");
    return 0;
}

static int test_null_arg_rejection(void) {
    char err[64];
    /* All required pointers NULL except one - should get NULL_ARG. */
    TEST_ASSERT(sc_core_flash(NULL, 0u, "/dev/x", "AB", "/tmp/x", NULL,
                              NULL, NULL, NULL, err, sizeof(err)) ==
                SC_FLASH_STATUS_NULL_ARG, "NULL transport");
    MockState st;
    mock_state_init(&st, "1.0.0");
    ScTransport t = make_transport(&st);
    TEST_ASSERT(sc_core_flash(&t, 0u, NULL, "AB", "/tmp/x", NULL,
                              NULL, NULL, NULL, err, sizeof(err)) ==
                SC_FLASH_STATUS_NULL_ARG, "NULL device_path");
    TEST_ASSERT(sc_core_flash(&t, 0u, "/dev/x", NULL, "/tmp/x", NULL,
                              NULL, NULL, NULL, err, sizeof(err)) ==
                SC_FLASH_STATUS_NULL_ARG, "NULL uid");
    TEST_ASSERT(sc_core_flash(&t, 0u, "/dev/x", "", "/tmp/x", NULL,
                              NULL, NULL, NULL, err, sizeof(err)) ==
                SC_FLASH_STATUS_NULL_ARG, "empty uid");
    TEST_ASSERT(sc_core_flash(&t, 0u, "/dev/x", "AB", NULL, NULL,
                              NULL, NULL, NULL, err, sizeof(err)) ==
                SC_FLASH_STATUS_NULL_ARG, "NULL uf2_path");
    TEST_ASSERT(sc_core_flash(&t, /*bad index=*/99u, "/dev/x", "AB",
                              "/tmp/x", NULL, NULL, NULL, NULL,
                              err, sizeof(err)) ==
                SC_FLASH_STATUS_NULL_ARG, "out-of-range module index");
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_happy_path_with_manifest();
    failures += test_happy_path_no_manifest();
    failures += test_format_check_rejects_corrupt_uf2();
    failures += test_manifest_module_mismatch();
    failures += test_manifest_artifact_mismatch();
    failures += test_auth_failure_propagates();
    failures += test_reboot_failure_propagates();
    failures += test_bootsel_timeout_when_drive_never_appears();
    failures += test_reenum_timeout_when_byid_entry_missing();
    failures += test_post_flash_fw_mismatch_with_manifest();
    failures += test_progress_callback_phases_flow();
    failures += test_status_and_phase_names_stable();
    failures += test_null_arg_rejection();
    if (failures == 0) {
        printf("[OK] sc_core_flash orchestrator: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "[FAIL] sc_core_flash orchestrator: %d test(s) failed\n",
            failures);
    return 1;
}

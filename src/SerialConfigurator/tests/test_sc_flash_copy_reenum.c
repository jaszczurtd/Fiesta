/*
 * Phase 6.4 - coverage for sc_flash_copy_uf2 and
 * sc_flash_wait_reenumeration.
 *
 * `sc_flash_copy_uf2` is exercised against a deterministic source
 * file and an `mkdtemp` destination directory. The progress callback
 * captures invocation count and cumulative byte totals so the test
 * can assert chunked behaviour without wiring real hardware.
 *
 * `sc_flash_wait_reenumeration` is driven through the test entry
 * point `sc_flash__wait_reenumeration_in` so the suite never touches
 * `/dev/serial/by-id`. A pthread worker drops a fake entry into the
 * temp parent mid-test; the watcher should pick it up by UID-suffix
 * substring match.
 */

#include "sc_flash.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define TEST_ASSERT(cond, msg)                                                \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "[FAIL] %s\n", msg);                              \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int make_temp_dir(char *out, size_t out_size, const char *tag) {
    if (out_size < 32u) return -1;
    (void)snprintf(out, out_size, "/tmp/sc_flash_%s_XXXXXX", tag);
    return (mkdtemp(out) != NULL) ? 0 : -1;
}

/* Build a deterministic source file at <dir>/src.uf2 containing
 * `total` bytes where byte i is `(uint8_t)(i & 0xFFu)`. Returns the
 * absolute path via @p out_src. */
static int make_src_file(const char *dir, size_t total,
                         char *out_src, size_t out_src_size) {
    if (snprintf(out_src, out_src_size, "%s/src.uf2", dir) <= 0) {
        return -1;
    }
    FILE *f = fopen(out_src, "wb");
    if (f == NULL) return -1;
    for (size_t i = 0u; i < total; ++i) {
        const uint8_t byte = (uint8_t)(i & 0xFFu);
        if (fwrite(&byte, 1u, 1u, f) != 1u) {
            (void)fclose(f);
            return -1;
        }
    }
    (void)fclose(f);
    return 0;
}

static int read_whole_file(const char *path, uint8_t *out, size_t expected) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;
    const size_t got = fread(out, 1u, expected, f);
    (void)fclose(f);
    return (got == expected) ? 0 : -1;
}

/* ── Progress capture ────────────────────────────────────────────── */

typedef struct {
    size_t calls;
    uint64_t last_written;
    uint64_t last_total;
    uint64_t monotonic_check; /* asserted to never decrease */
    bool monotonic_violation;
} progress_capture_t;

static void on_progress(uint64_t bytes_written, uint64_t total_bytes,
                        void *user) {
    progress_capture_t *cap = (progress_capture_t *)user;
    if (bytes_written < cap->monotonic_check) {
        cap->monotonic_violation = true;
    }
    cap->monotonic_check = bytes_written;
    cap->last_written = bytes_written;
    cap->last_total = total_bytes;
    cap->calls++;
}

/* ── sc_flash_copy_uf2 tests ─────────────────────────────────────── */

static int test_copy_writes_destination_byte_for_byte(void) {
    char src_dir[256];
    char dst_dir[256];
    TEST_ASSERT(make_temp_dir(src_dir, sizeof(src_dir), "src") == 0,
                "mkdtemp src");
    TEST_ASSERT(make_temp_dir(dst_dir, sizeof(dst_dir), "dst") == 0,
                "mkdtemp dst");

    char src_path[512];
    const size_t total = 200u * 1024u; /* 200 KiB -> 4 chunks of 64 KiB
                                          plus an 8 KiB tail. */
    TEST_ASSERT(make_src_file(src_dir, total,
                              src_path, sizeof(src_path)) == 0,
                "make_src_file");

    progress_capture_t cap = {0};
    char err[256] = {0};
    const sc_flash_status_t st = sc_flash_copy_uf2(
        src_path, dst_dir, on_progress, &cap, err, sizeof(err));

    TEST_ASSERT(st == SC_FLASH_OK, "copy returned OK");
    TEST_ASSERT(cap.calls >= 4u, "progress fired at least 4 times");
    TEST_ASSERT(cap.last_written == (uint64_t)total,
                "final progress equals total");
    TEST_ASSERT(cap.last_total == (uint64_t)total,
                "progress total matches source size");
    TEST_ASSERT(!cap.monotonic_violation,
                "progress bytes_written never decreased");

    char dest_path[512];
    (void)snprintf(dest_path, sizeof(dest_path),
                   "%s/firmware.uf2", dst_dir);
    uint8_t *expected_buf = (uint8_t *)malloc(total);
    uint8_t *got_buf = (uint8_t *)malloc(total);
    TEST_ASSERT(expected_buf != NULL && got_buf != NULL, "malloc");
    for (size_t i = 0u; i < total; ++i) {
        expected_buf[i] = (uint8_t)(i & 0xFFu);
    }
    TEST_ASSERT(read_whole_file(dest_path, got_buf, total) == 0,
                "read destination");
    TEST_ASSERT(memcmp(expected_buf, got_buf, total) == 0,
                "destination matches source byte-for-byte");

    free(expected_buf);
    free(got_buf);
    (void)unlink(dest_path);
    (void)unlink(src_path);
    (void)rmdir(src_dir);
    (void)rmdir(dst_dir);
    return 0;
}

static int test_copy_progress_called_with_null_cb(void) {
    char src_dir[256];
    char dst_dir[256];
    TEST_ASSERT(make_temp_dir(src_dir, sizeof(src_dir), "src") == 0,
                "mkdtemp src");
    TEST_ASSERT(make_temp_dir(dst_dir, sizeof(dst_dir), "dst") == 0,
                "mkdtemp dst");

    char src_path[512];
    TEST_ASSERT(make_src_file(src_dir, 1024u,
                              src_path, sizeof(src_path)) == 0, "src");

    const sc_flash_status_t st = sc_flash_copy_uf2(
        src_path, dst_dir, NULL, NULL, NULL, 0u);
    TEST_ASSERT(st == SC_FLASH_OK, "copy with NULL progress works");

    char dest_path[512];
    (void)snprintf(dest_path, sizeof(dest_path),
                   "%s/firmware.uf2", dst_dir);
    (void)unlink(dest_path);
    (void)unlink(src_path);
    (void)rmdir(src_dir);
    (void)rmdir(dst_dir);
    return 0;
}

static int test_copy_rejects_missing_source(void) {
    char dst_dir[256];
    TEST_ASSERT(make_temp_dir(dst_dir, sizeof(dst_dir), "dst") == 0,
                "mkdtemp dst");
    char err[256] = {0};
    const sc_flash_status_t st = sc_flash_copy_uf2(
        "/tmp/sc_flash_does_not_exist_xyz.uf2", dst_dir,
        NULL, NULL, err, sizeof(err));
    TEST_ASSERT(st == SC_FLASH_ERR_FILE_OPEN, "missing source rejected");
    TEST_ASSERT(strstr(err, "could not open source") != NULL,
                "diagnostic mentions source");
    (void)rmdir(dst_dir);
    return 0;
}

static int test_copy_rejects_unwritable_destination(void) {
    char src_dir[256];
    TEST_ASSERT(make_temp_dir(src_dir, sizeof(src_dir), "src") == 0,
                "mkdtemp src");
    char src_path[512];
    TEST_ASSERT(make_src_file(src_dir, 1024u,
                              src_path, sizeof(src_path)) == 0, "src");

    char err[256] = {0};
    const sc_flash_status_t st = sc_flash_copy_uf2(
        src_path, "/tmp/sc_flash_dst_does_not_exist_qq",
        NULL, NULL, err, sizeof(err));
    TEST_ASSERT(st == SC_FLASH_ERR_FILE_WRITE,
                "missing destination rejected");

    (void)unlink(src_path);
    (void)rmdir(src_dir);
    return 0;
}

static int test_copy_rejects_empty_source(void) {
    char src_dir[256];
    char dst_dir[256];
    TEST_ASSERT(make_temp_dir(src_dir, sizeof(src_dir), "src") == 0,
                "mkdtemp src");
    TEST_ASSERT(make_temp_dir(dst_dir, sizeof(dst_dir), "dst") == 0,
                "mkdtemp dst");

    char src_path[512];
    (void)snprintf(src_path, sizeof(src_path), "%s/empty.uf2", src_dir);
    FILE *f = fopen(src_path, "wb");
    TEST_ASSERT(f != NULL, "create empty src");
    (void)fclose(f);

    char err[256] = {0};
    const sc_flash_status_t st = sc_flash_copy_uf2(
        src_path, dst_dir, NULL, NULL, err, sizeof(err));
    TEST_ASSERT(st == SC_FLASH_ERR_EMPTY, "empty source rejected");

    (void)unlink(src_path);
    (void)rmdir(src_dir);
    (void)rmdir(dst_dir);
    return 0;
}

static int test_copy_rejects_null_paths(void) {
    char err[64] = {0};
    TEST_ASSERT(sc_flash_copy_uf2(NULL, "/tmp", NULL, NULL,
                                  err, sizeof(err)) ==
                SC_FLASH_ERR_NULL_ARG,
                "NULL src rejected");
    TEST_ASSERT(sc_flash_copy_uf2("/tmp/x", NULL, NULL, NULL,
                                  err, sizeof(err)) ==
                SC_FLASH_ERR_NULL_ARG,
                "NULL drive rejected");
    return 0;
}

/* ── sc_flash_wait_reenumeration tests ───────────────────────────── */

typedef struct {
    char path[512];
    uint32_t delay_ms;
} create_entry_args_t;

static void *create_entry_worker(void *arg) {
    create_entry_args_t *ca = (create_entry_args_t *)arg;
    struct timespec req;
    req.tv_sec = (time_t)(ca->delay_ms / 1000u);
    req.tv_nsec = (long)((ca->delay_ms % 1000u) * 1000000L);
    (void)nanosleep(&req, NULL);
    /* by-id entries are normally symlinks but the watcher only checks
     * the directory listing, so a regular file works as a stand-in. */
    FILE *f = fopen(ca->path, "wb");
    if (f != NULL) {
        (void)fclose(f);
    }
    return NULL;
}

static int test_reenum_returns_path_when_uid_appears(void) {
    char parent[256];
    TEST_ASSERT(make_temp_dir(parent, sizeof(parent), "by_id") == 0,
                "mkdtemp parent");

    const char *uid = "E661A4D1234567AB";
    create_entry_args_t ca;
    (void)snprintf(ca.path, sizeof(ca.path),
                   "%s/usb-Jaszczur_Fiesta_ECU_%s-if00",
                   parent, uid);
    ca.delay_ms = 200u;

    pthread_t worker;
    TEST_ASSERT(pthread_create(&worker, NULL, create_entry_worker, &ca) == 0,
                "pthread_create");

    char out[512] = {0};
    const sc_flash_status_t st = sc_flash__wait_reenumeration_in(
        parent, uid, 3000u, out, sizeof(out), NULL, 0u);
    (void)pthread_join(worker, NULL);

    TEST_ASSERT(st == SC_FLASH_OK, "reenum returned OK");
    TEST_ASSERT(strstr(out, uid) != NULL, "out_path contains UID");

    (void)unlink(ca.path);
    (void)rmdir(parent);
    return 0;
}

static int test_reenum_ignores_unrelated_entries(void) {
    char parent[256];
    TEST_ASSERT(make_temp_dir(parent, sizeof(parent), "by_id") == 0,
                "mkdtemp parent");

    /* Pre-existing unrelated entry (different UID). */
    char other[512];
    (void)snprintf(other, sizeof(other),
                   "%s/usb-Foo_Bar_OTHER_DEVICE_DEADBEEF-if00", parent);
    FILE *f = fopen(other, "wb");
    TEST_ASSERT(f != NULL, "create unrelated entry");
    (void)fclose(f);

    const char *uid = "ABCDEF0123456789";
    char out[512] = {0};
    char err[256] = {0};
    const sc_flash_status_t st = sc_flash__wait_reenumeration_in(
        parent, uid, 250u, out, sizeof(out), err, sizeof(err));

    TEST_ASSERT(st == SC_FLASH_ERR_REENUM_TIMEOUT, "unrelated entries ignored");
    TEST_ASSERT(strstr(err, "no by-id entry matching") != NULL,
                "diagnostic mentions UID");

    (void)unlink(other);
    (void)rmdir(parent);
    return 0;
}

static int test_reenum_times_out_on_empty_parent(void) {
    char parent[256];
    TEST_ASSERT(make_temp_dir(parent, sizeof(parent), "by_id") == 0,
                "mkdtemp parent");

    char out[512] = {0};
    const sc_flash_status_t st = sc_flash__wait_reenumeration_in(
        parent, "0011223344556677", 200u, out, sizeof(out), NULL, 0u);
    TEST_ASSERT(st == SC_FLASH_ERR_REENUM_TIMEOUT, "empty parent times out");
    TEST_ASSERT(out[0] == '\0', "out_path empty on timeout");
    (void)rmdir(parent);
    return 0;
}

static int test_reenum_picks_uid_among_many_entries(void) {
    char parent[256];
    TEST_ASSERT(make_temp_dir(parent, sizeof(parent), "by_id") == 0,
                "mkdtemp parent");

    /* Several unrelated entries. */
    char extras[3][512];
    const char *uids[3] = {
        "DEAD1111DEAD2222",
        "FACEB0CAFACEB0CA",
        "C0DEFEEDC0DEFEED",
    };
    for (size_t i = 0u; i < 3u; ++i) {
        (void)snprintf(extras[i], sizeof(extras[i]),
                       "%s/usb-Other_Device_OTHER_%s-if00",
                       parent, uids[i]);
        FILE *f = fopen(extras[i], "wb");
        TEST_ASSERT(f != NULL, "create extra");
        (void)fclose(f);
    }

    /* Target entry. */
    char target[512];
    (void)snprintf(target, sizeof(target),
                   "%s/usb-Jaszczur_Fiesta_Clocks_E661A4D1234567AB-if00",
                   parent);
    FILE *t = fopen(target, "wb");
    TEST_ASSERT(t != NULL, "create target");
    (void)fclose(t);

    char out[512] = {0};
    const sc_flash_status_t st = sc_flash__wait_reenumeration_in(
        parent, "E661A4D1234567AB", 500u, out, sizeof(out), NULL, 0u);

    TEST_ASSERT(st == SC_FLASH_OK, "reenum found target among many");
    TEST_ASSERT(strstr(out, "E661A4D1234567AB") != NULL,
                "out_path contains target UID");

    for (size_t i = 0u; i < 3u; ++i) {
        (void)unlink(extras[i]);
    }
    (void)unlink(target);
    (void)rmdir(parent);
    return 0;
}

static int test_reenum_rejects_null_inputs(void) {
    char err[64] = {0};
    TEST_ASSERT(sc_flash__wait_reenumeration_in(
                    NULL, "FOO", 100u, NULL, 0u, err, sizeof(err)) ==
                SC_FLASH_ERR_NULL_ARG,
                "NULL parent rejected");
    TEST_ASSERT(sc_flash__wait_reenumeration_in(
                    "/tmp", NULL, 100u, NULL, 0u, err, sizeof(err)) ==
                SC_FLASH_ERR_NULL_ARG,
                "NULL uid rejected");
    TEST_ASSERT(sc_flash__wait_reenumeration_in(
                    "/tmp", "", 100u, NULL, 0u, err, sizeof(err)) ==
                SC_FLASH_ERR_NULL_ARG,
                "empty uid rejected");
    return 0;
}

static int test_status_strings_for_new_codes(void) {
    TEST_ASSERT(strcmp(sc_flash_status_str(SC_FLASH_ERR_FILE_WRITE),
                       "FILE_WRITE") == 0,
                "FILE_WRITE status string");
    TEST_ASSERT(strcmp(sc_flash_status_str(SC_FLASH_ERR_REENUM_TIMEOUT),
                       "REENUM_TIMEOUT") == 0,
                "REENUM_TIMEOUT status string");
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_copy_writes_destination_byte_for_byte();
    failures += test_copy_progress_called_with_null_cb();
    failures += test_copy_rejects_missing_source();
    failures += test_copy_rejects_unwritable_destination();
    failures += test_copy_rejects_empty_source();
    failures += test_copy_rejects_null_paths();
    failures += test_reenum_returns_path_when_uid_appears();
    failures += test_reenum_ignores_unrelated_entries();
    failures += test_reenum_times_out_on_empty_parent();
    failures += test_reenum_picks_uid_among_many_entries();
    failures += test_reenum_rejects_null_inputs();
    failures += test_status_strings_for_new_codes();
    if (failures == 0) {
        printf("[OK] sc_flash copy + reenum: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "[FAIL] sc_flash copy + reenum: %d test(s) failed\n",
            failures);
    return 1;
}

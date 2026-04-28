/*
 * Phase 6.3 - coverage for sc_flash_watch_for_bootsel.
 *
 * Tests use the test-only entry point sc_flash__watch_for_bootsel_in
 * with mkdtemp-allocated parent directories so the suite stays
 * hermetic (never touches /media/$USER or /run/media/$USER on the
 * dev box).
 *
 * Worker-thread fixture: a pthread sleeps a known interval, then
 * mkdir()s the BOOTSEL-shaped child under the temp parent. The
 * watcher running on the main thread should pick the new directory up
 * within budget and return its full path.
 */

#include "sc_flash.h"

#include <errno.h>
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

static int make_temp_parent(char *out, size_t out_size) {
    if (out_size < 32u) return -1;
    (void)snprintf(out, out_size, "/tmp/sc_flash_bootsel_XXXXXX");
    return (mkdtemp(out) != NULL) ? 0 : -1;
}

typedef struct {
    char path[512];        /* full path of the directory to create */
    uint32_t delay_ms;     /* sleep before mkdir */
} create_child_args_t;

static void *create_child_worker(void *arg) {
    create_child_args_t *ca = (create_child_args_t *)arg;
    struct timespec req;
    req.tv_sec = (time_t)(ca->delay_ms / 1000u);
    req.tv_nsec = (long)((ca->delay_ms % 1000u) * 1000000L);
    (void)nanosleep(&req, NULL);
    (void)mkdir(ca->path, 0755);
    return NULL;
}

/* ── Tests ───────────────────────────────────────────────────────── */

static int test_returns_path_when_bootsel_appears(void) {
    char parent[256];
    TEST_ASSERT(make_temp_parent(parent, sizeof(parent)) == 0,
                "mkdtemp parent");

    create_child_args_t ca;
    (void)snprintf(ca.path, sizeof(ca.path), "%s/RPI-RP2", parent);
    ca.delay_ms = 200u;

    pthread_t worker;
    TEST_ASSERT(pthread_create(&worker, NULL, create_child_worker, &ca) == 0,
                "pthread_create");

    const char *parents[1] = { parent };
    char out[512] = {0};
    char err[256] = {0};
    const sc_flash_status_t st = sc_flash__watch_for_bootsel_in(
        parents, 1u, 3000u, out, sizeof(out), err, sizeof(err));

    (void)pthread_join(worker, NULL);

    TEST_ASSERT(st == SC_FLASH_OK, "watcher returned OK");
    char expected[512];
    (void)snprintf(expected, sizeof(expected), "%s/RPI-RP2", parent);
    TEST_ASSERT(strcmp(out, expected) == 0, "out_path matches expected");

    (void)rmdir(ca.path);
    (void)rmdir(parent);
    return 0;
}

static int test_matches_rp2350_alternative_name(void) {
    char parent[256];
    TEST_ASSERT(make_temp_parent(parent, sizeof(parent)) == 0,
                "mkdtemp parent");

    create_child_args_t ca;
    (void)snprintf(ca.path, sizeof(ca.path), "%s/RP2350", parent);
    ca.delay_ms = 150u;

    pthread_t worker;
    TEST_ASSERT(pthread_create(&worker, NULL, create_child_worker, &ca) == 0,
                "pthread_create");

    const char *parents[1] = { parent };
    char out[512] = {0};
    const sc_flash_status_t st = sc_flash__watch_for_bootsel_in(
        parents, 1u, 2000u, out, sizeof(out), NULL, 0u);

    (void)pthread_join(worker, NULL);

    TEST_ASSERT(st == SC_FLASH_OK, "RP2350 name matches");
    TEST_ASSERT(strstr(out, "/RP2350") != NULL, "out_path ends with /RP2350");

    (void)rmdir(ca.path);
    (void)rmdir(parent);
    return 0;
}

static int test_matches_rpi_rp2350_alternative_name(void) {
    char parent[256];
    TEST_ASSERT(make_temp_parent(parent, sizeof(parent)) == 0,
                "mkdtemp parent");

    char child[512];
    (void)snprintf(child, sizeof(child), "%s/RPI-RP2350", parent);
    TEST_ASSERT(mkdir(child, 0755) == 0, "mkdir RPI-RP2350");

    const char *parents[1] = { parent };
    char out[512] = {0};
    const sc_flash_status_t st = sc_flash__watch_for_bootsel_in(
        parents, 1u, 500u, out, sizeof(out), NULL, 0u);

    TEST_ASSERT(st == SC_FLASH_OK, "RPI-RP2350 prefix matches");
    TEST_ASSERT(strcmp(out, child) == 0, "out_path matches RPI-RP2350");

    (void)rmdir(child);
    (void)rmdir(parent);
    return 0;
}

static int test_times_out_when_no_bootsel_appears(void) {
    char parent[256];
    TEST_ASSERT(make_temp_parent(parent, sizeof(parent)) == 0,
                "mkdtemp parent");

    const char *parents[1] = { parent };
    char out[512] = {0};
    char err[256] = {0};

    struct timespec t_start;
    (void)clock_gettime(CLOCK_MONOTONIC, &t_start);
    const sc_flash_status_t st = sc_flash__watch_for_bootsel_in(
        parents, 1u, 250u, out, sizeof(out), err, sizeof(err));
    struct timespec t_end;
    (void)clock_gettime(CLOCK_MONOTONIC, &t_end);

    TEST_ASSERT(st == SC_FLASH_ERR_BOOTSEL_TIMEOUT, "timeout status");
    TEST_ASSERT(out[0] == '\0', "out_path is empty on timeout");
    TEST_ASSERT(strstr(err, "no BOOTSEL drive") != NULL,
                "diagnostic mentions BOOTSEL");

    /* Sanity: actually waited at least most of the budget. */
    const long elapsed_ms =
        (t_end.tv_sec - t_start.tv_sec) * 1000L +
        (t_end.tv_nsec - t_start.tv_nsec) / 1000000L;
    TEST_ASSERT(elapsed_ms >= 200, "watcher honoured the timeout budget");

    (void)rmdir(parent);
    return 0;
}

static int test_unrelated_directory_does_not_match(void) {
    char parent[256];
    TEST_ASSERT(make_temp_parent(parent, sizeof(parent)) == 0,
                "mkdtemp parent");

    char unrelated[512];
    (void)snprintf(unrelated, sizeof(unrelated), "%s/MyUSBStick", parent);
    TEST_ASSERT(mkdir(unrelated, 0755) == 0, "mkdir unrelated child");

    const char *parents[1] = { parent };
    char out[512] = {0};
    const sc_flash_status_t st = sc_flash__watch_for_bootsel_in(
        parents, 1u, 250u, out, sizeof(out), NULL, 0u);

    TEST_ASSERT(st == SC_FLASH_ERR_BOOTSEL_TIMEOUT,
                "non-BOOTSEL name is ignored");

    (void)rmdir(unrelated);
    (void)rmdir(parent);
    return 0;
}

static int test_two_parents_one_missing_one_present(void) {
    /* /media/$USER/-style scenario where one parent doesn't exist
     * (no media currently mounted) and the other one has the BOOTSEL
     * drive. Watcher must keep polling both without erroring out. */
    char parent_present[256];
    TEST_ASSERT(make_temp_parent(parent_present, sizeof(parent_present)) == 0,
                "mkdtemp present parent");

    char child[512];
    (void)snprintf(child, sizeof(child), "%s/RPI-RP2", parent_present);
    TEST_ASSERT(mkdir(child, 0755) == 0, "mkdir RPI-RP2");

    const char *parents[2] = {
        "/tmp/sc_flash_bootsel_DOES_NOT_EXIST_xyz",
        parent_present,
    };
    char out[512] = {0};
    const sc_flash_status_t st = sc_flash__watch_for_bootsel_in(
        parents, 2u, 500u, out, sizeof(out), NULL, 0u);

    TEST_ASSERT(st == SC_FLASH_OK, "watcher succeeds despite missing parent");
    TEST_ASSERT(strcmp(out, child) == 0, "found in the present parent");

    (void)rmdir(child);
    (void)rmdir(parent_present);
    return 0;
}

static int test_null_inputs_are_rejected(void) {
    TEST_ASSERT(sc_flash__watch_for_bootsel_in(NULL, 0u, 100u,
                                                NULL, 0u, NULL, 0u) ==
                SC_FLASH_ERR_NULL_ARG,
                "NULL parent_dirs rejected");

    const char *parents[1] = { "/tmp/sc_flash_bootsel_DOES_NOT_EXIST_q" };
    TEST_ASSERT(sc_flash__watch_for_bootsel_in(parents, 0u, 100u,
                                                NULL, 0u, NULL, 0u) ==
                SC_FLASH_ERR_NULL_ARG,
                "zero parent_count rejected");
    return 0;
}

static int test_status_string_for_new_codes(void) {
    TEST_ASSERT(strcmp(sc_flash_status_str(SC_FLASH_ERR_BOOTSEL_TIMEOUT),
                       "BOOTSEL_TIMEOUT") == 0,
                "BOOTSEL_TIMEOUT status string");
    TEST_ASSERT(strcmp(sc_flash_status_str(SC_FLASH_ERR_NOT_IMPLEMENTED),
                       "NOT_IMPLEMENTED") == 0,
                "NOT_IMPLEMENTED status string still stable");
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_returns_path_when_bootsel_appears();
    failures += test_matches_rp2350_alternative_name();
    failures += test_matches_rpi_rp2350_alternative_name();
    failures += test_times_out_when_no_bootsel_appears();
    failures += test_unrelated_directory_does_not_match();
    failures += test_two_parents_one_missing_one_present();
    failures += test_null_inputs_are_rejected();
    failures += test_status_string_for_new_codes();
    if (failures == 0) {
        printf("[OK] sc_flash bootsel: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "[FAIL] sc_flash bootsel: %d test(s) failed\n", failures);
    return 1;
}

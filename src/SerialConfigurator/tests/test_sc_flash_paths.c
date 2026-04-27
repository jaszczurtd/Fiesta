/*
 * sc_flash_paths persistence tests.
 *
 * Drives load/save round-trips through a process-local override
 * so the tests do not touch the developer's real config dir.
 */

#include "../src/ui/sc_flash_paths.c"

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

#define TEST_ASSERT_STR_EQ(actual, expected, msg) \
    do { \
        if (strcmp((actual), (expected)) != 0) { \
            fprintf(stderr, \
                    "FAIL: %s — %s (line %d): got '%s', want '%s'\n", \
                    __func__, (msg), __LINE__, (actual), (expected)); \
            return 1; \
        } \
    } while (0)

static char g_test_path[128];

static void with_temp_path(void)
{
    snprintf(g_test_path, sizeof(g_test_path),
             "/tmp/sc_flash_paths_test_%d.json", (int)getpid());
    (void)remove(g_test_path);
    sc_flash_paths_set_test_override(g_test_path);
}

static void cleanup(void)
{
    (void)remove(g_test_path);
    sc_flash_paths_set_test_override(NULL);
}

static int test_load_missing_file_is_clean_empty(void)
{
    with_temp_path();
    ScFlashPaths p;
    memset(&p, 0xAA, sizeof(p));   /* poison */
    const bool ok = sc_flash_paths_load(&p);
    cleanup();
    TEST_ASSERT(ok, "load returns true for missing file");
    TEST_ASSERT_STR_EQ(p.entries[0].uf2, "", "ECU uf2 zeroed");
    TEST_ASSERT_STR_EQ(p.entries[0].manifest, "", "ECU manifest zeroed");
    TEST_ASSERT_STR_EQ(p.entries[1].uf2, "", "Clocks uf2 zeroed");
    TEST_ASSERT_STR_EQ(p.entries[2].manifest, "", "OilAndSpeed manifest zeroed");
    return 0;
}

static int test_save_then_load_round_trip(void)
{
    with_temp_path();
    ScFlashPaths in;
    sc_flash_paths_init(&in);
    sc_flash_paths_set_uf2(&in,      "ECU",         "/path/ecu.uf2");
    sc_flash_paths_set_manifest(&in, "ECU",         "/path/ecu.json");
    sc_flash_paths_set_uf2(&in,      "Clocks",      "/path/clocks.uf2");
    /* OilAndSpeed: leave both empty intentionally. */

    TEST_ASSERT(sc_flash_paths_save(&in), "save");

    ScFlashPaths out;
    memset(&out, 0xAA, sizeof(out));
    TEST_ASSERT(sc_flash_paths_load(&out), "load");
    cleanup();

    TEST_ASSERT_STR_EQ(sc_flash_paths_get_uf2(&out, "ECU"),
                       "/path/ecu.uf2", "round-trip ECU uf2");
    TEST_ASSERT_STR_EQ(sc_flash_paths_get_manifest(&out, "ECU"),
                       "/path/ecu.json", "round-trip ECU manifest");
    TEST_ASSERT_STR_EQ(sc_flash_paths_get_uf2(&out, "Clocks"),
                       "/path/clocks.uf2", "round-trip Clocks uf2");
    TEST_ASSERT_STR_EQ(sc_flash_paths_get_manifest(&out, "Clocks"),
                       "", "Clocks manifest stays empty");
    TEST_ASSERT_STR_EQ(sc_flash_paths_get_uf2(&out, "OilAndSpeed"),
                       "", "OilAndSpeed uf2 stays empty");
    return 0;
}

static int test_load_malformed_file_returns_false_and_clears(void)
{
    with_temp_path();
    FILE *f = fopen(g_test_path, "wb");
    TEST_ASSERT(f != NULL, "open temp");
    const char *garbage = "{ this is not valid JSON };";
    (void)fwrite(garbage, 1u, strlen(garbage), f);
    (void)fclose(f);

    ScFlashPaths p;
    sc_flash_paths_set_uf2(&p, "ECU", "/should/be/cleared");
    const bool ok = sc_flash_paths_load(&p);
    cleanup();

    TEST_ASSERT(!ok, "load returns false for malformed JSON");
    TEST_ASSERT_STR_EQ(p.entries[0].uf2, "", "load zeros struct on parse failure");
    return 0;
}

static int test_unknown_top_key_is_ignored(void)
{
    with_temp_path();
    FILE *f = fopen(g_test_path, "wb");
    TEST_ASSERT(f != NULL, "open temp");
    const char *legacy =
        "{"
        " \"Adjustometer\": { \"uf2_path\": \"/old.uf2\", \"manifest_path\": \"\" },"
        " \"ECU\":          { \"uf2_path\": \"/new.uf2\", \"manifest_path\": \"\" }"
        "}";
    (void)fwrite(legacy, 1u, strlen(legacy), f);
    (void)fclose(f);

    ScFlashPaths p;
    TEST_ASSERT(sc_flash_paths_load(&p), "load with legacy Adjustometer key");
    cleanup();

    TEST_ASSERT_STR_EQ(sc_flash_paths_get_uf2(&p, "ECU"),
                       "/new.uf2", "ECU loaded");
    /* No way to fetch a value for a non-tracked module — sc_flash_paths
     * has no slot for Adjustometer, and the loader silently ignores it. */
    return 0;
}

static int test_unknown_inner_key_is_ignored(void)
{
    with_temp_path();
    FILE *f = fopen(g_test_path, "wb");
    TEST_ASSERT(f != NULL, "open temp");
    const char *forward_compat =
        "{"
        " \"ECU\": { \"uf2_path\": \"/x.uf2\","
        "          \"manifest_path\": \"/x.json\","
        "          \"future_field\": \"forward-compat-noise\" }"
        "}";
    (void)fwrite(forward_compat, 1u, strlen(forward_compat), f);
    (void)fclose(f);

    ScFlashPaths p;
    TEST_ASSERT(sc_flash_paths_load(&p), "load with extra inner key");
    cleanup();

    TEST_ASSERT_STR_EQ(sc_flash_paths_get_uf2(&p, "ECU"), "/x.uf2", "ECU uf2");
    TEST_ASSERT_STR_EQ(sc_flash_paths_get_manifest(&p, "ECU"), "/x.json", "ECU manifest");
    return 0;
}

static int test_setters_for_unknown_module_are_noop(void)
{
    ScFlashPaths p;
    sc_flash_paths_init(&p);
    sc_flash_paths_set_uf2(&p, "Adjustometer", "/should/be/dropped");
    sc_flash_paths_set_manifest(&p, "BogusModule", "/x");
    TEST_ASSERT_STR_EQ(sc_flash_paths_get_uf2(&p, "Adjustometer"), "",
                       "Adjustometer is not a tracked slot");
    TEST_ASSERT_STR_EQ(sc_flash_paths_get_manifest(&p, "BogusModule"), "",
                       "Unknown module getter returns empty");
    /* Nothing leaked into legitimate slots. */
    TEST_ASSERT_STR_EQ(p.entries[0].uf2, "", "ECU uf2 untouched");
    return 0;
}

static int test_paths_with_special_characters_round_trip(void)
{
    with_temp_path();
    ScFlashPaths in;
    sc_flash_paths_init(&in);
    /* Backslash and double-quote are the only chars that need
     * escaping in JSON; verify both round-trip cleanly. */
    sc_flash_paths_set_uf2(&in, "ECU", "/path/with \"quote\" and \\backslash");
    TEST_ASSERT(sc_flash_paths_save(&in), "save");

    ScFlashPaths out;
    TEST_ASSERT(sc_flash_paths_load(&out), "load");
    cleanup();

    TEST_ASSERT_STR_EQ(sc_flash_paths_get_uf2(&out, "ECU"),
                       "/path/with \"quote\" and \\backslash",
                       "escaped chars round-trip");
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += test_load_missing_file_is_clean_empty();
    failures += test_save_then_load_round_trip();
    failures += test_load_malformed_file_returns_false_and_clears();
    failures += test_unknown_top_key_is_ignored();
    failures += test_unknown_inner_key_is_ignored();
    failures += test_setters_for_unknown_module_are_noop();
    failures += test_paths_with_special_characters_round_trip();

    if (failures != 0) {
        fprintf(stderr, "test_sc_flash_paths: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    fprintf(stdout, "test_sc_flash_paths: all 7 tests passed\n");
    return EXIT_SUCCESS;
}

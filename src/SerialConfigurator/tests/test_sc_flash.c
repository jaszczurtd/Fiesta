/*
 * sc_flash UF2 format-checker tests.
 *
 * The tests build small valid / mutated UF2 files in /tmp via mkstemp
 * and then call sc_flash_uf2_format_check on them. The generator is
 * focused on what the format check actually verifies (magic words,
 * family id, block alignment, indices) — payload bytes inside each
 * block are zeros.
 */

#include "sc_flash.h"

#include <stdint.h>
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

static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void build_block(uint8_t block[SC_FLASH_UF2_BLOCK_SIZE],
                        uint32_t index,
                        uint32_t total,
                        uint32_t family)
{
    memset(block, 0, SC_FLASH_UF2_BLOCK_SIZE);
    put_u32_le(block + 0,  SC_FLASH_UF2_FIRST_MAGIC);
    put_u32_le(block + 4,  SC_FLASH_UF2_SECOND_MAGIC);
    put_u32_le(block + 8,  0u);              /* flags */
    put_u32_le(block + 12, 0x10000000u);     /* target addr */
    put_u32_le(block + 16, 256u);            /* payload size */
    put_u32_le(block + 20, index);
    put_u32_le(block + 24, total);
    put_u32_le(block + 28, family);
    put_u32_le(block + 508, SC_FLASH_UF2_END_MAGIC);
}

static bool write_uf2(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    const size_t n = fwrite(data, 1u, len, f);
    (void)fclose(f);
    return n == len;
}

static bool make_valid_uf2(char *out_path, size_t out_path_size,
                           uint32_t total_blocks)
{
    if (out_path_size < 32u) return false;
    snprintf(out_path, out_path_size, "/tmp/sc_flash_test_XXXXXX");
    int fd = mkstemp(out_path);
    if (fd < 0) return false;
    (void)close(fd);

    uint8_t *blob = (uint8_t *)malloc(total_blocks * SC_FLASH_UF2_BLOCK_SIZE);
    if (blob == NULL) {
        (void)remove(out_path);
        return false;
    }
    for (uint32_t i = 0u; i < total_blocks; ++i) {
        build_block(blob + i * SC_FLASH_UF2_BLOCK_SIZE,
                    i, total_blocks, SC_FLASH_UF2_FAMILY_RP2040);
    }
    const bool ok = write_uf2(out_path, blob, total_blocks * SC_FLASH_UF2_BLOCK_SIZE);
    free(blob);
    if (!ok) {
        (void)remove(out_path);
    }
    return ok;
}

/* ── tests ──────────────────────────────────────────────────────────────── */

static int test_valid_uf2_passes(void)
{
    char path[64];
    TEST_ASSERT(make_valid_uf2(path, sizeof(path), 4u), "make valid");

    char err[128];
    const sc_flash_status_t st = sc_flash_uf2_format_check(path, err, sizeof(err));
    (void)remove(path);
    TEST_ASSERT_EQ(SC_FLASH_OK, st, "valid UF2 -> OK");
    TEST_ASSERT(strstr(err, "4 blocks") != NULL, "OK message reports block count");
    return 0;
}

static int test_missing_file_returns_file_open(void)
{
    char err[128];
    TEST_ASSERT_EQ(SC_FLASH_ERR_FILE_OPEN,
                   sc_flash_uf2_format_check("/nonexistent/foo.uf2", err, sizeof(err)),
                   "missing file");
    return 0;
}

static int test_empty_file_is_rejected(void)
{
    char path[] = "/tmp/sc_flash_empty_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0, "mkstemp");
    (void)close(fd);
    /* mkstemp creates an empty file. Hand it straight to the checker. */
    char err[128];
    const sc_flash_status_t st = sc_flash_uf2_format_check(path, err, sizeof(err));
    (void)remove(path);
    TEST_ASSERT_EQ(SC_FLASH_ERR_EMPTY, st, "empty -> EMPTY");
    return 0;
}

static int test_unaligned_size_is_rejected(void)
{
    char path[] = "/tmp/sc_flash_unalign_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0, "mkstemp");
    (void)close(fd);
    /* Write 600 bytes — not a multiple of 512. */
    uint8_t blob[600];
    memset(blob, 0xAA, sizeof(blob));
    TEST_ASSERT(write_uf2(path, blob, sizeof(blob)), "write unaligned");

    char err[128];
    const sc_flash_status_t st = sc_flash_uf2_format_check(path, err, sizeof(err));
    (void)remove(path);
    TEST_ASSERT_EQ(SC_FLASH_ERR_NOT_BLOCK_ALIGNED, st, "unaligned -> NOT_BLOCK_ALIGNED");
    return 0;
}

static int test_wrong_first_magic_is_rejected(void)
{
    char path[64];
    TEST_ASSERT(make_valid_uf2(path, sizeof(path), 2u), "make valid");
    /* Corrupt the first magic word in block 0. */
    FILE *f = fopen(path, "rb+");
    TEST_ASSERT(f != NULL, "open r+");
    uint8_t bad_magic[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    TEST_ASSERT(fseek(f, 0, SEEK_SET) == 0, "seek 0");
    TEST_ASSERT(fwrite(bad_magic, 1u, 4u, f) == 4u, "write bad magic");
    (void)fclose(f);

    char err[128];
    const sc_flash_status_t st = sc_flash_uf2_format_check(path, err, sizeof(err));
    (void)remove(path);
    TEST_ASSERT_EQ(SC_FLASH_ERR_BAD_FIRST_MAGIC, st, "bad magic -> BAD_FIRST_MAGIC");
    TEST_ASSERT(strstr(err, "block 0") != NULL, "diagnostic mentions block 0");
    return 0;
}

static int test_wrong_family_is_rejected(void)
{
    char path[] = "/tmp/sc_flash_family_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0, "mkstemp");
    (void)close(fd);

    /* Build a single block with a non-RP2040 family id (STM32 placeholder). */
    uint8_t block[SC_FLASH_UF2_BLOCK_SIZE];
    build_block(block, 0u, 1u, 0x57755A57u);
    TEST_ASSERT(write_uf2(path, block, sizeof(block)), "write");

    char err[128];
    const sc_flash_status_t st = sc_flash_uf2_format_check(path, err, sizeof(err));
    (void)remove(path);
    TEST_ASSERT_EQ(SC_FLASH_ERR_WRONG_FAMILY, st, "wrong family -> WRONG_FAMILY");
    return 0;
}

static int test_block_index_out_of_range_is_rejected(void)
{
    char path[64];
    TEST_ASSERT(make_valid_uf2(path, sizeof(path), 2u), "make valid 2-block UF2");
    /* Mutate block 1 to claim total=1 (so its index 1 is out of range). */
    FILE *f = fopen(path, "rb+");
    TEST_ASSERT(f != NULL, "open r+");
    /* total field is at offset 24 of each block; block 1 starts at 512. */
    uint8_t bad_total[4] = { 0x01, 0x00, 0x00, 0x00 };
    TEST_ASSERT(fseek(f, 512 + 24, SEEK_SET) == 0, "seek block-1 total");
    TEST_ASSERT(fwrite(bad_total, 1u, 4u, f) == 4u, "write bad total");
    (void)fclose(f);

    char err[128];
    const sc_flash_status_t st = sc_flash_uf2_format_check(path, err, sizeof(err));
    (void)remove(path);
    /* The checker first notices that block 1's declared total (1)
     * disagrees with block 0's declared total (2) — that's the
     * "BLOCK_INDEX_OUT_OF_RANGE" status (we share the code for both
     * disagreement and out-of-range). */
    TEST_ASSERT_EQ(SC_FLASH_ERR_BLOCK_INDEX_OUT_OF_RANGE, st,
                   "out-of-range index/total -> BLOCK_INDEX_OUT_OF_RANGE");
    return 0;
}

static int test_status_strings_are_stable(void)
{
    TEST_ASSERT(strcmp(sc_flash_status_str(SC_FLASH_OK), "OK") == 0, "OK token");
    TEST_ASSERT(strcmp(sc_flash_status_str(SC_FLASH_ERR_BAD_FIRST_MAGIC),
                       "BAD_FIRST_MAGIC") == 0,
                "BAD_FIRST_MAGIC token");
    TEST_ASSERT(strcmp(sc_flash_status_str(SC_FLASH_ERR_WRONG_FAMILY),
                       "WRONG_FAMILY") == 0,
                "WRONG_FAMILY token");
    TEST_ASSERT(strcmp(sc_flash_status_str(SC_FLASH_ERR_NOT_IMPLEMENTED),
                       "NOT_IMPLEMENTED") == 0,
                "NOT_IMPLEMENTED token");
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += test_valid_uf2_passes();
    failures += test_missing_file_returns_file_open();
    failures += test_empty_file_is_rejected();
    failures += test_unaligned_size_is_rejected();
    failures += test_wrong_first_magic_is_rejected();
    failures += test_wrong_family_is_rejected();
    failures += test_block_index_out_of_range_is_rejected();
    failures += test_status_strings_are_stable();

    if (failures != 0) {
        fprintf(stderr, "test_sc_flash: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    fprintf(stdout, "test_sc_flash: all 8 tests passed\n");
    return EXIT_SUCCESS;
}

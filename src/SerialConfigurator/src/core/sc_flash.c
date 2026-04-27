#include "sc_flash.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *sc_flash_status_str(sc_flash_status_t st)
{
    switch (st) {
    case SC_FLASH_OK:                          return "OK";
    case SC_FLASH_ERR_NULL_ARG:                return "NULL_ARG";
    case SC_FLASH_ERR_FILE_OPEN:               return "FILE_OPEN";
    case SC_FLASH_ERR_FILE_READ:               return "FILE_READ";
    case SC_FLASH_ERR_EMPTY:                   return "EMPTY";
    case SC_FLASH_ERR_TOO_LARGE:               return "TOO_LARGE";
    case SC_FLASH_ERR_NOT_BLOCK_ALIGNED:       return "NOT_BLOCK_ALIGNED";
    case SC_FLASH_ERR_BAD_FIRST_MAGIC:         return "BAD_FIRST_MAGIC";
    case SC_FLASH_ERR_BAD_SECOND_MAGIC:        return "BAD_SECOND_MAGIC";
    case SC_FLASH_ERR_BAD_END_MAGIC:           return "BAD_END_MAGIC";
    case SC_FLASH_ERR_WRONG_FAMILY:            return "WRONG_FAMILY";
    case SC_FLASH_ERR_BLOCK_INDEX_OUT_OF_RANGE: return "BLOCK_INDEX_OUT_OF_RANGE";
    case SC_FLASH_ERR_NOT_IMPLEMENTED:         return "NOT_IMPLEMENTED";
    }
    return "UNKNOWN";
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void write_error(char *buf, size_t size, const char *fmt, ...)
{
    if (buf == NULL || size == 0u) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buf, size, fmt, args);
    va_end(args);
}

sc_flash_status_t sc_flash_uf2_format_check(const char *path,
                                            char *error_buf,
                                            size_t error_size)
{
    if (path == NULL) {
        write_error(error_buf, error_size, "null path");
        return SC_FLASH_ERR_NULL_ARG;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        write_error(error_buf, error_size, "could not open '%s'", path);
        return SC_FLASH_ERR_FILE_OPEN;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        (void)fclose(f);
        write_error(error_buf, error_size, "fseek end failed");
        return SC_FLASH_ERR_FILE_READ;
    }
    const long len_signed = ftell(f);
    if (len_signed < 0) {
        (void)fclose(f);
        write_error(error_buf, error_size, "ftell failed");
        return SC_FLASH_ERR_FILE_READ;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        (void)fclose(f);
        write_error(error_buf, error_size, "fseek start failed");
        return SC_FLASH_ERR_FILE_READ;
    }
    const size_t len = (size_t)len_signed;

    if (len == 0u) {
        (void)fclose(f);
        write_error(error_buf, error_size, "file is empty");
        return SC_FLASH_ERR_EMPTY;
    }
    if (len > SC_FLASH_UF2_MAX_BYTES) {
        (void)fclose(f);
        write_error(error_buf, error_size,
                    "file is %zu bytes, cap is %u",
                    len, SC_FLASH_UF2_MAX_BYTES);
        return SC_FLASH_ERR_TOO_LARGE;
    }
    if ((len % SC_FLASH_UF2_BLOCK_SIZE) != 0u) {
        (void)fclose(f);
        write_error(error_buf, error_size,
                    "file size %zu is not a multiple of %u",
                    len, SC_FLASH_UF2_BLOCK_SIZE);
        return SC_FLASH_ERR_NOT_BLOCK_ALIGNED;
    }

    uint8_t *buf = (uint8_t *)malloc(len);
    if (buf == NULL) {
        (void)fclose(f);
        write_error(error_buf, error_size, "out of memory");
        return SC_FLASH_ERR_FILE_READ;
    }
    const size_t n = fread(buf, 1u, len, f);
    (void)fclose(f);
    if (n != len) {
        free(buf);
        write_error(error_buf, error_size, "short read (%zu of %zu)", n, len);
        return SC_FLASH_ERR_FILE_READ;
    }

    const size_t total_blocks_seen = len / SC_FLASH_UF2_BLOCK_SIZE;
    uint32_t declared_total = 0u;

    for (size_t b = 0u; b < total_blocks_seen; ++b) {
        const uint8_t *block = buf + b * SC_FLASH_UF2_BLOCK_SIZE;

        const uint32_t magic1 = read_u32_le(block + 0);
        if (magic1 != SC_FLASH_UF2_FIRST_MAGIC) {
            free(buf);
            write_error(error_buf, error_size,
                        "block %zu: first magic 0x%08X != 0x%08X",
                        b, magic1, SC_FLASH_UF2_FIRST_MAGIC);
            return SC_FLASH_ERR_BAD_FIRST_MAGIC;
        }
        const uint32_t magic2 = read_u32_le(block + 4);
        if (magic2 != SC_FLASH_UF2_SECOND_MAGIC) {
            free(buf);
            write_error(error_buf, error_size,
                        "block %zu: second magic 0x%08X != 0x%08X",
                        b, magic2, SC_FLASH_UF2_SECOND_MAGIC);
            return SC_FLASH_ERR_BAD_SECOND_MAGIC;
        }
        const uint32_t end_magic = read_u32_le(block + 508);
        if (end_magic != SC_FLASH_UF2_END_MAGIC) {
            free(buf);
            write_error(error_buf, error_size,
                        "block %zu: end magic 0x%08X != 0x%08X",
                        b, end_magic, SC_FLASH_UF2_END_MAGIC);
            return SC_FLASH_ERR_BAD_END_MAGIC;
        }
        const uint32_t family = read_u32_le(block + 28);
        if (family != SC_FLASH_UF2_FAMILY_RP2040) {
            free(buf);
            write_error(error_buf, error_size,
                        "block %zu: family id 0x%08X != RP2040 (0x%08X)",
                        b, family, SC_FLASH_UF2_FAMILY_RP2040);
            return SC_FLASH_ERR_WRONG_FAMILY;
        }
        const uint32_t block_index = read_u32_le(block + 20);
        const uint32_t block_total = read_u32_le(block + 24);
        if (b == 0u) {
            declared_total = block_total;
        } else if (block_total != declared_total) {
            free(buf);
            write_error(error_buf, error_size,
                        "block %zu: total_blocks %u disagrees with first block's %u",
                        b, block_total, declared_total);
            return SC_FLASH_ERR_BLOCK_INDEX_OUT_OF_RANGE;
        }
        if (block_index >= block_total) {
            free(buf);
            write_error(error_buf, error_size,
                        "block %zu: index %u >= total %u",
                        b, block_index, block_total);
            return SC_FLASH_ERR_BLOCK_INDEX_OUT_OF_RANGE;
        }
    }

    free(buf);
    write_error(error_buf, error_size,
                "OK: %zu blocks, family RP2040",
                total_blocks_seen);
    return SC_FLASH_OK;
}

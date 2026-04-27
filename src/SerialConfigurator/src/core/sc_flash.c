#include "sc_flash.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__APPLE__)
#  include <dirent.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <time.h>
#  include <unistd.h>
#  define SC_FLASH_HAVE_POSIX_WATCHER 1
#else
#  define SC_FLASH_HAVE_POSIX_WATCHER 0
#endif

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
    case SC_FLASH_ERR_BOOTSEL_TIMEOUT:         return "BOOTSEL_TIMEOUT";
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

/* ── Phase 6.3 — BOOTSEL drive watcher ───────────────────────────── */

/* Match: name starts with "RPI-RP2" (RPI-RP2, RPI-RP2350, ...) OR
 * equals "RP2350". The RP2040 ROM mounts as "RPI-RP2"; RP2350 ROMs
 * vary by firmware version — both spellings are common in the wild. */
static bool dir_name_matches_bootsel(const char *name)
{
    if (name == NULL) {
        return false;
    }
    static const char k_prefix[] = "RPI-RP2";
    if (strncmp(name, k_prefix, sizeof(k_prefix) - 1u) == 0) {
        return true;
    }
    if (strcmp(name, "RP2350") == 0) {
        return true;
    }
    return false;
}

#if SC_FLASH_HAVE_POSIX_WATCHER

static bool bootsel_scan_dir_once(const char *parent,
                                  char *out_path, size_t out_path_size)
{
    DIR *d = opendir(parent);
    if (d == NULL) {
        /* Parent does not exist (no media mounted yet) — not an error,
         * just keep polling. */
        return false;
    }
    struct dirent *ent;
    bool matched = false;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.' && (name[1] == '\0' ||
                                (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }
        if (!dir_name_matches_bootsel(name)) {
            continue;
        }

        /* Confirm it really is a directory (BOOTSEL drives mount as
         * a directory; a stray file with that name should not match). */
        char candidate[1024];
        const int written = snprintf(candidate, sizeof(candidate),
                                     "%s/%s", parent, name);
        if (written < 0 || (size_t)written >= sizeof(candidate)) {
            continue;
        }
        struct stat st;
        if (stat(candidate, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }
        if (out_path != NULL && out_path_size > 0u) {
            (void)snprintf(out_path, out_path_size, "%s", candidate);
        }
        matched = true;
        break;
    }
    (void)closedir(d);
    return matched;
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return (uint64_t)ts.tv_sec * 1000u +
           (uint64_t)(ts.tv_nsec / 1000000);
}

static void sleep_ms(uint32_t ms)
{
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000u);
    req.tv_nsec = (long)((ms % 1000u) * 1000000L);
    (void)nanosleep(&req, NULL);
}

#endif /* SC_FLASH_HAVE_POSIX_WATCHER */

sc_flash_status_t sc_flash__watch_for_bootsel_in(
    const char *const *parent_dirs, size_t parent_count,
    uint32_t timeout_ms,
    char *out_path, size_t out_path_size,
    char *error_buf, size_t error_size)
{
    if (parent_dirs == NULL || parent_count == 0u) {
        write_error(error_buf, error_size, "no parent dirs");
        return SC_FLASH_ERR_NULL_ARG;
    }
    if (out_path != NULL && out_path_size > 0u) {
        out_path[0] = '\0';
    }

#if SC_FLASH_HAVE_POSIX_WATCHER
    const uint64_t start_ms = monotonic_ms();
    const uint64_t deadline_ms = start_ms + (uint64_t)timeout_ms;
    for (;;) {
        for (size_t i = 0u; i < parent_count; ++i) {
            if (parent_dirs[i] == NULL) {
                continue;
            }
            if (bootsel_scan_dir_once(parent_dirs[i],
                                      out_path, out_path_size)) {
                write_error(error_buf, error_size,
                            "found at %s",
                            (out_path != NULL) ? out_path : "(no buf)");
                return SC_FLASH_OK;
            }
        }
        const uint64_t now_ms = monotonic_ms();
        if (now_ms >= deadline_ms) {
            write_error(error_buf, error_size,
                        "no BOOTSEL drive after %u ms", (unsigned)timeout_ms);
            return SC_FLASH_ERR_BOOTSEL_TIMEOUT;
        }
        const uint64_t remaining = deadline_ms - now_ms;
        sleep_ms((uint32_t)((remaining < 100u) ? remaining : 100u));
    }
#else
    (void)timeout_ms;
    (void)out_path;
    (void)out_path_size;
    write_error(error_buf, error_size,
                "BOOTSEL watcher not implemented on this platform");
    return SC_FLASH_ERR_NOT_IMPLEMENTED;
#endif
}

sc_flash_status_t sc_flash_watch_for_bootsel(uint32_t timeout_ms,
                                             char *out_path,
                                             size_t out_path_size,
                                             char *error_buf,
                                             size_t error_size)
{
#if SC_FLASH_HAVE_POSIX_WATCHER
    const char *user = getenv("USER");
    if (user == NULL || user[0] == '\0') {
        user = getenv("LOGNAME");
    }
    if (user == NULL || user[0] == '\0') {
        write_error(error_buf, error_size,
                    "USER/LOGNAME unset — cannot resolve standard mount roots");
        return SC_FLASH_ERR_NULL_ARG;
    }

    char path_a[512];
    char path_b[512];
    (void)snprintf(path_a, sizeof(path_a), "/media/%s", user);
    (void)snprintf(path_b, sizeof(path_b), "/run/media/%s", user);
    const char *parents[2] = { path_a, path_b };

    return sc_flash__watch_for_bootsel_in(parents, 2u, timeout_ms,
                                          out_path, out_path_size,
                                          error_buf, error_size);
#else
    (void)timeout_ms;
    (void)out_path;
    (void)out_path_size;
    write_error(error_buf, error_size,
                "BOOTSEL watcher not implemented on this platform");
    return SC_FLASH_ERR_NOT_IMPLEMENTED;
#endif
}

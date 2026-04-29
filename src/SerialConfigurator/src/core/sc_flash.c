#include "sc_flash.h"
#include "../config.h"

#include <errno.h>
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

/* ── Diagnostic logging ──────────────────────────────────────────────
 * The flash flow has several layers (auth -> reboot -> watch BOOTSEL ->
 * copy -> wait re-enum), each of which can fail with a generic
 * "timeout" / "not found" code. When that happens the operator needs
 * to know *what the watcher actually saw on disk*, not just the code,
 * so the diagnostics below print a `[sc_flash]` trace to stderr.
 *
 * - `flash_log` is always-on. Used for one-time, high-signal events:
 *   the resolved $USER, the parent paths being polled, the first
 *   `opendir()` errno per parent, periodic heartbeats, and the
 *   timeout snapshot. Off-the-record under normal happy-path runs.
 * - `flash_log_v` is for the per-iteration noise (every readdir
 *   entry, every poll) and is gated by the compile-time
 *   `SC_DEBUG_DEEP` macro defined in src/config.h. */

static void flash_log(const char *fmt, ...)
{
    va_list ap;
    (void)fputs("[sc_flash] ", stderr);
    va_start(ap, fmt);
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);
    (void)fputc('\n', stderr);
    (void)fflush(stderr);
}

#ifdef SC_DEBUG_DEEP
static void flash_log_v(const char *fmt, ...)
{
    va_list ap;
    (void)fputs("[sc_flash] ", stderr);
    va_start(ap, fmt);
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);
    (void)fputc('\n', stderr);
    (void)fflush(stderr);
}
#else
#  define flash_log_v(...) ((void)0)
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
    case SC_FLASH_ERR_FILE_WRITE:              return "FILE_WRITE";
    case SC_FLASH_ERR_REENUM_TIMEOUT:          return "REENUM_TIMEOUT";
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
                "%zu blocks, family RP2040",
                total_blocks_seen);
    return SC_FLASH_OK;
}

/* ── Phase 6.3 - BOOTSEL drive watcher ───────────────────────────── */

/* Match: name starts with "RPI-RP2" (RPI-RP2, RPI-RP2350, ...) OR
 * equals "RP2350". The RP2040 ROM mounts as "RPI-RP2"; RP2350 ROMs
 * vary by firmware version - both spellings are common in the wild. */
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

/* Snapshot of one parent dir's last poll. Used both for the per-iteration
 * verbose trace and for the structured summary surfaced on timeout. */
#define SC_FLASH_OBS_NAME_MAX     96u
#define SC_FLASH_OBS_CAPTURED_MAX 8u

typedef struct {
    bool   exists;        /* opendir() succeeded */
    int    open_errno;    /* errno when opendir() failed */
    size_t entry_count;   /* total non-dotted entries seen */
    size_t captured;      /* entries kept in @c first_entries */
    char   first_entries[SC_FLASH_OBS_CAPTURED_MAX][SC_FLASH_OBS_NAME_MAX];
    char   reject_reason[SC_FLASH_OBS_NAME_MAX]; /* set when a name matched the
                                                  * BOOTSEL pattern but was
                                                  * rejected (e.g. not a dir);
                                                  * empty otherwise */
} parent_obs_t;

static void parent_obs_reset(parent_obs_t *o)
{
    memset(o, 0, sizeof(*o));
}

static void parent_obs_record(parent_obs_t *o, const char *name)
{
    o->entry_count++;
    if (o->captured < SC_FLASH_OBS_CAPTURED_MAX) {
        /* Bound the conversion explicitly so glibc FORTIFY does not
         * trip -Wformat-truncation when the dirent name (up to 255
         * bytes) is wider than the snapshot slot. */
        (void)snprintf(o->first_entries[o->captured],
                       sizeof(o->first_entries[0]),
                       "%.*s",
                       (int)(sizeof(o->first_entries[0]) - 1u),
                       name);
        o->captured++;
    }
}

/* Append a one-line readable summary of @p o to dest+off. Caller-bounded;
 * never overruns. Returns the new offset. */
static size_t parent_obs_append(char *dest, size_t dest_size, size_t off,
                                const char *parent, const parent_obs_t *o)
{
    if (dest == NULL || dest_size == 0u || off >= dest_size) {
        return off;
    }
    int n;
    if (!o->exists) {
        n = snprintf(dest + off, dest_size - off,
                     "; parent='%s' opendir_errno=%d (%s)",
                     parent, o->open_errno, strerror(o->open_errno));
    } else if (o->entry_count == 0u) {
        n = snprintf(dest + off, dest_size - off,
                     "; parent='%s' exists empty", parent);
    } else {
        n = snprintf(dest + off, dest_size - off,
                     "; parent='%s' exists entries=%zu [",
                     parent, o->entry_count);
        if (n > 0 && (size_t)n < dest_size - off) {
            off += (size_t)n;
            for (size_t i = 0u; i < o->captured; ++i) {
                int m = snprintf(dest + off, dest_size - off,
                                 "%s%s", (i > 0u) ? "," : "",
                                 o->first_entries[i]);
                if (m <= 0 || (size_t)m >= dest_size - off) {
                    break;
                }
                off += (size_t)m;
            }
            if (o->entry_count > o->captured) {
                int m = snprintf(dest + off, dest_size - off, ",...");
                if (m > 0 && (size_t)m < dest_size - off) {
                    off += (size_t)m;
                }
            }
            int m = snprintf(dest + off, dest_size - off, "]");
            if (m > 0 && (size_t)m < dest_size - off) {
                off += (size_t)m;
            }
            if (o->reject_reason[0] != '\0') {
                int r = snprintf(dest + off, dest_size - off,
                                 " reject=%s", o->reject_reason);
                if (r > 0 && (size_t)r < dest_size - off) {
                    off += (size_t)r;
                }
            }
            return off;
        }
    }
    if (n > 0 && (size_t)n < dest_size - off) {
        off += (size_t)n;
    }
    if (o->reject_reason[0] != '\0' && o->exists && o->entry_count == 0u) {
        int r = snprintf(dest + off, dest_size - off,
                         " reject=%s", o->reject_reason);
        if (r > 0 && (size_t)r < dest_size - off) {
            off += (size_t)r;
        }
    }
    return off;
}

/* Scan @p parent once. Fills @p out_path on a positive match. Always
 * writes a fresh snapshot into @p obs so the caller can log it / dump
 * it on timeout. */
static bool bootsel_scan_dir_once(const char *parent,
                                  char *out_path, size_t out_path_size,
                                  parent_obs_t *obs)
{
    parent_obs_reset(obs);
    DIR *d = opendir(parent);
    if (d == NULL) {
        obs->open_errno = errno;
        flash_log_v("opendir('%s') failed: errno=%d (%s)",
                    parent, obs->open_errno, strerror(obs->open_errno));
        return false;
    }
    obs->exists = true;
    struct dirent *ent;
    bool matched = false;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.' && (name[1] == '\0' ||
                                (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }
        parent_obs_record(obs, name);
        if (!dir_name_matches_bootsel(name)) {
            flash_log_v("'%s/%s' ignored: name does not match BOOTSEL pattern",
                        parent, name);
            continue;
        }

        /* Confirm it really is a directory (BOOTSEL drives mount as
         * a directory; a stray file with that name should not match). */
        char candidate[1024];
        const int written = snprintf(candidate, sizeof(candidate),
                                     "%s/%s", parent, name);
        if (written < 0 || (size_t)written >= sizeof(candidate)) {
            (void)snprintf(obs->reject_reason, sizeof(obs->reject_reason),
                           "'%.40s' path too long", name);
            flash_log("'%s/%s' rejected: composed path exceeds buffer",
                      parent, name);
            continue;
        }
        struct stat st;
        if (stat(candidate, &st) != 0) {
            const int e = errno;
            (void)snprintf(obs->reject_reason, sizeof(obs->reject_reason),
                           "'%.40s' stat errno=%d", name, e);
            flash_log("'%s' rejected: stat failed errno=%d (%s)",
                      candidate, e, strerror(e));
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            (void)snprintf(obs->reject_reason, sizeof(obs->reject_reason),
                           "'%.40s' not_a_dir mode=0%o",
                           name, (unsigned)(st.st_mode & 0xFFFFu));
            flash_log("'%s' rejected: not a directory (mode=0%o)",
                      candidate, (unsigned)(st.st_mode & 0xFFFFu));
            continue;
        }
        if (out_path != NULL && out_path_size > 0u) {
            (void)snprintf(out_path, out_path_size, "%s", candidate);
        }
        flash_log("BOOTSEL match: '%s'", candidate);
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

/* Scan /dev/disk/by-label for BOOTSEL block devices.
 *
 * On a healthy desktop session udisks2 picks the device up and mounts
 * it under /media/$USER or /run/media/$USER - and the directory-based
 * scan elsewhere finds it. On hosts where udisks2 is dormant (the
 * GUI was launched outside an active seat, sudo'd, started before the
 * session bus came up, or simply Mint Cinnamon's auto-mount being
 * picky after a suspend/resume) the block device is enumerated but
 * never auto-mounted, and the directory scan polls forever against an
 * empty `/media/$USER`.
 *
 * The label-dir lookup gives us a stable second signal: if the
 * symlink exists, BOOTSEL is on the bus; if /media/$USER stays empty
 * past that, we know auto-mount is the problem rather than enumeration.
 *
 * @param out_label  receives the basename ("RPI-RP2" / "RP2350").
 * @param out_dev    receives the realpath() of the device (e.g.
 *                   "/dev/sdc1") so callers can hand it to udisksctl.
 * @return true on first match, false otherwise (incl. label dir not
 *         existing, which is harmless on non-Linux or stripped distros). */
static bool bootsel_label_scan_once(char *out_label, size_t out_label_size,
                                    char *out_dev,   size_t out_dev_size)
{
    static const char k_label_dir[] = "/dev/disk/by-label";
    DIR *d = opendir(k_label_dir);
    if (d == NULL) {
        return false;
    }
    struct dirent *ent;
    bool found = false;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.' && (name[1] == '\0' ||
                                (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }
        if (!dir_name_matches_bootsel(name)) {
            continue;
        }
        /* Hand the by-label symlink itself to callers / `udisksctl -b`.
         * udisks2 resolves the symlink to the real block device on its
         * end, so we don't need to canonicalize here (and skipping the
         * realpath() call sidesteps the extra POSIX feature-test macros
         * glibc demands for it under -std=c11). */
        char link_path[1024];
        const int n = snprintf(link_path, sizeof(link_path), "%s/%s",
                               k_label_dir, name);
        if (n < 0 || (size_t)n >= sizeof(link_path)) {
            continue;
        }
        struct stat st;
        if (stat(link_path, &st) != 0) {
            flash_log_v("stat('%s') failed: errno=%d (%s)",
                        link_path, errno, strerror(errno));
            continue;
        }
        if (!S_ISBLK(st.st_mode)) {
            flash_log_v("'%s' is not a block device (mode=0%o), ignoring",
                        link_path, (unsigned)(st.st_mode & 0xFFFFu));
            continue;
        }
        if (out_label != NULL && out_label_size > 0u) {
            (void)snprintf(out_label, out_label_size,
                           "%.*s",
                           (int)(out_label_size - 1u), name);
        }
        if (out_dev != NULL && out_dev_size > 0u) {
            (void)snprintf(out_dev, out_dev_size,
                           "%.*s",
                           (int)(out_dev_size - 1u), link_path);
        }
        found = true;
        break;
    }
    (void)closedir(d);
    return found;
}

/* Decode an octal-escaped path from /proc/self/mountinfo into @p out
 * in place. The kernel encodes spaces and other awkward bytes as
 * `\NNN` octal triplets (e.g. "/media/some\040user/RPI-RP2"). For
 * BOOTSEL drives this rarely matters but the decoder costs nothing
 * and avoids a footgun the next time the operator's username has a
 * space or unicode in it. Returns the decoded length. */
static size_t mountinfo_decode_path(char *p)
{
    char *r = p;
    char *w = p;
    while (*r != '\0') {
        if (r[0] == '\\' && r[1] >= '0' && r[1] <= '7' &&
            r[2] >= '0' && r[2] <= '7' &&
            r[3] >= '0' && r[3] <= '7') {
            const unsigned v = (unsigned)((r[1] - '0') * 64 +
                                          (r[2] - '0') * 8  +
                                          (r[3] - '0'));
            *w++ = (char)v;
            r += 4;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
    return (size_t)(w - p);
}

/* Scan /proc/self/mountinfo for a FAT-family mount whose mountpoint
 * basename matches the BOOTSEL pattern (RPI-RP2*, RP2350). This is
 * the SOLE reliable way to find the drive on hosts where udisks2
 * mounts under /media/<other-user>/, /run/media/<other-user>/, or
 * any non-standard location (sysadmin-style /mnt/rpi-rp2, etc.).
 *
 * Returns true on first match. On hit, @p out_path receives the full
 * mountpoint path; @p out_writable becomes true iff the calling
 * process can write the directory (access(path, W_OK) == 0). */
static bool bootsel_find_mountpoint_in_mountinfo(char *out_path,
                                                 size_t out_path_size,
                                                 bool *out_writable)
{
    if (out_writable != NULL) {
        *out_writable = false;
    }
    FILE *f = fopen("/proc/self/mountinfo", "r");
    if (f == NULL) {
        flash_log_v("fopen('/proc/self/mountinfo') failed: errno=%d (%s)",
                    errno, strerror(errno));
        return false;
    }

    /* mountinfo format (one line per mount):
     *   <id> <parent> <major:minor> <root> <mountpoint> <mount-opts>
     *   <optional-fields...> - <fstype> <source> <super-opts>
     * The hyphen separator delimits the variable-length optional
     * fields list from the trailing fixed fields. */
    char line[2048];
    bool found = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        char *tokens[32];
        size_t ntok = 0u;
        char *saveptr = NULL;
        char *tok = strtok_r(line, " \t\n", &saveptr);
        while (tok != NULL && ntok < (sizeof(tokens) / sizeof(tokens[0]))) {
            tokens[ntok++] = tok;
            tok = strtok_r(NULL, " \t\n", &saveptr);
        }
        if (ntok < 7u) {
            continue;
        }
        size_t dash_idx = (size_t)-1;
        for (size_t i = 0u; i < ntok; ++i) {
            if (tokens[i][0] == '-' && tokens[i][1] == '\0') {
                dash_idx = i;
                break;
            }
        }
        if (dash_idx == (size_t)-1 || dash_idx + 2u >= ntok) {
            continue;
        }
        const char *fstype = tokens[dash_idx + 1u];
        if (strcmp(fstype, "vfat") != 0 &&
            strcmp(fstype, "msdos") != 0 &&
            strcmp(fstype, "exfat") != 0) {
            continue;
        }
        char *mountpoint = tokens[4];
        (void)mountinfo_decode_path(mountpoint);
        const char *base = strrchr(mountpoint, '/');
        base = (base != NULL) ? (base + 1) : mountpoint;
        if (!dir_name_matches_bootsel(base)) {
            continue;
        }
        if (out_path != NULL && out_path_size > 0u) {
            (void)snprintf(out_path, out_path_size,
                           "%.*s",
                           (int)(out_path_size - 1u), mountpoint);
        }
        if (out_writable != NULL) {
            *out_writable = (access(mountpoint, W_OK) == 0);
        }
        found = true;
        break;
    }
    (void)fclose(f);
    return found;
}

/* Invoke `udisksctl unmount -b <device>` and capture output. Mirrors
 * @c bootsel_attempt_udisks_mount but for the inverse operation -
 * used to forcibly unmount a /media/root/... mount that the system
 * udisks2 created when the user's session wasn't seat-active. The
 * polkit action is `org.freedesktop.UDisks2.filesystem-unmount-others`
 * which on standard Mint / Ubuntu is allowed for active sessions
 * without password and prompted for admin password otherwise - so
 * this call may fail visibly with a polkit auth error, which the
 * trace captures so the operator can see it. */
static bool bootsel_attempt_udisks_unmount(const char *device,
                                           char *err_buf, size_t err_size)
{
    if (device == NULL || device[0] == '\0') {
        if (err_buf != NULL && err_size > 0u) {
            (void)snprintf(err_buf, err_size, "no device path");
        }
        return false;
    }

    char cmd[1024];
    const int n = snprintf(cmd, sizeof(cmd),
                           "udisksctl unmount -b '%s' 2>&1", device);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        if (err_buf != NULL && err_size > 0u) {
            (void)snprintf(err_buf, err_size, "command too long");
        }
        return false;
    }

    flash_log("unmount: spawning '%s'", cmd);
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        const int e = errno;
        if (err_buf != NULL && err_size > 0u) {
            (void)snprintf(err_buf, err_size, "popen failed: %s", strerror(e));
        }
        flash_log("unmount: popen failed errno=%d (%s)", e, strerror(e));
        return false;
    }
    char line[256];
    char tail[512];
    tail[0] = '\0';
    size_t tail_off = 0u;
    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strlen(line);
        while (len > 0u && (line[len - 1u] == '\n' || line[len - 1u] == '\r')) {
            line[--len] = '\0';
        }
        flash_log("unmount: udisksctl: %s", line);
        if (tail_off < sizeof(tail) - 1u) {
            const int t = snprintf(tail + tail_off, sizeof(tail) - tail_off,
                                   "%s%s", (tail_off > 0u) ? " | " : "", line);
            if (t > 0) {
                tail_off += (size_t)t;
                if (tail_off > sizeof(tail) - 1u) {
                    tail_off = sizeof(tail) - 1u;
                }
            }
        }
    }
    const int rc = pclose(fp);
    if (rc != 0) {
        if (err_buf != NULL && err_size > 0u) {
            (void)snprintf(err_buf, err_size, "rc=%d output='%s'", rc, tail);
        }
        flash_log("unmount: udisksctl rc=%d", rc);
        return false;
    }
    if (err_buf != NULL && err_size > 0u) {
        (void)snprintf(err_buf, err_size, "ok: %s", tail);
    }
    flash_log("unmount: udisksctl ok");
    return true;
}

/* Invoke `udisksctl mount -b <device>` and capture the result. udisks2
 * runs as a daemon owned by root, accessed over the system D-Bus, with
 * polkit gating who may auto-mount. The default Mint / Ubuntu polkit
 * rules grant auto-mount to active session users - so on the unhappy
 * paths we run into here the call usually succeeds and trips udisks2
 * into doing what GNOME / Cinnamon failed to do automatically.
 *
 * Logs the spawned command, every line of output (so polkit auth
 * failures, missing-device errors, busy-mount errors are captured),
 * and the exit code. Caller treats success as "the watcher's parent
 * dir scan should now find the new mount point on its next poll". */
static bool bootsel_attempt_udisks_mount(const char *device,
                                         char *err_buf, size_t err_size)
{
    if (device == NULL || device[0] == '\0') {
        if (err_buf != NULL && err_size > 0u) {
            (void)snprintf(err_buf, err_size, "no device path");
        }
        return false;
    }

    char cmd[1024];
    /* The realpath() output is a canonical /dev/sdXN form with no shell
     * metacharacters. Wrap in single quotes anyway as defense-in-depth
     * - never embed an unquoted user-supplied path into a shell
     * command line. */
    const int n = snprintf(cmd, sizeof(cmd),
                           "udisksctl mount -b '%s' 2>&1", device);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        if (err_buf != NULL && err_size > 0u) {
            (void)snprintf(err_buf, err_size, "command too long");
        }
        return false;
    }

    flash_log("automount: spawning '%s'", cmd);
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        const int e = errno;
        if (err_buf != NULL && err_size > 0u) {
            (void)snprintf(err_buf, err_size, "popen failed: %s", strerror(e));
        }
        flash_log("automount: popen failed errno=%d (%s)", e, strerror(e));
        return false;
    }

    char line[256];
    char tail[512];
    tail[0] = '\0';
    size_t tail_off = 0u;
    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strlen(line);
        while (len > 0u && (line[len - 1u] == '\n' || line[len - 1u] == '\r')) {
            line[--len] = '\0';
        }
        flash_log("automount: udisksctl: %s", line);
        if (tail_off < sizeof(tail) - 1u) {
            const int t = snprintf(tail + tail_off, sizeof(tail) - tail_off,
                                   "%s%s", (tail_off > 0u) ? " | " : "", line);
            if (t > 0) {
                tail_off += (size_t)t;
                if (tail_off > sizeof(tail) - 1u) {
                    tail_off = sizeof(tail) - 1u;
                }
            }
        }
    }
    const int rc = pclose(fp);
    /* `udisksctl mount` returns 0 on success and prints the mount point.
     * Failure modes include polkit auth refused (rc=1), device already
     * mounted (still rc=1 but harmless - we'll find it on next scan),
     * and "object not in registry" (rc=1, indicates udisks2 daemon is
     * not running). The tail string carries the precise reason. */
    if (rc != 0) {
        if (err_buf != NULL && err_size > 0u) {
            (void)snprintf(err_buf, err_size,
                           "rc=%d output='%s'", rc, tail);
        }
        flash_log("automount: udisksctl rc=%d", rc);
        return false;
    }
    if (err_buf != NULL && err_size > 0u) {
        (void)snprintf(err_buf, err_size, "ok: %s", tail);
    }
    flash_log("automount: udisksctl ok");
    return true;
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
        flash_log("watch_for_bootsel: rejected - no parent dirs");
        return SC_FLASH_ERR_NULL_ARG;
    }
    if (out_path != NULL && out_path_size > 0u) {
        out_path[0] = '\0';
    }

#if SC_FLASH_HAVE_POSIX_WATCHER
    /* Cap parents at a small static limit; the production caller passes
     * 2 (`/media/$USER`, `/run/media/$USER`) and tests pass 1 or 2. */
    const size_t scan_count = (parent_count > SC_FLASH_BOOTSEL_PARENTS_MAX)
                                  ? SC_FLASH_BOOTSEL_PARENTS_MAX : parent_count;
    parent_obs_t obs[SC_FLASH_BOOTSEL_PARENTS_MAX];
    bool   prev_existed[SC_FLASH_BOOTSEL_PARENTS_MAX] = { false };
    size_t prev_entries[SC_FLASH_BOOTSEL_PARENTS_MAX] = { 0u };
    bool   logged_open_errno[SC_FLASH_BOOTSEL_PARENTS_MAX] = { false };
    for (size_t i = 0u; i < SC_FLASH_BOOTSEL_PARENTS_MAX; ++i) {
        parent_obs_reset(&obs[i]);
    }

    flash_log("watch_for_bootsel: timeout=%u ms parents=%zu",
              (unsigned)timeout_ms, scan_count);
    for (size_t i = 0u; i < scan_count; ++i) {
        flash_log("  parent[%zu] = '%s'",
                  i, (parent_dirs[i] != NULL) ? parent_dirs[i] : "(null)");
    }

    const uint64_t start_ms = monotonic_ms();
    const uint64_t deadline_ms = start_ms + (uint64_t)timeout_ms;
    uint64_t next_heartbeat_ms = start_ms + SC_FLASH_BOOTSEL_HEARTBEAT_MS;
    uint64_t iters = 0u;

    for (;;) {
        ++iters;
        for (size_t i = 0u; i < scan_count; ++i) {
            if (parent_dirs[i] == NULL) {
                continue;
            }
            const bool matched =
                bootsel_scan_dir_once(parent_dirs[i],
                                      out_path, out_path_size, &obs[i]);

            /* Always-on log on first opendir failure per parent so a
             * stale snapshot doesn't drown out a permission/typo bug. */
            if (!obs[i].exists && !logged_open_errno[i]) {
                flash_log("parent[%zu]='%s' opendir errno=%d (%s)",
                          i, parent_dirs[i], obs[i].open_errno,
                          strerror(obs[i].open_errno));
                logged_open_errno[i] = true;
            }
            /* Always-on log when the parent transitions empty->non-empty
             * (e.g. udisks2 finally mounted something) and when entries
             * appear/disappear, since either is a clear signal for the
             * operator that something on disk changed. */
            if (obs[i].exists != prev_existed[i]) {
                flash_log("parent[%zu]='%s' state %s->%s",
                          i, parent_dirs[i],
                          prev_existed[i] ? "exists" : "missing",
                          obs[i].exists   ? "exists" : "missing");
                prev_existed[i] = obs[i].exists;
            }
            if (obs[i].exists && obs[i].entry_count != prev_entries[i]) {
                char snap[512];
                size_t off = 0u;
                snap[0] = '\0';
                off = parent_obs_append(snap, sizeof(snap), off,
                                        parent_dirs[i], &obs[i]);
                (void)off;
                flash_log("parent[%zu] entry-count %zu->%zu%s",
                          i, prev_entries[i], obs[i].entry_count, snap);
                prev_entries[i] = obs[i].entry_count;
            }

            if (matched) {
                write_error(error_buf, error_size,
                            "found at %s",
                            (out_path != NULL) ? out_path : "(no buf)");
                return SC_FLASH_OK;
            }
        }

        const uint64_t now_ms = monotonic_ms();
        if (now_ms >= next_heartbeat_ms) {
            flash_log("watch_for_bootsel: still polling, elapsed=%llu ms iter=%llu",
                      (unsigned long long)(now_ms - start_ms),
                      (unsigned long long)iters);
            next_heartbeat_ms += SC_FLASH_BOOTSEL_HEARTBEAT_MS;
        }
        if (now_ms >= deadline_ms) {
            /* On timeout dump everything we observed so the operator can
             * see exactly what was on disk. The summary also goes into
             * @p error_buf so it surfaces in the GUI / CLI flash log. */
            char summary[768];
            size_t off = (size_t)snprintf(summary, sizeof(summary),
                "no BOOTSEL drive after %u ms (iters=%llu)",
                (unsigned)timeout_ms, (unsigned long long)iters);
            for (size_t i = 0u; i < scan_count; ++i) {
                if (parent_dirs[i] == NULL) {
                    continue;
                }
                off = parent_obs_append(summary, sizeof(summary), off,
                                        parent_dirs[i], &obs[i]);
            }
            write_error(error_buf, error_size, "%s", summary);
            flash_log("BOOTSEL_TIMEOUT - %s", summary);
            flash_log("hint: confirm the BOOTSEL drive auto-mounted under one of the parents above; "
                      "if the drive shows up elsewhere (e.g. /mnt/...) the watcher will not see it");
            return SC_FLASH_ERR_BOOTSEL_TIMEOUT;
        }
        const uint64_t remaining = deadline_ms - now_ms;
        sleep_ms((uint32_t)((remaining < SC_FLASH_BOOTSEL_POLL_INTERVAL_MS)
                                ? remaining
                                : SC_FLASH_BOOTSEL_POLL_INTERVAL_MS));
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
    if (out_path != NULL && out_path_size > 0u) {
        out_path[0] = '\0';
    }
#if SC_FLASH_HAVE_POSIX_WATCHER
    const char *source = "USER";
    const char *user = getenv("USER");
    if (user == NULL || user[0] == '\0') {
        user = getenv("LOGNAME");
        source = "LOGNAME";
    }
    if (user == NULL || user[0] == '\0') {
        write_error(error_buf, error_size,
                    "USER/LOGNAME unset - cannot resolve standard mount roots");
        flash_log("watch_for_bootsel: rejected - USER and LOGNAME unset; "
                  "the GUI/CLI was launched without a user-session env "
                  "(check `env | grep -E '^(USER|LOGNAME)='`)");
        return SC_FLASH_ERR_NULL_ARG;
    }
    flash_log("watch_for_bootsel: resolved %s='%s'", source, user);

    char path_a[512];
    char path_b[512];
    (void)snprintf(path_a, sizeof(path_a), SC_FLASH_BOOTSEL_PARENT_A_FMT, user);
    (void)snprintf(path_b, sizeof(path_b), SC_FLASH_BOOTSEL_PARENT_B_FMT, user);
    const char *parents[2] = { path_a, path_b };
    const size_t scan_count = 2u;

    flash_log("watch_for_bootsel: timeout=%u ms parents=%zu",
              (unsigned)timeout_ms, scan_count);
    for (size_t i = 0u; i < scan_count; ++i) {
        flash_log("  parent[%zu] = '%s'", i, parents[i]);
    }

    /* Auto-mount opt-out: defaults to enabled. The user can suppress
     * `udisksctl mount -b` by exporting SC_FLASH_NO_AUTOMOUNT=1. The
     * trigger fires only when the by-label block device has been
     * visible for > AUTOMOUNT_GRACE_MS without any directory match,
     * i.e. we give udisks2 a fair chance to do it itself first. */
    const char *no_automount_env = getenv("SC_FLASH_NO_AUTOMOUNT");
    const bool automount_enabled =
        !(no_automount_env != NULL && no_automount_env[0] != '\0' &&
          no_automount_env[0] != '0');
    flash_log("watch_for_bootsel: automount=%s%s",
              automount_enabled ? "enabled" : "disabled",
              automount_enabled ? " (set SC_FLASH_NO_AUTOMOUNT=1 to disable)"
                                : " (SC_FLASH_NO_AUTOMOUNT is set)");

    /* Grace before auto-mount fires once we see the by-label entry.
     * Short enough to beat root's udisks2 (which on Mint typically
     * mounts under /media/root within ~2 s of BOOTSEL appearing) but
     * long enough that we don't fire before udev finishes attaching
     * the partition. */
    parent_obs_t obs[2];
    bool   prev_existed[2] = { false, false };
    size_t prev_entries[2] = { 0u, 0u };
    bool   logged_open_errno[2] = { false, false };
    parent_obs_reset(&obs[0]);
    parent_obs_reset(&obs[1]);

    bool block_dev_seen = false;
    bool block_dev_logged = false;
    bool automount_attempted = false;
    bool automount_succeeded = false;
    bool unmount_others_attempted = false;
    bool unmount_others_succeeded = false;
    char block_label[64];
    char block_dev_path[256];
    char automount_err[512];
    char unmount_err[512];
    block_label[0] = '\0';
    block_dev_path[0] = '\0';
    automount_err[0] = '\0';
    unmount_err[0] = '\0';
    uint64_t block_dev_first_seen_ms = 0u;

    const uint64_t start_ms = monotonic_ms();
    const uint64_t deadline_ms = start_ms + (uint64_t)timeout_ms;
    uint64_t next_heartbeat_ms = start_ms + SC_FLASH_BOOTSEL_HEARTBEAT_MS;
    uint64_t iters = 0u;

    for (;;) {
        ++iters;
        for (size_t i = 0u; i < scan_count; ++i) {
            const bool matched =
                bootsel_scan_dir_once(parents[i],
                                      out_path, out_path_size, &obs[i]);

            if (!obs[i].exists && !logged_open_errno[i]) {
                flash_log("parent[%zu]='%s' opendir errno=%d (%s)",
                          i, parents[i], obs[i].open_errno,
                          strerror(obs[i].open_errno));
                logged_open_errno[i] = true;
            }
            if (obs[i].exists != prev_existed[i]) {
                flash_log("parent[%zu]='%s' state %s->%s",
                          i, parents[i],
                          prev_existed[i] ? "exists" : "missing",
                          obs[i].exists   ? "exists" : "missing");
                prev_existed[i] = obs[i].exists;
            }
            if (obs[i].exists && obs[i].entry_count != prev_entries[i]) {
                char snap[512];
                size_t off = 0u;
                snap[0] = '\0';
                off = parent_obs_append(snap, sizeof(snap), off,
                                        parents[i], &obs[i]);
                (void)off;
                flash_log("parent[%zu] entry-count %zu->%zu%s",
                          i, prev_entries[i], obs[i].entry_count, snap);
                prev_entries[i] = obs[i].entry_count;
            }

            if (matched) {
                write_error(error_buf, error_size,
                            "found at %s",
                            (out_path != NULL) ? out_path : "(no buf)");
                return SC_FLASH_OK;
            }
        }

        /* Mountinfo probe: catches mounts that don't live under
         * /media/$USER - most often /media/root/RPI-RP2 (system
         * udisks2 mounts as root when the user's session isn't seat-
         * active) but also any operator-supplied path like
         * /mnt/rpi-rp2/. The scan above only reaches the standard
         * udisks2 user dirs; this one looks at *every* current
         * mountpoint. */
        char mi_path[512];
        bool mi_writable = false;
        if (bootsel_find_mountpoint_in_mountinfo(mi_path, sizeof(mi_path),
                                                  &mi_writable)) {
            const bool under_user_root =
                (strncmp(mi_path, parents[0], strlen(parents[0])) == 0) ||
                (strncmp(mi_path, parents[1], strlen(parents[1])) == 0);
            if (mi_writable) {
                if (out_path != NULL && out_path_size > 0u) {
                    (void)snprintf(out_path, out_path_size, "%.*s",
                                   (int)(out_path_size - 1u), mi_path);
                }
                flash_log("BOOTSEL match (mountinfo): '%s'%s",
                          mi_path,
                          under_user_root ? ""
                            : " - outside /media/$USER, but writable, "
                              "proceeding");
                write_error(error_buf, error_size, "found at %s", mi_path);
                return SC_FLASH_OK;
            }
            /* Found but unwritable. Most common cause: /media/root/...
             * created by the system udisks2 acting as root because the
             * user's polkit session was inactive. Try one round of
             * `udisksctl unmount` - polkit on Mint / Ubuntu allows
             * filesystem-unmount-others for the active session
             * without prompting; if our session is non-active we lose
             * here too and surface the actionable error. */
            flash_log("BOOTSEL mounted at '%s' but uid=%u cannot write - "
                      "trying udisksctl unmount once before giving up",
                      mi_path, (unsigned)getuid());
            if (automount_enabled && !unmount_others_attempted &&
                block_dev_path[0] != '\0') {
                unmount_others_succeeded = bootsel_attempt_udisks_unmount(
                    block_dev_path, unmount_err, sizeof(unmount_err));
                unmount_others_attempted = true;
                if (unmount_others_succeeded) {
                    /* Reset auto-mount latch so the next iteration can
                     * remount the device under our session. */
                    automount_attempted = false;
                    automount_succeeded = false;
                    block_dev_first_seen_ms = monotonic_ms();
                    flash_log("unmount succeeded - re-arming automount; "
                              "next iteration will mount as uid=%u",
                              (unsigned)getuid());
                    /* Sleep briefly so udev / udisks2 settles on the
                     * new state before we look again. */
                    sleep_ms(200u);
                    continue;
                }
                flash_log("unmount failed - surfacing remediation: %s",
                          unmount_err);
            }
            char summary[1024];
            size_t s_off = (size_t)snprintf(summary, sizeof(summary),
                "BOOTSEL drive mounted at '%.200s' but the current user "
                "(uid=%u) cannot write there. Most likely udisks2 "
                "mounted it as root (active-seat policy).",
                mi_path, (unsigned)getuid());
            if (unmount_others_attempted) {
                int n = snprintf(summary + s_off, sizeof(summary) - s_off,
                    " Auto-unmount attempt: %s",
                    unmount_others_succeeded ? "ok-but-still-not-writable"
                                             : unmount_err);
                if (n > 0 && (size_t)n < sizeof(summary) - s_off) {
                    s_off += (size_t)n;
                }
            }
            int n2 = snprintf(summary + s_off, sizeof(summary) - s_off,
                " Remediation: (a) `sudo umount '%.200s'` and rerun - "
                "udisks2 should remount under /media/$USER for the active "
                "session, or (b) run the flasher as root, or (c) "
                "fix `loginctl session-status` so polkit allows "
                "filesystem-unmount-others for your session.",
                mi_path);
            if (n2 > 0 && (size_t)n2 < sizeof(summary) - s_off) {
                s_off += (size_t)n2;
            }
            write_error(error_buf, error_size, "%s", summary);
            flash_log("BOOTSEL drive at '%s' is NOT writable for uid=%u - "
                      "aborting WAIT_BOOTSEL early. %s",
                      mi_path, (unsigned)getuid(), summary);
            return SC_FLASH_ERR_BOOTSEL_TIMEOUT;
        }

        /* Block-device probe: catches "BOOTSEL is on the bus but
         * udisks2 has not auto-mounted it" - the killer mode on Mint
         * Cinnamon and headless / sudo'd sessions. */
        char tmp_label[64];
        char tmp_dev[256];
        if (bootsel_label_scan_once(tmp_label, sizeof(tmp_label),
                                    tmp_dev,   sizeof(tmp_dev))) {
            if (!block_dev_seen) {
                block_dev_seen = true;
                block_dev_first_seen_ms = monotonic_ms();
                (void)snprintf(block_label, sizeof(block_label),
                               "%.*s", (int)(sizeof(block_label) - 1u),
                               tmp_label);
                (void)snprintf(block_dev_path, sizeof(block_dev_path),
                               "%.*s", (int)(sizeof(block_dev_path) - 1u),
                               tmp_dev);
            }
            if (!block_dev_logged) {
                flash_log("block device visible: /dev/disk/by-label/%s -> %s "
                          "(no /media/$USER mount yet - udisks2 may be "
                          "dormant in this session)",
                          block_label, block_dev_path);
                block_dev_logged = true;
            }

            if (automount_enabled && !automount_attempted) {
                const uint64_t elapsed_since_seen =
                    monotonic_ms() - block_dev_first_seen_ms;
                if (elapsed_since_seen >= SC_FLASH_AUTOMOUNT_GRACE_MS) {
                    flash_log("automount: %u ms grace expired with no "
                              "auto-mount; invoking udisksctl on '%s'",
                              (unsigned)elapsed_since_seen, block_dev_path);
                    automount_succeeded = bootsel_attempt_udisks_mount(
                        block_dev_path, automount_err,
                        sizeof(automount_err));
                    if (!automount_succeeded) {
                        flash_log("automount: failed - %s. Hint: try "
                                  "manually `udisksctl mount -b %s` and "
                                  "rerun, or check polkit "
                                  "(/etc/polkit-1/...) and udisks2 "
                                  "service status.",
                                  automount_err, block_dev_path);
                    }
                    automount_attempted = true;
                }
            }
        }

        const uint64_t now_ms = monotonic_ms();
        if (now_ms >= next_heartbeat_ms) {
            flash_log("watch_for_bootsel: still polling, elapsed=%llu ms "
                      "iter=%llu%s%s",
                      (unsigned long long)(now_ms - start_ms),
                      (unsigned long long)iters,
                      block_dev_seen ? " [block_dev_visible]" : "",
                      automount_succeeded ? " [automount_ok]"
                        : (automount_attempted ? " [automount_failed]" : ""));
            next_heartbeat_ms += SC_FLASH_BOOTSEL_HEARTBEAT_MS;
        }
        if (now_ms >= deadline_ms) {
            char summary[1024];
            size_t off = (size_t)snprintf(summary, sizeof(summary),
                "no BOOTSEL drive after %u ms (iters=%llu)",
                (unsigned)timeout_ms, (unsigned long long)iters);
            for (size_t i = 0u; i < scan_count; ++i) {
                off = parent_obs_append(summary, sizeof(summary), off,
                                        parents[i], &obs[i]);
            }
            if (block_dev_seen) {
                int n = snprintf(summary + off, sizeof(summary) - off,
                    "; block_device='/dev/disk/by-label/%s'->'%s' visible "
                    "but not mounted",
                    block_label, block_dev_path);
                if (n > 0 && (size_t)n < sizeof(summary) - off) {
                    off += (size_t)n;
                }
                if (automount_attempted) {
                    n = snprintf(summary + off, sizeof(summary) - off,
                        "; automount %s (%s)",
                        automount_succeeded ? "ok-but-no-mountpoint"
                                            : "failed",
                        automount_err);
                    if (n > 0 && (size_t)n < sizeof(summary) - off) {
                        off += (size_t)n;
                    }
                } else {
                    n = snprintf(summary + off, sizeof(summary) - off,
                        "; automount not_attempted (SC_FLASH_NO_AUTOMOUNT "
                        "in effect - try `udisksctl mount -b %s`)",
                        block_dev_path);
                    if (n > 0 && (size_t)n < sizeof(summary) - off) {
                        off += (size_t)n;
                    }
                }
            }
            write_error(error_buf, error_size, "%s", summary);
            flash_log("BOOTSEL_TIMEOUT - %s", summary);
            return SC_FLASH_ERR_BOOTSEL_TIMEOUT;
        }
        const uint64_t remaining = deadline_ms - now_ms;
        sleep_ms((uint32_t)((remaining < SC_FLASH_BOOTSEL_POLL_INTERVAL_MS)
                                ? remaining
                                : SC_FLASH_BOOTSEL_POLL_INTERVAL_MS));
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

/* ── Phase 6.4 - UF2 copy with progress + re-enumeration waiter ─── */

sc_flash_status_t sc_flash_copy_uf2(const char *src_uf2_path,
                                    const char *drive_path,
                                    sc_flash_progress_cb progress_cb,
                                    void *progress_user,
                                    char *error_buf,
                                    size_t error_size)
{
    if (src_uf2_path == NULL || drive_path == NULL) {
        write_error(error_buf, error_size, "null path");
        return SC_FLASH_ERR_NULL_ARG;
    }

    FILE *src = fopen(src_uf2_path, "rb");
    if (src == NULL) {
        write_error(error_buf, error_size,
                    "could not open source '%s'", src_uf2_path);
        return SC_FLASH_ERR_FILE_OPEN;
    }

    if (fseek(src, 0, SEEK_END) != 0) {
        (void)fclose(src);
        write_error(error_buf, error_size, "fseek end failed");
        return SC_FLASH_ERR_FILE_READ;
    }
    const long len_signed = ftell(src);
    if (len_signed < 0) {
        (void)fclose(src);
        write_error(error_buf, error_size, "ftell failed");
        return SC_FLASH_ERR_FILE_READ;
    }
    if (fseek(src, 0, SEEK_SET) != 0) {
        (void)fclose(src);
        write_error(error_buf, error_size, "fseek start failed");
        return SC_FLASH_ERR_FILE_READ;
    }
    const size_t total = (size_t)len_signed;

    if (total == 0u) {
        (void)fclose(src);
        write_error(error_buf, error_size, "source file is empty");
        return SC_FLASH_ERR_EMPTY;
    }
    if (total > SC_FLASH_UF2_MAX_BYTES) {
        (void)fclose(src);
        write_error(error_buf, error_size,
                    "source is %zu bytes, cap is %u",
                    total, SC_FLASH_UF2_MAX_BYTES);
        return SC_FLASH_ERR_TOO_LARGE;
    }

    char dest_path[1024];
    const int dest_len = snprintf(dest_path, sizeof(dest_path),
                                  "%s/%s", drive_path,
                                  SC_FLASH_COPY_DEST_FILENAME);
    if (dest_len < 0 || (size_t)dest_len >= sizeof(dest_path)) {
        (void)fclose(src);
        write_error(error_buf, error_size,
                    "drive_path too long for buffer");
        return SC_FLASH_ERR_NULL_ARG;
    }

    FILE *dst = fopen(dest_path, "wb");
    if (dst == NULL) {
        (void)fclose(src);
        write_error(error_buf, error_size,
                    "could not open destination '%s'", dest_path);
        return SC_FLASH_ERR_FILE_WRITE;
    }

    uint8_t chunk[SC_FLASH_COPY_CHUNK_BYTES];
    uint64_t copied = 0u;
    while (copied < total) {
        const size_t want = ((total - copied) < SC_FLASH_COPY_CHUNK_BYTES)
                                ? (size_t)(total - copied)
                                : SC_FLASH_COPY_CHUNK_BYTES;
        const size_t got = fread(chunk, 1u, want, src);
        if (got != want) {
            (void)fclose(src);
            (void)fclose(dst);
            write_error(error_buf, error_size,
                        "short read at %llu / %zu",
                        (unsigned long long)copied, total);
            return SC_FLASH_ERR_FILE_READ;
        }
        const size_t put = fwrite(chunk, 1u, got, dst);
        if (put != got) {
            (void)fclose(src);
            (void)fclose(dst);
            write_error(error_buf, error_size,
                        "short write at %llu / %zu",
                        (unsigned long long)copied, total);
            return SC_FLASH_ERR_FILE_WRITE;
        }
        copied += (uint64_t)put;
        if (progress_cb != NULL) {
            progress_cb(copied, (uint64_t)total, progress_user);
        }
    }

    (void)fclose(src);

    /* Flush to the physical device before declaring success. The
     * boot ROM watches the mass-storage write stream and reboots
     * once the file is fully landed; if we return without fsync()
     * the caller may race against the kernel's writeback queue. */
    if (fflush(dst) != 0) {
        (void)fclose(dst);
        write_error(error_buf, error_size, "fflush failed");
        return SC_FLASH_ERR_FILE_WRITE;
    }
#if SC_FLASH_HAVE_POSIX_WATCHER
    {
        const int fd = fileno(dst);
        if (fd >= 0) {
            (void)fsync(fd);
        }
    }
#endif
    (void)fclose(dst);

    write_error(error_buf, error_size,
                "OK: copied %zu bytes to %s", total, dest_path);
    return SC_FLASH_OK;
}

sc_flash_status_t sc_flash__wait_reenumeration_in(
    const char *parent_dir, const char *uid_hex,
    uint32_t timeout_ms,
    char *out_path, size_t out_path_size,
    char *error_buf, size_t error_size)
{
    if (parent_dir == NULL || uid_hex == NULL || uid_hex[0] == '\0') {
        write_error(error_buf, error_size, "null/empty inputs");
        flash_log("wait_reenumeration: rejected - parent_dir=%s uid_hex=%s",
                  (parent_dir != NULL) ? parent_dir : "(null)",
                  (uid_hex != NULL) ? (uid_hex[0] == '\0' ? "(empty)" : uid_hex)
                                    : "(null)");
        return SC_FLASH_ERR_NULL_ARG;
    }
    if (out_path != NULL && out_path_size > 0u) {
        out_path[0] = '\0';
    }

#if SC_FLASH_HAVE_POSIX_WATCHER
    flash_log("wait_reenumeration: parent='%s' uid='%s' timeout=%u ms",
              parent_dir, uid_hex, (unsigned)timeout_ms);

    parent_obs_t obs;
    parent_obs_reset(&obs);
    bool   prev_existed     = false;
    size_t prev_entries     = 0u;
    bool   logged_open_err  = false;
    const uint64_t start_ms = monotonic_ms();
    const uint64_t deadline_ms = start_ms + (uint64_t)timeout_ms;
    uint64_t next_heartbeat_ms = start_ms + SC_FLASH_BOOTSEL_HEARTBEAT_MS;
    uint64_t iters = 0u;

    for (;;) {
        ++iters;
        parent_obs_reset(&obs);
        DIR *d = opendir(parent_dir);
        if (d != NULL) {
            obs.exists = true;
            struct dirent *ent;
            bool matched = false;
            /* Sized to NAME_MAX+1 so a full dirent name fits without
             * triggering -Wformat-truncation. The host's by-id symlink
             * names are well under this in practice. */
            char matched_name[256];
            matched_name[0] = '\0';
            while ((ent = readdir(d)) != NULL) {
                const char *name = ent->d_name;
                if (name[0] == '.' && (name[1] == '\0' ||
                                        (name[1] == '.' &&
                                         name[2] == '\0'))) {
                    continue;
                }
                parent_obs_record(&obs, name);
                if (strstr(name, uid_hex) == NULL) {
                    flash_log_v("'%s/%s' ignored: does not contain uid='%s'",
                                parent_dir, name, uid_hex);
                    continue;
                }
                if (!matched) {
                    (void)snprintf(matched_name, sizeof(matched_name),
                                   "%.*s",
                                   (int)(sizeof(matched_name) - 1u), name);
                    matched = true;
                }
            }
            (void)closedir(d);
            if (matched) {
                if (out_path != NULL && out_path_size > 0u) {
                    (void)snprintf(out_path, out_path_size,
                                   "%s/%s", parent_dir, matched_name);
                }
                flash_log("re-enum match: '%s/%s'", parent_dir, matched_name);
                write_error(error_buf, error_size,
                            "found at %s",
                            (out_path != NULL) ? out_path : "(no buf)");
                return SC_FLASH_OK;
            }
        } else {
            obs.open_errno = errno;
            if (!logged_open_err) {
                flash_log("opendir('%s') failed: errno=%d (%s)",
                          parent_dir, obs.open_errno,
                          strerror(obs.open_errno));
                logged_open_err = true;
            }
        }

        if (obs.exists != prev_existed) {
            flash_log("parent='%s' state %s->%s",
                      parent_dir,
                      prev_existed ? "exists" : "missing",
                      obs.exists   ? "exists" : "missing");
            prev_existed = obs.exists;
        }
        if (obs.exists && obs.entry_count != prev_entries) {
            char snap[512];
            size_t off = 0u;
            snap[0] = '\0';
            off = parent_obs_append(snap, sizeof(snap), off,
                                    parent_dir, &obs);
            (void)off;
            flash_log("parent entry-count %zu->%zu%s",
                      prev_entries, obs.entry_count, snap);
            prev_entries = obs.entry_count;
        }

        const uint64_t now_ms = monotonic_ms();
        if (now_ms >= next_heartbeat_ms) {
            flash_log("wait_reenumeration: still polling, elapsed=%llu ms iter=%llu",
                      (unsigned long long)(now_ms - start_ms),
                      (unsigned long long)iters);
            next_heartbeat_ms += SC_FLASH_BOOTSEL_HEARTBEAT_MS;
        }
        if (now_ms >= deadline_ms) {
            char summary[768];
            size_t off = (size_t)snprintf(summary, sizeof(summary),
                "no by-id entry matching uid=%s after %u ms (iters=%llu)",
                uid_hex, (unsigned)timeout_ms, (unsigned long long)iters);
            off = parent_obs_append(summary, sizeof(summary), off,
                                    parent_dir, &obs);
            write_error(error_buf, error_size, "%s", summary);
            flash_log("REENUM_TIMEOUT - %s", summary);
            return SC_FLASH_ERR_REENUM_TIMEOUT;
        }
        const uint64_t remaining = deadline_ms - now_ms;
        sleep_ms((uint32_t)((remaining < SC_FLASH_BOOTSEL_POLL_INTERVAL_MS)
                                ? remaining
                                : SC_FLASH_BOOTSEL_POLL_INTERVAL_MS));
    }
#else
    (void)timeout_ms;
    (void)out_path;
    (void)out_path_size;
    write_error(error_buf, error_size,
                "re-enumeration waiter not implemented on this platform");
    return SC_FLASH_ERR_NOT_IMPLEMENTED;
#endif
}

sc_flash_status_t sc_flash_wait_reenumeration(const char *uid_hex,
                                              uint32_t timeout_ms,
                                              char *out_path,
                                              size_t out_path_size,
                                              char *error_buf,
                                              size_t error_size)
{
#if SC_FLASH_HAVE_POSIX_WATCHER
    return sc_flash__wait_reenumeration_in(SC_FLASH_REENUM_PARENT_DIR,
                                           uid_hex, timeout_ms,
                                           out_path, out_path_size,
                                           error_buf, error_size);
#else
    (void)uid_hex;
    (void)timeout_ms;
    (void)out_path;
    (void)out_path_size;
    write_error(error_buf, error_size,
                "re-enumeration waiter not implemented on this platform");
    return SC_FLASH_ERR_NOT_IMPLEMENTED;
#endif
}

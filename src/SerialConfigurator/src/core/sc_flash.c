#include "sc_flash.h"

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
 * The flash flow has several layers (auth → reboot → watch BOOTSEL →
 * copy → wait re-enum), each of which can fail with a generic
 * "timeout" / "not found" code. When that happens the operator needs
 * to know *what the watcher actually saw on disk*, not just the code,
 * so the diagnostics below print a `[sc_flash]` trace to stderr.
 *
 * - `flash_log` is always-on. Used for one-time, high-signal events:
 *   the resolved $USER, the parent paths being polled, the first
 *   `opendir()` errno per parent, periodic heartbeats, and the
 *   timeout snapshot. Off-the-record under normal happy-path runs.
 * - `flash_log_v` is gated by `SC_FLASH_DEBUG=1` and is for the
 *   per-iteration noise (every readdir entry, every poll). Operators
 *   reproducing a bug on a foreign distro can flip the env var to
 *   capture every step. */
static bool flash_log_verbose_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("SC_FLASH_DEBUG");
        cached = (v != NULL && v[0] != '\0' && v[0] != '0') ? 1 : 0;
    }
    return cached != 0;
}

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

static void flash_log_v(const char *fmt, ...)
{
    if (!flash_log_verbose_enabled()) {
        return;
    }
    va_list ap;
    (void)fputs("[sc_flash] ", stderr);
    va_start(ap, fmt);
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);
    (void)fputc('\n', stderr);
    (void)fflush(stderr);
}

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
        (void)snprintf(o->first_entries[o->captured],
                       sizeof(o->first_entries[0]), "%s", name);
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

#endif /* SC_FLASH_HAVE_POSIX_WATCHER */

sc_flash_status_t sc_flash__watch_for_bootsel_in(
    const char *const *parent_dirs, size_t parent_count,
    uint32_t timeout_ms,
    char *out_path, size_t out_path_size,
    char *error_buf, size_t error_size)
{
    if (parent_dirs == NULL || parent_count == 0u) {
        write_error(error_buf, error_size, "no parent dirs");
        flash_log("watch_for_bootsel: rejected — no parent dirs");
        return SC_FLASH_ERR_NULL_ARG;
    }
    if (out_path != NULL && out_path_size > 0u) {
        out_path[0] = '\0';
    }

#if SC_FLASH_HAVE_POSIX_WATCHER
    /* Cap parents at a small static limit; the production caller passes
     * 2 (`/media/$USER`, `/run/media/$USER`) and tests pass 1 or 2. */
#  define SC_FLASH_BOOTSEL_PARENTS_MAX 4u
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
    uint64_t next_heartbeat_ms = start_ms + 5000u;
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
            /* Always-on log when the parent transitions empty→non-empty
             * (e.g. udisks2 finally mounted something) and when entries
             * appear/disappear, since either is a clear signal for the
             * operator that something on disk changed. */
            if (obs[i].exists != prev_existed[i]) {
                flash_log("parent[%zu]='%s' state %s→%s",
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
                flash_log("parent[%zu] entry-count %zu→%zu%s",
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
            next_heartbeat_ms += 5000u;
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
            flash_log("BOOTSEL_TIMEOUT — %s", summary);
            flash_log("hint: confirm the BOOTSEL drive auto-mounted under one of the parents above; "
                      "if the drive shows up elsewhere (e.g. /mnt/...) the watcher will not see it");
            return SC_FLASH_ERR_BOOTSEL_TIMEOUT;
        }
        const uint64_t remaining = deadline_ms - now_ms;
        sleep_ms((uint32_t)((remaining < 100u) ? remaining : 100u));
    }
#  undef SC_FLASH_BOOTSEL_PARENTS_MAX
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
    const char *source = "USER";
    const char *user = getenv("USER");
    if (user == NULL || user[0] == '\0') {
        user = getenv("LOGNAME");
        source = "LOGNAME";
    }
    if (user == NULL || user[0] == '\0') {
        write_error(error_buf, error_size,
                    "USER/LOGNAME unset — cannot resolve standard mount roots");
        flash_log("watch_for_bootsel: rejected — USER and LOGNAME unset; "
                  "the GUI/CLI was launched without a user-session env "
                  "(check `env | grep -E '^(USER|LOGNAME)='`)");
        return SC_FLASH_ERR_NULL_ARG;
    }
    flash_log("watch_for_bootsel: resolved %s='%s'", source, user);

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

/* ── Phase 6.4 — UF2 copy with progress + re-enumeration waiter ─── */

#define SC_FLASH_COPY_CHUNK_BYTES (64u * 1024u)
#define SC_FLASH_COPY_DEST_FILENAME "firmware.uf2"

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
        flash_log("wait_reenumeration: rejected — parent_dir=%s uid_hex=%s",
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
    uint64_t next_heartbeat_ms = start_ms + 5000u;
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
                    (void)snprintf(matched_name, sizeof(matched_name), "%s",
                                   name);
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
            flash_log("parent='%s' state %s→%s",
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
            flash_log("parent entry-count %zu→%zu%s",
                      prev_entries, obs.entry_count, snap);
            prev_entries = obs.entry_count;
        }

        const uint64_t now_ms = monotonic_ms();
        if (now_ms >= next_heartbeat_ms) {
            flash_log("wait_reenumeration: still polling, elapsed=%llu ms iter=%llu",
                      (unsigned long long)(now_ms - start_ms),
                      (unsigned long long)iters);
            next_heartbeat_ms += 5000u;
        }
        if (now_ms >= deadline_ms) {
            char summary[768];
            size_t off = (size_t)snprintf(summary, sizeof(summary),
                "no by-id entry matching uid=%s after %u ms (iters=%llu)",
                uid_hex, (unsigned)timeout_ms, (unsigned long long)iters);
            off = parent_obs_append(summary, sizeof(summary), off,
                                    parent_dir, &obs);
            write_error(error_buf, error_size, "%s", summary);
            flash_log("REENUM_TIMEOUT — %s", summary);
            return SC_FLASH_ERR_REENUM_TIMEOUT;
        }
        const uint64_t remaining = deadline_ms - now_ms;
        sleep_ms((uint32_t)((remaining < 100u) ? remaining : 100u));
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
    return sc_flash__wait_reenumeration_in("/dev/serial/by-id",
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

#include "sc_transport.h"
#include "../config.h"

#include "sc_frame.h"
#include "sc_protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ── Diagnostic logging ──────────────────────────────────────────────
 * Mirrors the [sc_flash] tracer in sc_flash.c so the operator can see
 * the auth -> reboot -> bootsel chain end-to-end without rebuilding.
 *
 * - `transport_log` is always-on, used for one-time/high-signal events:
 *   open(), close(), cache hit/miss/invalidate, retry-loop attempts.
 * - `transport_log_v` prints noisy per-frame trace (every send/recv
 *   chunk) and is gated by the compile-time `SC_DEBUG_DEEP` macro
 *   defined in src/config.h. */

static void transport_log(const char *fmt, ...)
{
    va_list ap;
    (void)fputs("[sc_transport] ", stderr);
    va_start(ap, fmt);
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);
    (void)fputc('\n', stderr);
    (void)fflush(stderr);
}

#ifdef SC_DEBUG_DEEP
static void transport_log_v(const char *fmt, ...)
{
    va_list ap;
    (void)fputs("[sc_transport] ", stderr);
    va_start(ap, fmt);
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);
    (void)fputc('\n', stderr);
    (void)fflush(stderr);
}
#else
#  define transport_log_v(...) ((void)0)
#endif

typedef struct ScCachedPortEntry {
    bool in_use;
    int fd;
    uint16_t next_seq;
    char device_path[SC_TRANSPORT_PATH_MAX];
} ScCachedPortEntry;

static ScCachedPortEntry s_cached_ports[SC_TRANSPORT_MAX_CACHED_PORTS];

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == 0 || dst_size == 0u) {
        return;
    }

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    (void)snprintf(dst, dst_size, "%s", src);
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error == 0 || error_size == 0u) {
        return;
    }

    copy_string(error, error_size, message);
}

static bool configure_serial_port(int fd, char *error, size_t error_size)
{
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        (void)snprintf(error, error_size, "tcgetattr failed: %s", strerror(errno));
        return false;
    }

    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    (void)cfsetispeed(&tty, B115200);
    (void)cfsetospeed(&tty, B115200);

    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~HUPCL;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        (void)snprintf(error, error_size, "tcsetattr failed: %s", strerror(errno));
        return false;
    }

    if (tcflush(fd, TCIOFLUSH) != 0) {
        (void)snprintf(error, error_size, "tcflush failed: %s", strerror(errno));
        return false;
    }

    return true;
}

static bool write_all(int fd, const char *data, size_t len, char *error, size_t error_size)
{
    if (data == 0) {
        set_error(error, error_size, "internal error: write buffer is NULL");
        return false;
    }

    size_t written = 0u;
    while (written < len) {
        const ssize_t rc = write(fd, data + written, len - written);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }

            (void)snprintf(error, error_size, "write failed: %s", strerror(errno));
            return false;
        }

        if (rc == 0) {
            set_error(error, error_size, "write failed: no bytes written");
            return false;
        }

        written += (size_t)rc;
    }

    return true;
}

static const char *trim_leading_spaces(const char *line)
{
    if (line == NULL) {
        return NULL;
    }
    while (*line == ' ' || *line == '\t') {
        ++line;
    }
    return line;
}

static bool candidate_is_out_of_scope(const char *path)
{
    if (path == NULL) {
        return false;
    }
    /* Adjustometer is permanently out of scope for SerialConfigurator's
     * framed protocol by project policy. Skip it at candidate enumeration
     * time so we don't emit expected HELLO timeouts for that device. */
    return strstr(path, "Fiesta_Adjustometer") != NULL;
}

/**
 * Read framed `$SC,...*crc` lines from @p fd until either:
 *   - one decodes successfully AND its sequence number equals @p expected_seq
 *     (in which case its inner payload is copied into @p payload_out and
 *     true is returned), or
 *   - the deadline expires (false + timeout error).
 *
 * Lines that don't start with `$SC,` (e.g. stray debug prints) are silently
 * dropped, as are frames with bad CRC or wrong seq. This is the strict
 * begins-with parser that replaces the previous substring-based extractors.
 */
static bool read_framed_response_with_deadline(
    int fd,
    uint16_t expected_seq,
    int total_timeout_ms,
    char *payload_out,
    size_t payload_out_size,
    char *error,
    size_t error_size
)
{
    if (payload_out == NULL || payload_out_size == 0u) {
        set_error(error, error_size, "internal error: invalid response buffer");
        return false;
    }

    struct timespec t_start;
    if (clock_gettime(CLOCK_MONOTONIC, &t_start) != 0) {
        (void)snprintf(error, error_size, "clock_gettime failed: %s", strerror(errno));
        return false;
    }

    payload_out[0] = '\0';
    if (error != NULL && error_size > 0u) {
        error[0] = '\0';
    }

    char line[SC_FRAME_LINE_MAX];
    size_t used = 0u;
    bool line_overflow = false;
    line[0] = '\0';

    size_t bytes_seen = 0u;
    size_t lines_seen = 0u;
    size_t non_sc_lines = 0u;
    size_t bad_sc_lines = 0u;
    size_t wrong_seq_frames = 0u;
    size_t repaired_missing_dollar = 0u;
    size_t overflow_lines = 0u;
    uint16_t first_wrong_seq = 0u;
    char first_non_sc_line[96];
    char first_bad_sc_line[96];
    first_non_sc_line[0] = '\0';
    first_bad_sc_line[0] = '\0';

    /* Fail-fast deadline. When the firmware has clearly already
     * answered (we saw a `$SC,<seq>,...` line for our seq, but its CRC
     * did not validate - typically a single-byte CDC drop in the
     * middle), waiting the full `total_timeout_ms` for a redo just
     * burns wall-clock; the firmware does NOT retransmit the same
     * frame on its own. Cap the wait to a short grace window so the
     * caller can reopen the port and retry sooner. -1 means "no
     * fail-fast active". */
    long fail_fast_deadline_ms = -1;
    static const long FAIL_FAST_GRACE_MS = 60;

    while (1) {
        struct timespec t_now;
        if (clock_gettime(CLOCK_MONOTONIC, &t_now) != 0) {
            (void)snprintf(error, error_size, "clock_gettime failed: %s", strerror(errno));
            return false;
        }

        const long elapsed_ms =
            (t_now.tv_sec - t_start.tv_sec) * 1000L +
            (t_now.tv_nsec - t_start.tv_nsec) / 1000000L;
        if (elapsed_ms >= total_timeout_ms) {
            break;
        }
        if (fail_fast_deadline_ms >= 0 && elapsed_ms >= fail_fast_deadline_ms) {
            break;
        }

        long left_ms_long = total_timeout_ms - elapsed_ms;
        if (fail_fast_deadline_ms >= 0) {
            const long left_ff = fail_fast_deadline_ms - elapsed_ms;
            if (left_ff < left_ms_long) left_ms_long = left_ff;
        }
        const int left_ms = (int)left_ms_long;
        const int wait_ms = left_ms > 100 ? 100 : left_ms;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = wait_ms / 1000;
        timeout.tv_usec = (wait_ms % 1000) * 1000;

        const int ready = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            (void)snprintf(error, error_size, "select failed: %s", strerror(errno));
            return false;
        }
        if (ready == 0) {
            continue;
        }

        char chunk[64];
        const ssize_t received = read(fd, chunk, sizeof(chunk));
        if (received < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            (void)snprintf(error, error_size, "read failed: %s", strerror(errno));
            return false;
        }
        if (received == 0) {
            continue;
        }

        bytes_seen += (size_t)received;
        for (ssize_t i = 0; i < received; ++i) {
            const char c = chunk[i];
            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                lines_seen++;
                if (line_overflow) {
                    overflow_lines++;
                } else if (used > 0u) {
                    line[used] = '\0';
                    const char *trimmed = trim_leading_spaces(line);
                    const char *framed = NULL;
                    bool repaired_prefix = false;
                    char repaired_line[SC_FRAME_LINE_MAX + 2u];
                    /* Strict prefix match - only true frames are even
                     * considered (this replaces the previous strstr-based
                     * substring search that was vulnerable to false matches
                     * inside debug log lines). */
                    if (strncmp(trimmed, SC_FRAME_PREFIX, SC_FRAME_PREFIX_LEN) == 0) {
                        framed = trimmed;
                    } else if (strncmp(trimmed, "SC,", 3u) == 0) {
                        /* Field logs from real hardware occasionally show a
                         * one-byte drop at frame start (`SC,...` instead of
                         * `$SC,...`). Recover by re-prepending `$` and then
                         * validating the frame normally (CRC + seq). */
                        const size_t tl = strlen(trimmed);
                        if (tl + 2u <= sizeof(repaired_line)) {
                            repaired_line[0] = '$';
                            (void)snprintf(repaired_line + 1u,
                                           sizeof(repaired_line) - 1u,
                                           "%s", trimmed);
                            framed = repaired_line;
                            repaired_prefix = true;
                        }
                    }

                    if (framed != NULL) {
                        uint16_t got_seq = 0u;
                        char inner[SC_TRANSPORT_RESPONSE_MAX];
                        if (sc_frame_decode(framed, &got_seq,
                                            inner, sizeof(inner))) {
                            if (repaired_prefix) {
                                repaired_missing_dollar++;
                                transport_log("frame repaired (missing '$') seq=%u",
                                              (unsigned)got_seq);
                            }
                            if (got_seq == expected_seq) {
                                (void)snprintf(payload_out,
                                               payload_out_size,
                                               "%s", inner);
                                return true;
                            }
                            /* Stale/late reply for a previous request ->
                             * keep waiting. */
                            wrong_seq_frames++;
                            if (first_wrong_seq == 0u) {
                                first_wrong_seq = got_seq;
                            }
                            transport_log_v(
                                "drop frame: wrong seq got=%u want=%u line='%.80s'",
                                (unsigned)got_seq, (unsigned)expected_seq, framed);
                        } else {
                            bad_sc_lines++;
                            if (first_bad_sc_line[0] == '\0') {
                                (void)snprintf(first_bad_sc_line,
                                               sizeof(first_bad_sc_line),
                                               "%.80s", framed);
                            }
                            transport_log_v("drop frame: decode failed line='%.80s'",
                                            framed);
                            /* Arm fail-fast on the first corrupt frame.
                             * Subsequent corruptions only ever shrink
                             * the window further (never extend it). */
                            if (fail_fast_deadline_ms < 0) {
                                struct timespec t_ff;
                                if (clock_gettime(CLOCK_MONOTONIC, &t_ff) == 0) {
                                    const long ff_elapsed =
                                        (t_ff.tv_sec - t_start.tv_sec) * 1000L +
                                        (t_ff.tv_nsec - t_start.tv_nsec) / 1000000L;
                                    fail_fast_deadline_ms =
                                        ff_elapsed + FAIL_FAST_GRACE_MS;
                                }
                            }
                        }
                    } else {
                        non_sc_lines++;
                        if (first_non_sc_line[0] == '\0') {
                            (void)snprintf(first_non_sc_line,
                                           sizeof(first_non_sc_line),
                                           "%.80s", trimmed);
                        }
                        transport_log_v("drop line: non-SC '%.80s'", trimmed);
                    }
                }

                used = 0u;
                line_overflow = false;
                line[0] = '\0';
                continue;
            }

            if (line_overflow) {
                continue;
            }

            if (used + 1u < sizeof(line)) {
                line[used++] = c;
            } else {
                line_overflow = true;
            }
        }
    }

    if (first_non_sc_line[0] != '\0') {
        transport_log("timeout diag seq=%u: first non-SC line='%.80s'",
                      (unsigned)expected_seq, first_non_sc_line);
    }
    if (first_bad_sc_line[0] != '\0') {
        transport_log("timeout diag seq=%u: first bad SC frame='%.80s'",
                      (unsigned)expected_seq, first_bad_sc_line);
    }
    (void)snprintf(
        error, error_size,
        "timeout waiting for framed response (seq=%u, bytes=%zu, lines=%zu, non_sc=%zu, bad_sc=%zu, wrong_seq=%zu, repaired=%zu, overflow=%zu, first_wrong_seq=%u)",
        (unsigned)expected_seq,
        bytes_seen,
        lines_seen,
        non_sc_lines,
        bad_sc_lines,
        wrong_seq_frames,
        repaired_missing_dollar,
        overflow_lines,
        (unsigned)first_wrong_seq
    );
    return false;
}

static bool open_and_configure_port(const char *device_path, int *fd, char *error, size_t error_size)
{
    if (device_path == 0 || fd == 0) {
        set_error(error, error_size, "internal error: invalid arguments");
        transport_log("open: rejected - null device_path or fd pointer");
        return false;
    }

    *fd = open(device_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (*fd < 0) {
        const int e = errno;
        (void)snprintf(
            error,
            error_size,
            "open(%.120s) failed: %.96s",
            device_path,
            strerror(e)
        );
        transport_log("open('%s') failed: errno=%d (%s)",
                      device_path, e, strerror(e));
        return false;
    }
    transport_log("open('%s') ok: fd=%d", device_path, *fd);

    if (!configure_serial_port(*fd, error, error_size)) {
        transport_log("configure_serial_port(fd=%d) failed: %s",
                      *fd, error ? error : "(no buf)");
        (void)close(*fd);
        *fd = -1;
        return false;
    }

    struct timeval settle_tv;
    settle_tv.tv_sec = 0;
    settle_tv.tv_usec = SC_TRANSPORT_OPEN_SETTLE_USEC;
    (void)select(0, 0, 0, 0, &settle_tv);
    (void)tcflush(*fd, TCIFLUSH);
    return true;
}

static bool response_has_prefix(const char *response, const char *prefix)
{
    if (response == 0 || prefix == 0) {
        return false;
    }

    return strncmp(response, prefix, strlen(prefix)) == 0;
}

static void close_cached_port_slot(size_t slot)
{
    if (slot >= SC_TRANSPORT_MAX_CACHED_PORTS) {
        return;
    }

    if (s_cached_ports[slot].in_use && s_cached_ports[slot].fd >= 0) {
        transport_log("cache slot=%zu close fd=%d path='%s'",
                      slot, s_cached_ports[slot].fd,
                      s_cached_ports[slot].device_path);
        (void)close(s_cached_ports[slot].fd);
    }

    s_cached_ports[slot].in_use = false;
    s_cached_ports[slot].fd = -1;
    s_cached_ports[slot].device_path[0] = '\0';
}

/**
 * Reconcile cached ports against the current candidate list: close any
 * slot whose device path is no longer present (module unplugged or
 * renumbered), keep the rest. This preserves warm-cache fast-path
 * detection across repeated `list_candidates()` calls without leaking
 * stale fds for genuinely-disconnected devices.
 */
static void reconcile_cached_ports_with_list(const ScTransportCandidateList *list)
{
    if (list == NULL) {
        return;
    }
    for (size_t i = 0u; i < SC_TRANSPORT_MAX_CACHED_PORTS; ++i) {
        if (!s_cached_ports[i].in_use) {
            continue;
        }
        bool still_present = false;
        for (size_t j = 0u; j < list->count; ++j) {
            if (strcmp(s_cached_ports[i].device_path, list->paths[j]) == 0) {
                still_present = true;
                break;
            }
        }
        if (!still_present) {
            transport_log("reconcile: dropping cached slot=%zu fd=%d "
                          "path='%s' (no longer in candidate list - "
                          "device unplugged or by-id symlink gone)",
                          i, s_cached_ports[i].fd,
                          s_cached_ports[i].device_path);
            close_cached_port_slot(i);
        }
    }
}

static int find_cached_port_slot(const char *device_path)
{
    if (device_path == 0 || device_path[0] == '\0') {
        return -1;
    }

    for (size_t i = 0u; i < SC_TRANSPORT_MAX_CACHED_PORTS; ++i) {
        if (!s_cached_ports[i].in_use) {
            continue;
        }

        if (strcmp(s_cached_ports[i].device_path, device_path) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static int find_free_cached_port_slot(void)
{
    for (size_t i = 0u; i < SC_TRANSPORT_MAX_CACHED_PORTS; ++i) {
        if (!s_cached_ports[i].in_use) {
            return (int)i;
        }
    }

    return -1;
}

static void invalidate_cached_port(const char *device_path)
{
    const int slot = find_cached_port_slot(device_path);
    if (slot >= 0) {
        transport_log("invalidate path='%s' (slot=%d)", device_path, slot);
        close_cached_port_slot((size_t)slot);
    } else {
        transport_log_v("invalidate path='%s' - no cached slot", device_path);
    }
}

static bool acquire_cached_port(
    const char *device_path,
    int *fd,
    size_t *slot_out,
    char *error,
    size_t error_size
)
{
    if (device_path == NULL || fd == NULL) {
        set_error(error, error_size, "internal error: invalid cached-port arguments");
        return false;
    }

    int slot = find_cached_port_slot(device_path);
    if (slot >= 0) {
        if (s_cached_ports[slot].fd >= 0) {
            transport_log_v("cache hit slot=%d fd=%d path='%s'",
                            slot, s_cached_ports[slot].fd, device_path);
            *fd = s_cached_ports[slot].fd;
            if (slot_out != NULL) {
                *slot_out = (size_t)slot;
            }
            return true;
        }

        transport_log("cache slot=%d stale (fd<0) for path='%s' - reopening",
                      slot, device_path);
        close_cached_port_slot((size_t)slot);
    }

    int opened_fd = -1;
    if (!open_and_configure_port(device_path, &opened_fd, error, error_size)) {
        return false;
    }

    slot = find_free_cached_port_slot();
    if (slot < 0) {
        transport_log("cache full - evicting slot 0 path='%s'",
                      s_cached_ports[0].device_path);
        close_cached_port_slot(0u);
        slot = 0;
    }

    s_cached_ports[slot].in_use = true;
    s_cached_ports[slot].fd = opened_fd;
    s_cached_ports[slot].next_seq = 1u;
    copy_string(
        s_cached_ports[slot].device_path,
        sizeof(s_cached_ports[slot].device_path),
        device_path
    );
    transport_log("cache install slot=%d fd=%d path='%s'",
                  slot, opened_fd, device_path);
    *fd = opened_fd;
    if (slot_out != NULL) {
        *slot_out = (size_t)slot;
    }
    return true;
}

static uint16_t allocate_next_seq(size_t slot)
{
    if (slot >= SC_TRANSPORT_MAX_CACHED_PORTS) {
        return 1u;
    }
    uint16_t seq = s_cached_ports[slot].next_seq;
    if (seq == 0u) {
        seq = 1u;
    }
    /* Wrap from 0xFFFF back to 1 (skip 0 to keep "0" reserved as "unset"). */
    s_cached_ports[slot].next_seq = (uint16_t)((seq == 0xFFFFu) ? 1u : (seq + 1u));
    return seq;
}

/**
 * Send a single framed request and wait for the matching framed response.
 */
static bool framed_exchange_on_fd(
    int fd,
    uint16_t seq,
    const char *inner,
    int timeout_ms,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
)
{
    char framed_line[SC_FRAME_LINE_MAX + 2u];
    size_t framed_len = 0u;
    if (!sc_frame_encode(seq, inner, framed_line, sizeof(framed_line) - 1u,
                         &framed_len)) {
        set_error(error, error_size, "failed to encode SC frame");
        return false;
    }
    framed_line[framed_len] = '\n';
    framed_line[framed_len + 1u] = '\0';

    (void)tcflush(fd, TCIFLUSH);

    if (!write_all(fd, framed_line, framed_len + 1u, error, error_size)) {
        return false;
    }

    return read_framed_response_with_deadline(
        fd, seq, timeout_ms, response, response_size, error, error_size);
}

static bool send_hello_bootstrap_on_fd(
    int fd,
    size_t cache_slot,
    int timeout_ms,
    char *error,
    size_t error_size
)
{
    char hello_response[SC_TRANSPORT_RESPONSE_MAX];
    hello_response[0] = '\0';

    const uint16_t seq = allocate_next_seq(cache_slot);
    if (!framed_exchange_on_fd(fd, seq, "HELLO", timeout_ms,
                               hello_response, sizeof(hello_response),
                               error, error_size)) {
        return false;
    }

    if (!response_has_prefix(hello_response, SC_REPLY_HELLO_HEAD)) {
        (void)snprintf(
            error,
            error_size,
            "HELLO bootstrap rejected: %.180s",
            hello_response
        );
        return false;
    }

    return true;
}

static bool send_sc_command_on_fd(
    int fd,
    size_t cache_slot,
    const char *command_inner,
    int timeout_ms,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
)
{
    const uint16_t seq = allocate_next_seq(cache_slot);
    return framed_exchange_on_fd(fd, seq, command_inner, timeout_ms,
                                 response, response_size, error, error_size);
}

static bool default_list_candidates(
    void *context,
    ScTransportCandidateList *list,
    char *error,
    size_t error_size
)
{
    (void)context;
    if (list == 0) {
        set_error(error, error_size, "internal error: candidate list is NULL");
        return false;
    }

    /* Do NOT blow away the cache here. Detection runs are expected to be
     * idempotent (Detect button can be pressed repeatedly), and re-opening
     * every port costs ~100 ms of CDC settle time per module. The cache is
     * reconciled below against the fresh glob output so genuinely-removed
     * devices still get their fds closed. */

    list->count = 0u;
    list->truncated = false;
    for (size_t i = 0u; i < SC_TRANSPORT_MAX_CANDIDATES; ++i) {
        list->paths[i][0] = '\0';
    }

    glob_t devices;
    memset(&devices, 0, sizeof(devices));

    const int glob_rc = glob(SC_TRANSPORT_GLOB_PATTERN, 0, 0, &devices);
    if (glob_rc == GLOB_NOMATCH) {
        globfree(&devices);
        return true;
    }

    if (glob_rc != 0) {
        (void)snprintf(error, error_size, "device scan failed (glob rc=%d)", glob_rc);
        globfree(&devices);
        return false;
    }

    for (size_t i = 0u; i < devices.gl_pathc; ++i) {
        if (candidate_is_out_of_scope(devices.gl_pathv[i])) {
            transport_log("candidate skip path='%s': Adjustometer is out-of-scope",
                          devices.gl_pathv[i]);
            continue;
        }
        if (list->count >= SC_TRANSPORT_MAX_CANDIDATES) {
            list->truncated = true;
            break;
        }

        copy_string(
            list->paths[list->count],
            sizeof(list->paths[list->count]),
            devices.gl_pathv[i]
        );
        list->count++;
    }

    globfree(&devices);
    reconcile_cached_ports_with_list(list);
    return true;
}

static bool default_resolve_device_path(
    void *context,
    const char *candidate_path,
    char *device_path,
    size_t device_path_size,
    char *error,
    size_t error_size
)
{
    (void)context;
    (void)error;
    (void)error_size;

    if (candidate_path == 0 || device_path == 0 || device_path_size == 0u) {
        set_error(error, error_size, "internal error: invalid path arguments");
        return false;
    }

    copy_string(device_path, device_path_size, candidate_path);
    return true;
}

static bool default_send_hello(
    void *context,
    const char *device_path,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
)
{
    (void)context;
    if (device_path == NULL || response == NULL || response_size == 0u) {
        set_error(error, error_size, "internal error: invalid HELLO arguments");
        return false;
    }

    char last_error[256];
    last_error[0] = '\0';

    transport_log_v("HELLO begin path='%s'", device_path);
    for (int attempt = 0; attempt < SC_TRANSPORT_HELLO_ATTEMPTS; ++attempt) {
        int fd = -1;
        size_t slot = 0u;
        bool success = false;

        do {
            if (!acquire_cached_port(device_path, &fd, &slot,
                                     last_error, sizeof(last_error))) {
                break;
            }

            const int timeout_ms = (attempt == 0)
                ? SC_TRANSPORT_PRIMARY_TIMEOUT_MS
                : SC_TRANSPORT_RETRY_TIMEOUT_MS;

            const uint16_t seq = allocate_next_seq(slot);
            if (!framed_exchange_on_fd(fd, seq, "HELLO", timeout_ms,
                                       response, response_size,
                                       last_error, sizeof(last_error))) {
                break;
            }

            success = true;
        } while (0);

        if (success) {
            transport_log("HELLO ok path='%s' attempt=%d reply='%.80s'",
                          device_path, attempt + 1, response);
            return true;
        }

        transport_log("HELLO attempt %d/%d failed path='%s': %s",
                      attempt + 1, SC_TRANSPORT_HELLO_ATTEMPTS,
                      device_path, last_error);
        invalidate_cached_port(device_path);

        if (attempt + 1 < SC_TRANSPORT_HELLO_ATTEMPTS) {
            struct timeval retry_pause = { .tv_sec = 0, .tv_usec = SC_TRANSPORT_RETRY_PAUSE_USEC };
            (void)select(0, NULL, NULL, NULL, &retry_pause);
        }
    }

    transport_log("HELLO exhausted path='%s': %s", device_path, last_error);
    copy_string(error, error_size, last_error);
    return false;
}

static bool default_send_sc_command(
    void *context,
    const char *device_path,
    const char *command,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
)
{
    (void)context;
    if (device_path == NULL || command == NULL ||
        response == NULL || response_size == 0u) {
        set_error(error, error_size, "internal error: invalid SC command arguments");
        return false;
    }

    /* Strip a trailing newline if the caller supplied one - framing adds its
     * own terminator and disallows raw '\n' inside the payload. */
    char inner[SC_FRAME_PAYLOAD_MAX];
    size_t cmd_len = strlen(command);
    while (cmd_len > 0u && (command[cmd_len - 1u] == '\n' ||
                            command[cmd_len - 1u] == '\r')) {
        --cmd_len;
    }
    if (cmd_len == 0u) {
        set_error(error, error_size, "SC command cannot be empty");
        return false;
    }
    if (cmd_len + 1u > sizeof(inner)) {
        set_error(error, error_size, "SC command is too long");
        return false;
    }
    memcpy(inner, command, cmd_len);
    inner[cmd_len] = '\0';

    char last_error[256];
    last_error[0] = '\0';

    transport_log_v("SC '%s' begin path='%s'", inner, device_path);
    for (int attempt = 0; attempt < SC_TRANSPORT_SC_ATTEMPTS; ++attempt) {
        int fd = -1;
        size_t slot = 0u;
        bool success = false;
        const int timeout_ms = (attempt == 0)
            ? SC_TRANSPORT_PRIMARY_TIMEOUT_MS
            : SC_TRANSPORT_RETRY_TIMEOUT_MS;

        do {
            if (!acquire_cached_port(device_path, &fd, &slot,
                                     last_error, sizeof(last_error))) {
                break;
            }

            if (!send_sc_command_on_fd(fd, slot, inner, timeout_ms,
                                       response, response_size,
                                       last_error, sizeof(last_error))) {
                break;
            }

            if (!response_has_prefix(response, SC_STATUS_NOT_READY)) {
                success = true;
                break;
            }

            transport_log("SC '%s' got NOT_READY - re-handshaking on fd=%d",
                          inner, fd);
            /* Module forgot HELLO (re-enumeration / reset) - re-handshake
             * and retry the same command on the same fd. */
            if (!send_hello_bootstrap_on_fd(fd, slot, timeout_ms,
                                            last_error, sizeof(last_error))) {
                break;
            }

            if (!send_sc_command_on_fd(fd, slot, inner, timeout_ms,
                                       response, response_size,
                                       last_error, sizeof(last_error))) {
                break;
            }

            success = true;
        } while (0);

        if (success) {
            if (attempt > 0) {
                transport_log("SC '%s' recovered path='%s' attempt=%d reply='%.80s'",
                              inner, device_path, attempt + 1, response);
            } else {
                transport_log_v("SC '%s' ok path='%s' attempt=%d reply='%.80s'",
                                inner, device_path, attempt + 1, response);
            }
            return true;
        }

        transport_log("SC '%s' attempt %d/%d failed path='%s': %s",
                      inner, attempt + 1, SC_TRANSPORT_SC_ATTEMPTS,
                      device_path, last_error);
        invalidate_cached_port(device_path);

        if (attempt + 1 < SC_TRANSPORT_SC_ATTEMPTS) {
            struct timeval retry_pause = { .tv_sec = 0, .tv_usec = SC_TRANSPORT_RETRY_PAUSE_USEC };
            (void)select(0, NULL, NULL, NULL, &retry_pause);
        }
    }

    transport_log("SC '%s' exhausted path='%s': %s",
                  inner, device_path, last_error);
    copy_string(error, error_size, last_error);
    return false;
}

static const ScTransportOps k_default_ops = {
    .list_candidates = default_list_candidates,
    .resolve_device_path = default_resolve_device_path,
    .send_hello = default_send_hello,
    .send_sc_command = default_send_sc_command,
};

void sc_transport_init_default(ScTransport *transport)
{
    if (transport == 0) {
        return;
    }

    transport->ops = &k_default_ops;
    transport->context = 0;
}

void sc_transport_init_custom(ScTransport *transport, const ScTransportOps *ops, void *context)
{
    if (transport == 0) {
        return;
    }

    if (ops == 0) {
        sc_transport_init_default(transport);
        return;
    }

    transport->ops = ops;
    transport->context = context;
}

bool sc_transport_list_candidates(
    const ScTransport *transport,
    ScTransportCandidateList *list,
    char *error,
    size_t error_size
)
{
    if (transport == 0 || transport->ops == 0 || transport->ops->list_candidates == 0) {
        set_error(error, error_size, "transport list_candidates operation is unavailable");
        return false;
    }

    return transport->ops->list_candidates(transport->context, list, error, error_size);
}

bool sc_transport_resolve_device_path(
    const ScTransport *transport,
    const char *candidate_path,
    char *device_path,
    size_t device_path_size,
    char *error,
    size_t error_size
)
{
    if (transport == 0 || transport->ops == 0 || transport->ops->resolve_device_path == 0) {
        set_error(error, error_size, "transport resolve_device_path operation is unavailable");
        return false;
    }

    return transport->ops->resolve_device_path(
        transport->context,
        candidate_path,
        device_path,
        device_path_size,
        error,
        error_size
    );
}

bool sc_transport_send_hello(
    const ScTransport *transport,
    const char *device_path,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
)
{
    if (transport == 0 || transport->ops == 0 || transport->ops->send_hello == 0) {
        set_error(error, error_size, "transport send_hello operation is unavailable");
        return false;
    }

    return transport->ops->send_hello(
        transport->context,
        device_path,
        response,
        response_size,
        error,
        error_size
    );
}

bool sc_transport_send_sc_command(
    const ScTransport *transport,
    const char *device_path,
    const char *command,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
)
{
    if (transport == 0 || transport->ops == 0 || transport->ops->send_sc_command == 0) {
        set_error(error, error_size, "transport send_sc_command operation is unavailable");
        return false;
    }

    return transport->ops->send_sc_command(
        transport->context,
        device_path,
        command,
        response,
        response_size,
        error,
        error_size
    );
}

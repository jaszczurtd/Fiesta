#include "sc_core.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdarg.h>

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

typedef struct ScModuleDef {
    const char *token;
    const char *display_name;
} ScModuleDef;

static const ScModuleDef k_module_defs[SC_MODULE_COUNT] = {
    { "ECU", "ECU" },
    { "Clocks", "Clocks" },
    { "OIL&SPD", "OilAndSpeed" },
};

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

static void log_append(char *log_output, size_t log_output_size, const char *format, ...)
{
    if (log_output == 0 || log_output_size == 0u || format == 0) {
        return;
    }

    const size_t current_len = strlen(log_output);
    if (current_len >= log_output_size - 1u) {
        return;
    }

    va_list args;
    va_start(args, format);
    (void)vsnprintf(
        log_output + current_len,
        log_output_size - current_len,
        format,
        args
    );
    va_end(args);
}

static bool extract_module_token(const char *response, char *module, size_t module_size)
{
    if (response == 0 || module == 0 || module_size == 0u) {
        return false;
    }

    const char *start = strstr(response, "module=");
    if (start == 0) {
        module[0] = '\0';
        return false;
    }

    start += strlen("module=");
    size_t pos = 0u;

    while (start[pos] != '\0' && !isspace((unsigned char)start[pos]) && pos + 1u < module_size) {
        module[pos] = start[pos];
        pos++;
    }

    module[pos] = '\0';
    return pos > 0u;
}

static int module_index_from_token(const char *token)
{
    if (token == 0) {
        return -1;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        if (strcmp(token, k_module_defs[i].token) == 0) {
            return (int)i;
        }
    }

    if (strcmp(token, "OilAndSpeed") == 0) {
        return 2;
    }

    return -1;
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
        (void)snprintf(error, error_size, "internal error: write buffer is NULL");
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
            (void)snprintf(error, error_size, "write failed: no bytes written");
            return false;
        }

        written += (size_t)rc;
    }

    return true;
}

static bool read_line_with_timeout(
    int fd,
    char *out,
    size_t out_size,
    int timeout_ms,
    char *error,
    size_t error_size
)
{
    if (out == 0 || out_size == 0u) {
        (void)snprintf(error, error_size, "internal error: output buffer is NULL");
        return false;
    }

    out[0] = '\0';
    size_t used = 0u;
    int remaining_ms = timeout_ms;

    while (remaining_ms > 0) {
        const int wait_ms = remaining_ms > 100 ? 100 : remaining_ms;
        remaining_ms -= wait_ms;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = wait_ms / 1000;
        timeout.tv_usec = (wait_ms % 1000) * 1000;

        const int ready = select(fd + 1, &read_fds, 0, 0, &timeout);
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

        for (ssize_t i = 0; i < received; ++i) {
            const char c = chunk[i];
            if (c == '\n') {
                out[used] = '\0';
                return used > 0u;
            }

            if (c == '\r') {
                continue;
            }

            if (used + 1u < out_size) {
                out[used++] = c;
            }
        }
    }

    out[used] = '\0';
    if (used > 0u) {
        return true;
    }

    (void)snprintf(error, error_size, "timeout waiting for HELLO response");
    return false;
}

static bool send_hello_command(
    const char *device_path,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
)
{
    if (device_path == 0 || response == 0 || response_size == 0u) {
        return false;
    }

    int fd = open(device_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        (void)snprintf(
            error,
            error_size,
            "open(%.120s) failed: %.96s",
            device_path,
            strerror(errno)
        );
        return false;
    }

    bool success = false;
    do {
        if (!configure_serial_port(fd, error, error_size)) {
            break;
        }

        /* Settle: give the device time to recover from any reset triggered by
         * the port-open event (DTR assertion via USB CDC SET_CONTROL_LINE_STATE),
         * then flush stale RX bytes before sending HELLO. */
        struct timeval settle_tv = { .tv_sec = 0, .tv_usec = 500000 }; /* 500 ms */
        (void)select(0, 0, 0, 0, &settle_tv);
        (void)tcflush(fd, TCIFLUSH);

        const char *hello_command = "HELLO\n";
        if (!write_all(fd, hello_command, strlen(hello_command), error, error_size)) {
            break;
        }

        response[0] = '\0';

        /* Use wall-clock deadline so that every debug line received from the
         * device consumes only the time it actually took — not a fixed 250 ms
         * budget.  ECU sends many debug lines on the same USB CDC port; the
         * old budget-decrement approach exhausted the 3 s window on debug
         * traffic before the "OK HELLO" response was ever seen. */
        struct timespec t_start, t_now;
        (void)clock_gettime(CLOCK_MONOTONIC, &t_start);
        const long total_ms = 3000L;

        while (1) {
            (void)clock_gettime(CLOCK_MONOTONIC, &t_now);
            const long elapsed_ms =
                (t_now.tv_sec  - t_start.tv_sec)  * 1000L +
                (t_now.tv_nsec - t_start.tv_nsec) / 1000000L;
            if (elapsed_ms >= total_ms) {
                break;
            }
            const int left_ms = (int)(total_ms - elapsed_ms);
            const int step_ms = left_ms > 250 ? 250 : left_ms;

            char line[SC_HELLO_RESPONSE_MAX];
            char line_error[128];
            line[0] = '\0';
            line_error[0] = '\0';

            if (!read_line_with_timeout(fd, line, sizeof(line), step_ms, line_error, sizeof(line_error))) {
                continue;
            }

            if (strncmp(line, "OK HELLO", 8) == 0 || strncmp(line, "ERR ", 4) == 0) {
                copy_string(response, response_size, line);
                break;
            }
        }

        if (response[0] == '\0') {
            (void)snprintf(error, error_size, "timeout waiting for protocol response");
            break;
        }

        success = true;
    } while (0);

    (void)close(fd);
    return success;
}

static void resolve_device_path(const char *candidate_path, char *resolved, size_t resolved_size)
{
    if (candidate_path == 0 || resolved == 0 || resolved_size == 0u) {
        return;
    }

    copy_string(resolved, resolved_size, candidate_path);
}

static bool all_modules_detected(const ScCore *core)
{
    if (core == 0) {
        return false;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        if (!core->modules[i].detected) {
            return false;
        }
    }

    return true;
}

void sc_core_init(ScCore *core)
{
    if (core == 0) {
        return;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        core->modules[i].display_name = k_module_defs[i].display_name;
    }

    sc_core_reset_detection(core);
}

void sc_core_reset_detection(ScCore *core)
{
    if (core == 0) {
        return;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        core->modules[i].detected = false;
        core->modules[i].port_path[0] = '\0';
        core->modules[i].hello_response[0] = '\0';
    }
}

void sc_core_detect_modules(ScCore *core, char *log_output, size_t log_output_size)
{
    if (log_output != 0 && log_output_size > 0u) {
        log_output[0] = '\0';
    }

    if (core == 0) {
        log_append(log_output, log_output_size, "[ERROR] Core is not initialized.\n");
        return;
    }

    sc_core_reset_detection(core);

    log_append(log_output, log_output_size, "Detecting Fiesta modules with HELLO...\n");

    glob_t devices;
    memset(&devices, 0, sizeof(devices));

    const int glob_rc = glob("/dev/serial/by-id/usb-Jaszczur_Fiesta_*", 0, 0, &devices);
    if (glob_rc == GLOB_NOMATCH) {
        log_append(
            log_output,
            log_output_size,
            "No Fiesta serial devices found in /dev/serial/by-id.\n"
        );
        globfree(&devices);
        return;
    }

    if (glob_rc != 0) {
        log_append(log_output, log_output_size, "Device scan failed (glob rc=%d).\n", glob_rc);
        globfree(&devices);
        return;
    }

    log_append(log_output, log_output_size, "Candidates: %zu\n", devices.gl_pathc);

    for (size_t i = 0u; i < devices.gl_pathc; ++i) {
        const char *candidate_path = devices.gl_pathv[i];
        char device_path[SC_PORT_PATH_MAX];
        resolve_device_path(candidate_path, device_path, sizeof(device_path));

        log_append(
            log_output,
            log_output_size,
            "\n[%zu/%zu] %s -> %s\n",
            i + 1u,
            devices.gl_pathc,
            candidate_path,
            device_path
        );

        char response[SC_HELLO_RESPONSE_MAX];
        char error[256];
        response[0] = '\0';
        error[0] = '\0';

        if (!send_hello_command(device_path, response, sizeof(response), error, sizeof(error))) {
            log_append(log_output, log_output_size, "HELLO failed: %s\n", error);
            continue;
        }

        log_append(log_output, log_output_size, "HELLO response: %s\n", response);

        if (strncmp(response, "OK HELLO", 8) != 0) {
            log_append(log_output, log_output_size, "Ignored: response is not OK HELLO.\n");
            continue;
        }

        char module_token[64];
        if (!extract_module_token(response, module_token, sizeof(module_token))) {
            log_append(log_output, log_output_size, "Ignored: missing module=<name> in response.\n");
            continue;
        }

        const int module_index = module_index_from_token(module_token);
        if (module_index < 0) {
            log_append(
                log_output,
                log_output_size,
                "Ignored: unsupported module token '%s'.\n",
                module_token
            );
            continue;
        }

        ScModuleStatus *status = &core->modules[module_index];
        if (status->detected) {
            log_append(
                log_output,
                log_output_size,
                "Module '%s' already detected earlier. Updating data from latest response.\n",
                status->display_name
            );
        }

        status->detected = true;
        copy_string(status->port_path, sizeof(status->port_path), device_path);
        copy_string(status->hello_response, sizeof(status->hello_response), response);

        log_append(
            log_output,
            log_output_size,
            "Detected module '%s' on %s\n",
            status->display_name,
            device_path
        );

        if (all_modules_detected(core)) {
            log_append(
                log_output,
                log_output_size,
                "All known modules are detected. Stopping scan early.\n"
            );
            break;
        }
    }

    globfree(&devices);

    log_append(log_output, log_output_size, "\nDetection summary:\n");
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *status = &core->modules[i];
        log_append(
            log_output,
            log_output_size,
            "- %-12s : %s\n",
            status->display_name,
            status->detected ? "DETECTED" : "NOT DETECTED"
        );
    }
}

size_t sc_core_module_count(void)
{
    return SC_MODULE_COUNT;
}

const ScModuleStatus *sc_core_module_status(const ScCore *core, size_t index)
{
    if (core == 0 || index >= SC_MODULE_COUNT) {
        return 0;
    }

    return &core->modules[index];
}

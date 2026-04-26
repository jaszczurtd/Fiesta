#include "sc_transport.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define SC_TRANSPORT_GLOB_PATTERN "/dev/serial/by-id/usb-Jaszczur_Fiesta_*"
#define SC_TRANSPORT_TOTAL_TIMEOUT_MS 3000
#define SC_TRANSPORT_HELLO_RETRY_TIMEOUT_MS 6000
#define SC_TRANSPORT_HELLO_ATTEMPTS 2
#define SC_TRANSPORT_SC_ATTEMPTS 2

typedef bool (*ScLineMatchFn)(const char *line);

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

static bool line_is_hello_or_error(const char *line)
{
    if (line == 0) {
        return false;
    }

    return strstr(line, "OK HELLO") != 0 || strncmp(line, "ERR ", 4) == 0;
}

static bool line_is_sc_or_error(const char *line)
{
    if (line == 0) {
        return false;
    }

    return strncmp(line, "SC_", 3) == 0 || strncmp(line, "ERR ", 4) == 0;
}

static bool read_protocol_line_with_deadline(
    int fd,
    int total_timeout_ms,
    ScLineMatchFn matcher,
    const char *timeout_context,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
)
{
    if (matcher == 0 || response == 0 || response_size == 0u) {
        set_error(error, error_size, "internal error: invalid matcher or response buffer");
        return false;
    }

    struct timespec t_start;
    if (clock_gettime(CLOCK_MONOTONIC, &t_start) != 0) {
        (void)snprintf(error, error_size, "clock_gettime failed: %s", strerror(errno));
        return false;
    }

    response[0] = '\0';
    if (error != 0 && error_size > 0u) {
        error[0] = '\0';
    }

    char line[SC_TRANSPORT_RESPONSE_MAX];
    size_t used = 0u;
    bool line_overflow = false;
    line[0] = '\0';

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

        const int left_ms = (int)(total_timeout_ms - elapsed_ms);
        const int wait_ms = left_ms > 100 ? 100 : left_ms;

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
            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                if (!line_overflow && used > 0u) {
                    line[used] = '\0';
                    const char *candidate = line;
                    while (candidate[0] == ' ') {
                        candidate++;
                    }

                    if (matcher(candidate)) {
                        copy_string(response, response_size, candidate);
                        return true;
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

    if (!line_overflow && used > 0u) {
        line[used] = '\0';
        const char *candidate = line;
        while (candidate[0] == ' ') {
            candidate++;
        }

        if (matcher(candidate)) {
            copy_string(response, response_size, candidate);
            return true;
        }
    }

    (void)snprintf(
        error,
        error_size,
        "timeout waiting for %s response",
        timeout_context != 0 ? timeout_context : "protocol"
    );
    return false;
}

static bool open_and_configure_port(const char *device_path, int *fd, char *error, size_t error_size)
{
    if (device_path == 0 || fd == 0) {
        set_error(error, error_size, "internal error: invalid arguments");
        return false;
    }

    *fd = open(device_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (*fd < 0) {
        (void)snprintf(
            error,
            error_size,
            "open(%.120s) failed: %.96s",
            device_path,
            strerror(errno)
        );
        return false;
    }

    if (!configure_serial_port(*fd, error, error_size)) {
        (void)close(*fd);
        *fd = -1;
        return false;
    }

    struct timeval settle_tv;
    settle_tv.tv_sec = 0;
    settle_tv.tv_usec = 500000;
    (void)select(0, 0, 0, 0, &settle_tv);
    (void)tcflush(*fd, TCIFLUSH);
    return true;
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
    if (device_path == 0 || response == 0 || response_size == 0u) {
        set_error(error, error_size, "internal error: invalid HELLO arguments");
        return false;
    }

    char last_error[256];
    last_error[0] = '\0';

    for (int attempt = 0; attempt < SC_TRANSPORT_HELLO_ATTEMPTS; ++attempt) {
        int fd = -1;
        bool success = false;

        do {
            if (!open_and_configure_port(device_path, &fd, last_error, sizeof(last_error))) {
                break;
            }

            const char *hello_command = "HELLO\n";
            if (!write_all(fd, hello_command, strlen(hello_command), last_error, sizeof(last_error))) {
                break;
            }

            const int timeout_ms = (attempt == 0)
                ? SC_TRANSPORT_TOTAL_TIMEOUT_MS
                : SC_TRANSPORT_HELLO_RETRY_TIMEOUT_MS;
            if (!read_protocol_line_with_deadline(
                    fd,
                    timeout_ms,
                    line_is_hello_or_error,
                    "HELLO",
                    response,
                    response_size,
                    last_error,
                    sizeof(last_error)
                )) {
                break;
            }

            success = true;
        } while (0);

        if (fd >= 0) {
            (void)close(fd);
        }

        if (success) {
            return true;
        }

        if (attempt + 1 < SC_TRANSPORT_HELLO_ATTEMPTS) {
            struct timeval retry_pause = { .tv_sec = 0, .tv_usec = 250000 };
            (void)select(0, 0, 0, 0, &retry_pause);
        }
    }

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
    if (device_path == 0 || command == 0 || response == 0 || response_size == 0u) {
        set_error(error, error_size, "internal error: invalid SC command arguments");
        return false;
    }

    const size_t command_len = strlen(command);
    if (command_len == 0u) {
        set_error(error, error_size, "SC command cannot be empty");
        return false;
    }

    char last_error[256];
    last_error[0] = '\0';

    for (int attempt = 0; attempt < SC_TRANSPORT_SC_ATTEMPTS; ++attempt) {
        int fd = -1;
        bool success = false;

        do {
            if (!open_and_configure_port(device_path, &fd, last_error, sizeof(last_error))) {
                break;
            }

            const char *hello_command = "HELLO\n";
            char hello_response[SC_TRANSPORT_RESPONSE_MAX];
            hello_response[0] = '\0';

            if (!write_all(fd, hello_command, strlen(hello_command), last_error, sizeof(last_error))) {
                break;
            }

            const int hello_timeout_ms = (attempt == 0)
                ? SC_TRANSPORT_TOTAL_TIMEOUT_MS
                : SC_TRANSPORT_HELLO_RETRY_TIMEOUT_MS;
            if (!read_protocol_line_with_deadline(
                    fd,
                    hello_timeout_ms,
                    line_is_hello_or_error,
                    "HELLO bootstrap",
                    hello_response,
                    sizeof(hello_response),
                    last_error,
                    sizeof(last_error)
                )) {
                break;
            }

            if (strncmp(hello_response, "OK HELLO", 8) != 0) {
                (void)snprintf(
                    last_error,
                    sizeof(last_error),
                    "HELLO bootstrap rejected: %.180s",
                    hello_response
                );
                break;
            }

            char command_line[SC_TRANSPORT_RESPONSE_MAX];
            if (command[command_len - 1u] == '\n') {
                copy_string(command_line, sizeof(command_line), command);
            } else {
                if (command_len + 2u > sizeof(command_line)) {
                    set_error(last_error, sizeof(last_error), "SC command line is too long");
                    break;
                }

                (void)snprintf(command_line, sizeof(command_line), "%s\n", command);
            }

            if (!write_all(fd, command_line, strlen(command_line), last_error, sizeof(last_error))) {
                break;
            }

            const int command_timeout_ms = (attempt == 0)
                ? SC_TRANSPORT_TOTAL_TIMEOUT_MS
                : SC_TRANSPORT_HELLO_RETRY_TIMEOUT_MS;
            if (!read_protocol_line_with_deadline(
                    fd,
                    command_timeout_ms,
                    line_is_sc_or_error,
                    "SC command",
                    response,
                    response_size,
                    last_error,
                    sizeof(last_error)
                )) {
                break;
            }

            success = true;
        } while (0);

        if (fd >= 0) {
            (void)close(fd);
        }

        if (success) {
            return true;
        }

        if (attempt + 1 < SC_TRANSPORT_SC_ATTEMPTS) {
            struct timeval retry_pause = { .tv_sec = 0, .tv_usec = 250000 };
            (void)select(0, 0, 0, 0, &retry_pause);
        }
    }

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

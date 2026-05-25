/**
 * @file sc_gps.c
 * @brief Host-side SC_GET_GPS transport + reply parser.
 *
 * Kept out of sc_core.c so the core's configuration/auth surface does
 * not grow whenever telemetry fields are added. The wire format is
 * defined by SC_REPLY_GPS_FMT in sc_protocol.h; this file is the only
 * host-side consumer.
 */

#include "sc_gps.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_core.h"
#include "sc_protocol.h"

/* Mirror sc_core.c's tiny error-string helper. Keeping a local copy
 * avoids exposing sc_core internals just to share a 4-line utility. */
static void sc_gps_set_error(char *error, size_t error_size,
                             const char *message)
{
    if (error == 0 || error_size == 0u) {
        return;
    }
    if (message == 0) {
        error[0] = '\0';
        return;
    }
    const size_t len = strlen(message);
    const size_t copy_len = (len < (error_size - 1u)) ? len : (error_size - 1u);
    memcpy(error, message, copy_len);
    error[copy_len] = '\0';
}

/* Whitespace-delimited token walker over `result->details`. Mirrors
 * the lightweight tokenizer sc_core.c uses for PARAM_LIST / PARAM
 * payloads so the GPS reply parser stays self-contained. */
static bool sc_gps_next_token(const char **cursor, char *out, size_t out_size)
{
    if (cursor == 0 || *cursor == 0 || out == 0 || out_size == 0u) {
        return false;
    }
    const char *p = *cursor;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p == '\0') {
        *cursor = p;
        return false;
    }
    const char *start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        ++p;
    }
    size_t len = (size_t)(p - start);
    if (len >= out_size) {
        len = out_size - 1u;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    *cursor = p;
    return true;
}

bool sc_gps_get(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    /* Routed through the core's public command dispatcher: keeps the
     * transport / precondition / logging plumbing in a single place
     * and avoids each telemetry endpoint reimplementing it. */
    return sc_core_send_sc_command(
        core,
        module_index,
        SC_CMD_GET_GPS,
        result,
        log_output,
        log_output_size
    );
}

static void sc_gps_snapshot_reset(ScGpsSnapshot *snap)
{
    if (snap == 0) {
        return;
    }
    snap->available = false;
    snap->latitude_deg = 0.0;
    snap->longitude_deg = 0.0;
    snap->speed_kmh = 0.0;
    snap->epoch_utc = 0u;
}

bool sc_gps_parse_result(
    const ScCommandResult *result,
    ScGpsSnapshot *parsed,
    char *error,
    size_t error_size
)
{
    sc_gps_set_error(error, error_size, "");
    sc_gps_snapshot_reset(parsed);

    if (result == 0 || parsed == 0) {
        sc_gps_set_error(error, error_size,
                         "gps parse failed: invalid arguments");
        return false;
    }

    if (result->status != SC_COMMAND_STATUS_OK ||
        strcmp(result->topic, SC_REPLY_TAG_GPS) != 0) {
        sc_gps_set_error(error, error_size,
                         "gps parse failed: response is not SC_OK GPS");
        return false;
    }

    /* Defaults match the firmware's "no fix" payload: available=0 with
     * the remaining slots zeroed. The loop below upgrades fields as
     * tokens arrive; unknown keys are tolerated so older host builds
     * stay forward-compatible when new fields are appended. */
    bool seen_available = false;
    long lat_e6 = 0;
    long lon_e6 = 0;
    int speed_x10 = 0;
    unsigned long epoch = 0u;

    const char *cursor = result->details;
    char token[SC_HELLO_RESPONSE_MAX];
    while (sc_gps_next_token(&cursor, token, sizeof(token))) {
        char *equals = strchr(token, '=');
        if (equals == 0) {
            sc_gps_set_error(error, error_size,
                             "gps parse failed: token without '='");
            return false;
        }
        *equals = '\0';
        const char *key = token;
        const char *raw_value = equals + 1u;
        if (raw_value[0] == '\0') {
            sc_gps_set_error(error, error_size,
                             "gps parse failed: empty value token");
            return false;
        }

        char *end = 0;
        if (strcmp(key, "available") == 0) {
            errno = 0;
            unsigned long v = strtoul(raw_value, &end, 10);
            if (errno != 0 || end == raw_value || *end != '\0' || v > 1u) {
                sc_gps_set_error(error, error_size,
                                 "gps parse failed: bad available value");
                return false;
            }
            parsed->available = (v == 1u);
            seen_available = true;
        } else if (strcmp(key, "lat_e6") == 0) {
            errno = 0;
            lat_e6 = strtol(raw_value, &end, 10);
            if (errno != 0 || end == raw_value || *end != '\0') {
                sc_gps_set_error(error, error_size,
                                 "gps parse failed: bad lat_e6 value");
                return false;
            }
        } else if (strcmp(key, "lon_e6") == 0) {
            errno = 0;
            lon_e6 = strtol(raw_value, &end, 10);
            if (errno != 0 || end == raw_value || *end != '\0') {
                sc_gps_set_error(error, error_size,
                                 "gps parse failed: bad lon_e6 value");
                return false;
            }
        } else if (strcmp(key, "speed_kmh_x10") == 0) {
            errno = 0;
            long v = strtol(raw_value, &end, 10);
            if (errno != 0 || end == raw_value || *end != '\0' ||
                v > INT_MAX || v < INT_MIN) {
                sc_gps_set_error(error, error_size,
                                 "gps parse failed: bad speed_kmh_x10 value");
                return false;
            }
            speed_x10 = (int)v;
        } else if (strcmp(key, "epoch") == 0) {
            errno = 0;
            epoch = strtoul(raw_value, &end, 10);
            if (errno != 0 || end == raw_value || *end != '\0') {
                sc_gps_set_error(error, error_size,
                                 "gps parse failed: bad epoch value");
                return false;
            }
        }
        /* Unknown keys: ignored for forward compatibility. */
    }

    if (!seen_available) {
        sc_gps_set_error(error, error_size,
                         "gps parse failed: missing 'available' field");
        return false;
    }

    /* Sanity-check ranges only when the fix is reported as valid; for
     * available=0 the firmware emits zeroed-out fields by construction. */
    if (parsed->available) {
        if (lat_e6 < -90000000L || lat_e6 > 90000000L) {
            sc_gps_set_error(error, error_size,
                             "gps parse failed: lat_e6 out of range");
            return false;
        }
        if (lon_e6 < -180000000L || lon_e6 > 180000000L) {
            sc_gps_set_error(error, error_size,
                             "gps parse failed: lon_e6 out of range");
            return false;
        }
    }

    parsed->latitude_deg = (double)lat_e6 / 1000000.0;
    parsed->longitude_deg = (double)lon_e6 / 1000000.0;
    parsed->speed_kmh = (double)speed_x10 / 10.0;
    parsed->epoch_utc = (uint32_t)epoch;
    return true;
}

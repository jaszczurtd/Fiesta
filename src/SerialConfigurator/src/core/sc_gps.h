#ifndef SC_GPS_H
#define SC_GPS_H

/**
 * @file sc_gps.h
 * @brief Host-side GPS telemetry snapshot for the SC_GET_GPS endpoint.
 *
 * GPS telemetry lives outside the descriptor framework on purpose:
 * lat/lon are int32 (microdegrees) and the descriptor reply format is
 * int16-shaped (SC_REPLY_PARAM_FMT uses %d). Rather than extending the
 * whole parameter pipeline for a single read-only, never-persisted
 * snapshot, SC_GET_GPS is a sibling of SC_GET_META: a discrete command
 * with its own atomic single-line reply (see SC_REPLY_GPS_FMT in
 * sc_protocol.h).
 *
 * This module is kept separate from sc_core.{c,h} so the core stays
 * focused on the configuration/auth lifecycle and so future telemetry
 * fields (heading, fix quality, satellite count) can land here without
 * touching the core surface.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decoded `SC_OK GPS ...` reply.
 *
 * `available` mirrors the firmware's `available=` field. When false,
 * the remaining fields are zeroed and must not be consumed. When true:
 *   - latitude_deg / longitude_deg are decoded from the wire's
 *     microdegree integers (lat_e6 / lon_e6) so the host stores them
 *     as ordinary decimal degrees.
 *   - speed_kmh is decoded from `speed_kmh_x10` (one decimal).
 *   - epoch_utc is the Unix epoch parsed from the GPS RMC sentence.
 */
typedef struct ScGpsSnapshot {
    bool available;
    double latitude_deg;
    double longitude_deg;
    double speed_kmh;
    uint32_t epoch_utc;
} ScGpsSnapshot;

/**
 * @brief Send `SC_GET_GPS` to the selected module and capture the reply.
 *
 * Thin wrapper over the SC core transport; same arguments and logging
 * semantics as @ref sc_core_sc_get_meta. The caller still owns parsing
 * the @p result via @ref sc_gps_parse_result.
 */
bool sc_gps_get(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
);

/**
 * @brief Decode the `SC_OK GPS ...` payload into @p parsed.
 *
 * Returns false (and writes @p error) when the reply is not SC_OK GPS,
 * when a token is malformed, when `available` is missing, or when
 * lat/lon are out of range while `available=1`. Unknown keys are
 * silently ignored so older host builds remain forward-compatible if
 * the firmware later appends fields (heading, hdop, ...).
 */
bool sc_gps_parse_result(
    const ScCommandResult *result,
    ScGpsSnapshot *parsed,
    char *error,
    size_t error_size
);

#ifdef __cplusplus
}
#endif

#endif /* SC_GPS_H */

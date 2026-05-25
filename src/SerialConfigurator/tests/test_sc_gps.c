/*
 * Coverage for the SC_GET_GPS host-side parser
 * (src/SerialConfigurator/src/core/sc_gps.c). The transport side is
 * exercised by the integration tests against a real device; this file
 * pins the wire-format → ScGpsSnapshot decoder so silent regressions
 * (truncation, range checks, forward-compat keys) get caught at unit
 * scope.
 *
 * Test fixtures bypass sc_gps_get entirely: ScCommandResult is filled
 * in by hand the way sc_core's parser would after framing succeeds,
 * which keeps the assertion surface focused on sc_gps_parse_result.
 */

#include "sc_core.h"
#include "sc_gps.h"
#include "sc_protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond, msg)                                                \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "[FAIL] %s\n", msg);                              \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static void make_gps_ok_result(ScCommandResult *r, const char *details)
{
    memset(r, 0, sizeof(*r));
    r->status = SC_COMMAND_STATUS_OK;
    snprintf(r->status_token, sizeof(r->status_token), "%s", SC_STATUS_OK);
    snprintf(r->topic, sizeof(r->topic), "%s", SC_REPLY_TAG_GPS);
    snprintf(r->details, sizeof(r->details), "%s", details);
    snprintf(r->response, sizeof(r->response),
             "%s %s %s", SC_STATUS_OK, SC_REPLY_TAG_GPS, details);
}

/* Fix available with a representative coordinate (50.061389,19.937222 -
 * Kraków market square) and 25.3 km/h forward speed. */
static int test_parse_ok_with_fix(void)
{
    ScCommandResult r;
    make_gps_ok_result(&r,
        "available=1 lat_e6=50061389 lon_e6=19937222 "
        "speed_kmh_x10=253 epoch=1716998400");

    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(sc_gps_parse_result(&r, &s, err, sizeof(err)),
                "parse_ok_with_fix: expected success");
    TEST_ASSERT(s.available, "parse_ok_with_fix: available=true expected");
    TEST_ASSERT(fabs(s.latitude_deg - 50.061389) < 1e-9,
                "parse_ok_with_fix: latitude mismatch");
    TEST_ASSERT(fabs(s.longitude_deg - 19.937222) < 1e-9,
                "parse_ok_with_fix: longitude mismatch");
    TEST_ASSERT(fabs(s.speed_kmh - 25.3) < 1e-9,
                "parse_ok_with_fix: speed mismatch");
    TEST_ASSERT(s.epoch_utc == 1716998400u,
                "parse_ok_with_fix: epoch mismatch");
    return 0;
}

/* No-fix payload: firmware reports available=0 with zeroed slots. */
static int test_parse_no_fix(void)
{
    ScCommandResult r;
    make_gps_ok_result(&r,
        "available=0 lat_e6=0 lon_e6=0 speed_kmh_x10=0 epoch=0");

    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(sc_gps_parse_result(&r, &s, err, sizeof(err)),
                "parse_no_fix: expected success");
    TEST_ASSERT(!s.available, "parse_no_fix: available=false expected");
    TEST_ASSERT(s.latitude_deg == 0.0 && s.longitude_deg == 0.0 &&
                s.speed_kmh == 0.0 && s.epoch_utc == 0u,
                "parse_no_fix: fields must be zero");
    return 0;
}

/* Negative coordinates (southern + western hemispheres) and negative
 * speed values are valid on the wire. */
static int test_parse_negative_values(void)
{
    ScCommandResult r;
    make_gps_ok_result(&r,
        "available=1 lat_e6=-33868820 lon_e6=-151209296 "
        "speed_kmh_x10=-1 epoch=1");

    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(sc_gps_parse_result(&r, &s, err, sizeof(err)),
                "parse_negative_values: expected success");
    TEST_ASSERT(fabs(s.latitude_deg - (-33.868820)) < 1e-9,
                "parse_negative_values: lat mismatch");
    TEST_ASSERT(fabs(s.longitude_deg - (-151.209296)) < 1e-9,
                "parse_negative_values: lon mismatch");
    TEST_ASSERT(fabs(s.speed_kmh - (-0.1)) < 1e-9,
                "parse_negative_values: speed mismatch");
    return 0;
}

/* Forward-compatibility: unknown keys must be tolerated so an older
 * host build keeps decoding a firmware that appended new fields. */
static int test_parse_unknown_key_ignored(void)
{
    ScCommandResult r;
    make_gps_ok_result(&r,
        "available=1 lat_e6=1000000 lon_e6=2000000 "
        "speed_kmh_x10=0 epoch=0 hdop_x10=12 sats=7");

    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(sc_gps_parse_result(&r, &s, err, sizeof(err)),
                "parse_unknown_key_ignored: expected success");
    TEST_ASSERT(s.available, "parse_unknown_key_ignored: available expected");
    TEST_ASSERT(fabs(s.latitude_deg - 1.0) < 1e-9,
                "parse_unknown_key_ignored: lat mismatch");
    TEST_ASSERT(fabs(s.longitude_deg - 2.0) < 1e-9,
                "parse_unknown_key_ignored: lon mismatch");
    return 0;
}

/* Reject when the 'available' field is missing - the rest of the
 * payload cannot be interpreted without it. */
static int test_parse_missing_available(void)
{
    ScCommandResult r;
    make_gps_ok_result(&r,
        "lat_e6=0 lon_e6=0 speed_kmh_x10=0 epoch=0");

    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(!sc_gps_parse_result(&r, &s, err, sizeof(err)),
                "parse_missing_available: expected failure");
    TEST_ASSERT(strstr(err, "available") != 0,
                "parse_missing_available: error must mention 'available'");
    return 0;
}

/* A malformed token without '=' must fail rather than silently slip
 * through and corrupt downstream snapshots. */
static int test_parse_bad_token(void)
{
    ScCommandResult r;
    make_gps_ok_result(&r, "available=1 lat_e6 lon_e6=0 speed_kmh_x10=0 epoch=0");

    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(!sc_gps_parse_result(&r, &s, err, sizeof(err)),
                "parse_bad_token: expected failure");
    return 0;
}

/* Range guard: latitude must lie within ±90°, longitude within ±180°.
 * Out-of-range values only fail when available=1 (no-fix payload is
 * allowed to ship zeros). */
static int test_parse_lat_out_of_range(void)
{
    ScCommandResult r;
    make_gps_ok_result(&r,
        "available=1 lat_e6=90000001 lon_e6=0 speed_kmh_x10=0 epoch=0");

    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(!sc_gps_parse_result(&r, &s, err, sizeof(err)),
                "parse_lat_out_of_range: expected failure");
    TEST_ASSERT(strstr(err, "lat") != 0,
                "parse_lat_out_of_range: error must mention 'lat'");
    return 0;
}

static int test_parse_lon_out_of_range(void)
{
    ScCommandResult r;
    make_gps_ok_result(&r,
        "available=1 lat_e6=0 lon_e6=-180000001 speed_kmh_x10=0 epoch=0");

    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(!sc_gps_parse_result(&r, &s, err, sizeof(err)),
                "parse_lon_out_of_range: expected failure");
    TEST_ASSERT(strstr(err, "lon") != 0,
                "parse_lon_out_of_range: error must mention 'lon'");
    return 0;
}

/* Wrong topic - reply was OK but for a different command. */
static int test_parse_wrong_topic(void)
{
    ScCommandResult r;
    memset(&r, 0, sizeof(r));
    r.status = SC_COMMAND_STATUS_OK;
    snprintf(r.status_token, sizeof(r.status_token), "%s", SC_STATUS_OK);
    snprintf(r.topic, sizeof(r.topic), "META");
    snprintf(r.details, sizeof(r.details), "module=ECU");
    snprintf(r.response, sizeof(r.response), "%s META module=ECU", SC_STATUS_OK);

    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(!sc_gps_parse_result(&r, &s, err, sizeof(err)),
                "parse_wrong_topic: expected failure");
    return 0;
}

/* Non-OK status must be rejected even when the topic is GPS. */
static int test_parse_non_ok_status(void)
{
    ScCommandResult r;
    memset(&r, 0, sizeof(r));
    r.status = SC_COMMAND_STATUS_NOT_READY;
    snprintf(r.status_token, sizeof(r.status_token), "%s", "SC_NOT_READY");
    snprintf(r.topic, sizeof(r.topic), "%s", SC_REPLY_TAG_GPS);

    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(!sc_gps_parse_result(&r, &s, err, sizeof(err)),
                "parse_non_ok_status: expected failure");
    return 0;
}

/* NULL guards: parser must not crash when result or parsed is NULL. */
static int test_parse_null_args(void)
{
    ScGpsSnapshot s;
    char err[128];
    TEST_ASSERT(!sc_gps_parse_result(0, &s, err, sizeof(err)),
                "parse_null_args: NULL result must fail");

    ScCommandResult r;
    make_gps_ok_result(&r, "available=0 lat_e6=0 lon_e6=0 speed_kmh_x10=0 epoch=0");
    TEST_ASSERT(!sc_gps_parse_result(&r, 0, err, sizeof(err)),
                "parse_null_args: NULL parsed must fail");
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_parse_ok_with_fix();
    rc |= test_parse_no_fix();
    rc |= test_parse_negative_values();
    rc |= test_parse_unknown_key_ignored();
    rc |= test_parse_missing_available();
    rc |= test_parse_bad_token();
    rc |= test_parse_lat_out_of_range();
    rc |= test_parse_lon_out_of_range();
    rc |= test_parse_wrong_topic();
    rc |= test_parse_non_ok_status();
    rc |= test_parse_null_args();

    if (rc == 0) {
        printf("[OK] test_sc_gps: all cases passed\n");
    }
    return rc;
}

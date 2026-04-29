/*
 * R1.1 - coverage of the generic SC param framework in
 * src/common/scDefinitions/. Verifies:
 *   - find_by_id, validate_range, get_i16, set_i16, load_defaults
 *     (kind / read-only / range / NULL guards),
 *   - reply_get_param_list / reply_get_values_i16 / reply_get_param
 *     emit byte-identical strings to the legacy ECU/Clocks/OilAndSpeed
 *     emitters,
 *   - blob_encode / blob_decode round-trip + reject paths +
 *     schema-versioned upgrade where a V1-shaped blob is decoded
 *     into a V2-shaped values struct (V2-only field stays at default),
 *   - blob_encode for the canonical ECU V2 layout matches
 *     ECU's existing 18-byte wire layout byte-for-byte (regression
 *     guard for R1.2 migration).
 */

#include "sc_param_handlers.h"
#include "sc_param_types.h"
#include "sc_protocol.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond, msg)                                                \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "[FAIL] %s\n", msg);                              \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/* ── ECU-shaped fixture mirroring src/ECU/config.c V2 layout ─────── */

typedef struct {
    int16_t fan_coolant_start_c;
    int16_t fan_coolant_stop_c;
    int16_t fan_air_start_c;
    int16_t fan_air_stop_c;
    int16_t heater_stop_c;
    int16_t nominal_rpm; /* schema_since=2 - V1 blob lacks this field. */
} ecu_values_t;

static const sc_param_descriptor_t k_ecu_descs[] = {
    SC_PARAM_SCALAR_I16("fan_coolant_start_c", ecu_values_t,
                        fan_coolant_start_c, 70, 130, 95, 1, "cooling_fan"),
    SC_PARAM_SCALAR_I16("fan_coolant_stop_c", ecu_values_t,
                        fan_coolant_stop_c, 50, 120, 85, 1, "cooling_fan"),
    SC_PARAM_SCALAR_I16("fan_air_start_c", ecu_values_t,
                        fan_air_start_c, 20, 90, 60, 1, "cabin_fan"),
    SC_PARAM_SCALAR_I16("fan_air_stop_c", ecu_values_t,
                        fan_air_stop_c, -20, 80, 50, 1, "cabin_fan"),
    SC_PARAM_SCALAR_I16("heater_stop_c", ecu_values_t,
                        heater_stop_c, 40, 100, 75, 1, "engine_heater"),
    SC_PARAM_SCALAR_I16("nominal_rpm", ecu_values_t, nominal_rpm, 700, 1200,
                        850, 2, "idle"),
};
static const size_t k_ecu_descs_count =
    sizeof(k_ecu_descs) / sizeof(k_ecu_descs[0]);

/* ── Clocks-shaped fixture: read-only, not-persisted threshold params */

typedef struct {
    int16_t coolant_warn_c;
    int16_t coolant_max_c;
    int16_t oil_warn_c;
    int16_t oil_max_c;
} clocks_values_t;

static const sc_param_descriptor_t k_clocks_descs[] = {
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("coolant_warn_c", clocks_values_t,
                                         coolant_warn_c, 60, 120, 95, 1,
                                         "coolant"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("coolant_max_c", clocks_values_t,
                                         coolant_max_c, 80, 130, 105, 1,
                                         "coolant"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("oil_warn_c", clocks_values_t,
                                         oil_warn_c, 60, 130, 105, 1, "oil"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("oil_max_c", clocks_values_t,
                                         oil_max_c, 80, 140, 120, 1, "oil"),
};
static const size_t k_clocks_descs_count =
    sizeof(k_clocks_descs) / sizeof(k_clocks_descs[0]);

/* ── Reply emit capture ──────────────────────────────────────────── */

#define CAPTURE_LINES_MAX 8u
#define CAPTURE_LINE_LEN  256u

typedef struct {
    char lines[CAPTURE_LINES_MAX][CAPTURE_LINE_LEN];
    size_t count;
} capture_t;

static void capture_reset(capture_t *cap) {
    memset(cap, 0, sizeof(*cap));
}

static void capture_emit(const char *payload, void *user) {
    capture_t *cap = (capture_t *)user;
    if (cap->count >= CAPTURE_LINES_MAX) {
        return;
    }
    strncpy(cap->lines[cap->count], payload, CAPTURE_LINE_LEN - 1u);
    cap->lines[cap->count][CAPTURE_LINE_LEN - 1u] = '\0';
    cap->count++;
}

/* ── Tests ───────────────────────────────────────────────────────── */

static int test_find_by_id_paths(void) {
    TEST_ASSERT(sc_param_find_by_id(NULL, 6u, "fan_coolant_start_c") == NULL,
                "find_by_id NULL descs returns NULL");
    TEST_ASSERT(sc_param_find_by_id(k_ecu_descs, k_ecu_descs_count, NULL) ==
                    NULL,
                "find_by_id NULL id returns NULL");
    TEST_ASSERT(
        sc_param_find_by_id(k_ecu_descs, k_ecu_descs_count, "missing") == NULL,
        "find_by_id unknown id returns NULL");
    const sc_param_descriptor_t *d =
        sc_param_find_by_id(k_ecu_descs, k_ecu_descs_count, "nominal_rpm");
    TEST_ASSERT(d != NULL, "find_by_id finds nominal_rpm");
    TEST_ASSERT(d == &k_ecu_descs[5], "find_by_id returns matching pointer");
    return 0;
}

static int test_validate_range_boundaries(void) {
    const sc_param_descriptor_t *d =
        sc_param_find_by_id(k_ecu_descs, k_ecu_descs_count, "fan_air_stop_c");
    TEST_ASSERT(d != NULL, "fan_air_stop_c found");

    TEST_ASSERT(!sc_param_validate_range(NULL, 0), "NULL desc rejected");
    TEST_ASSERT(!sc_param_validate_range(d, -21), "below min rejected");
    TEST_ASSERT(sc_param_validate_range(d, -20), "at min accepted");
    TEST_ASSERT(sc_param_validate_range(d, 0), "mid accepted");
    TEST_ASSERT(sc_param_validate_range(d, 80), "at max accepted");
    TEST_ASSERT(!sc_param_validate_range(d, 81), "above max rejected");
    return 0;
}

static int test_get_set_i16_guards(void) {
    ecu_values_t v;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &v);

    /* Reads at offsets match. */
    TEST_ASSERT(sc_param_get_i16(&k_ecu_descs[0], &v) == 95,
                "fan_coolant_start_c default");
    TEST_ASSERT(sc_param_get_i16(&k_ecu_descs[5], &v) == 850,
                "nominal_rpm default");

    /* NULL guards. */
    TEST_ASSERT(sc_param_get_i16(NULL, &v) == 0, "NULL desc returns 0");
    TEST_ASSERT(sc_param_get_i16(&k_ecu_descs[0], NULL) == 0,
                "NULL ctx returns 0");

    /* Successful set. */
    TEST_ASSERT(sc_param_set_i16(&k_ecu_descs[0], &v, 100),
                "valid set succeeds");
    TEST_ASSERT(v.fan_coolant_start_c == 100, "value written");

    /* Range guard. */
    TEST_ASSERT(!sc_param_set_i16(&k_ecu_descs[0], &v, 200),
                "out-of-range set rejected");
    TEST_ASSERT(v.fan_coolant_start_c == 100, "value unchanged after reject");

    /* Read-only guard (clocks). Pre-zero the values struct so the
     * post-load_defaults assertion is checking that load_defaults left
     * the slot alone, not that uninitialised stack happens to read 0. */
    clocks_values_t c = {0};
    sc_param_load_defaults(k_clocks_descs, k_clocks_descs_count, &c);
    TEST_ASSERT(c.coolant_warn_c == 0, "RO field untouched by load_defaults");
    TEST_ASSERT(!sc_param_set_i16(&k_clocks_descs[0], &c, 95),
                "RO set rejected");
    return 0;
}

static int test_load_defaults_skips_readonly(void) {
    clocks_values_t c;
    memset(&c, 0xAB, sizeof(c));
    const size_t written =
        sc_param_load_defaults(k_clocks_descs, k_clocks_descs_count, &c);
    TEST_ASSERT(written == 0u, "RO descriptors skipped (zero writes)");
    /* The struct is left as caller had it. */
    TEST_ASSERT(c.coolant_warn_c == (int16_t)0xABABu,
                "RO field untouched by load_defaults");

    ecu_values_t v;
    memset(&v, 0, sizeof(v));
    const size_t ecu_written =
        sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &v);
    TEST_ASSERT(ecu_written == 6u, "all ECU writable defaults applied");
    TEST_ASSERT(v.fan_coolant_start_c == 95, "fan coolant start default");
    TEST_ASSERT(v.fan_coolant_stop_c == 85, "fan coolant stop default");
    TEST_ASSERT(v.fan_air_start_c == 60, "fan air start default");
    TEST_ASSERT(v.fan_air_stop_c == 50, "fan air stop default");
    TEST_ASSERT(v.heater_stop_c == 75, "heater stop default");
    TEST_ASSERT(v.nominal_rpm == 850, "nominal_rpm default");
    return 0;
}

static int test_reply_param_list(void) {
    capture_t cap;
    capture_reset(&cap);
    sc_param_reply_get_param_list(k_ecu_descs, k_ecu_descs_count, capture_emit,
                                  &cap);
    TEST_ASSERT(cap.count == 1u, "one line emitted");
    TEST_ASSERT(strcmp(cap.lines[0],
                       "SC_OK PARAM_LIST fan_coolant_start_c,"
                       "fan_coolant_stop_c,fan_air_start_c,fan_air_stop_c,"
                       "heater_stop_c,nominal_rpm") == 0,
                "param list matches expected wire format");
    return 0;
}

static int test_reply_param_values(void) {
    ecu_values_t v;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &v);

    capture_t cap;
    capture_reset(&cap);
    sc_param_reply_get_values_i16(k_ecu_descs, k_ecu_descs_count, &v,
                                  capture_emit, &cap);
    TEST_ASSERT(cap.count == 1u, "one values line emitted");
    TEST_ASSERT(strcmp(cap.lines[0],
                       "SC_OK PARAM_VALUES fan_coolant_start_c=95"
                       " fan_coolant_stop_c=85 fan_air_start_c=60"
                       " fan_air_stop_c=50 heater_stop_c=75"
                       " nominal_rpm=850") == 0,
                "values reply matches legacy ECU format");
    return 0;
}

/* A descriptor with an empty group string must surface as
 * `group=general` on the wire so the parser never sees an empty
 * value token. */
static int test_reply_get_param_emits_general_for_empty_group(void) {
    typedef struct {
        int16_t solo;
    } solo_values_t;
    static const sc_param_descriptor_t k_solo_descs[] = {
        SC_PARAM_SCALAR_I16("solo", solo_values_t, solo, 0, 100, 50, 1, ""),
    };

    solo_values_t v = { .solo = 50 };
    capture_t cap;
    capture_reset(&cap);
    sc_param_reply_get_param(k_solo_descs, 1u, &v, "solo",
                             capture_emit, &cap);
    TEST_ASSERT(cap.count == 1u, "one line emitted");
    TEST_ASSERT(strstr(cap.lines[0], "group=general") != NULL,
                "empty group falls back to 'general' on the wire");
    return 0;
}

static int test_reply_get_param_happy_and_invalid(void) {
    ecu_values_t v;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &v);

    capture_t cap;
    capture_reset(&cap);
    sc_param_reply_get_param(k_ecu_descs, k_ecu_descs_count, &v,
                             "heater_stop_c", capture_emit, &cap);
    TEST_ASSERT(cap.count == 1u, "happy path emits one line");
    TEST_ASSERT(strcmp(cap.lines[0],
                       "SC_OK PARAM id=heater_stop_c value=75 min=40 max=100"
                       " default=75 group=engine_heater") == 0,
                "happy path reply matches ECU format with group field");

    capture_reset(&cap);
    sc_param_reply_get_param(k_ecu_descs, k_ecu_descs_count, &v, "nope",
                             capture_emit, &cap);
    TEST_ASSERT(cap.count == 1u, "invalid id emits one line");
    TEST_ASSERT(strcmp(cap.lines[0], "SC_INVALID_PARAM_ID id=nope") == 0,
                "invalid id reply matches legacy ECU format");
    return 0;
}

static int test_blob_size_for_schema(void) {
    TEST_ASSERT(sc_param_blob_size_for_schema(k_ecu_descs, k_ecu_descs_count,
                                              1) == 16u,
                "ECU V1 blob size matches legacy");
    TEST_ASSERT(sc_param_blob_size_for_schema(k_ecu_descs, k_ecu_descs_count,
                                              2) == 18u,
                "ECU V2 blob size matches legacy");
    TEST_ASSERT(
        sc_param_blob_size_for_schema(k_ecu_descs, k_ecu_descs_count, 0) == 0u,
        "ECU schema=0 yields zero (no persisted fields)");
    TEST_ASSERT(sc_param_blob_size_for_schema(k_clocks_descs,
                                              k_clocks_descs_count, 1) == 0u,
                "Clocks RO/NotPersisted yields zero");
    return 0;
}

static int test_blob_round_trip_v2(void) {
    ecu_values_t in;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &in);
    in.fan_coolant_start_c = 110;
    in.nominal_rpm = 900;

    uint8_t blob[18];
    const size_t written = sc_param_blob_encode(
        k_ecu_descs, k_ecu_descs_count, &in, 2, blob, sizeof(blob));
    TEST_ASSERT(written == 18u, "encode writes 18 bytes (V2)");

    /* Schema header LE = 0x0002. */
    TEST_ASSERT(blob[0] == 0x02 && blob[1] == 0x00, "schema LE = 2");
    /* fan_coolant_start_c = 110 -> 0x006E. */
    TEST_ASSERT(blob[2] == 0x6E && blob[3] == 0x00,
                "fan_coolant_start_c LE encoded");
    /* nominal_rpm = 900 -> 0x0384 (LE: 0x84 0x03). */
    TEST_ASSERT(blob[12] == 0x84 && blob[13] == 0x03, "nominal_rpm LE encoded");

    /* CRC matches independently. */
    const uint32_t crc = sc_param_crc32(blob, 14u);
    const uint32_t in_crc = (uint32_t)blob[14] | ((uint32_t)blob[15] << 8) |
                            ((uint32_t)blob[16] << 16) |
                            ((uint32_t)blob[17] << 24);
    TEST_ASSERT(crc == in_crc, "encoded CRC matches recomputed CRC");

    ecu_values_t out;
    memset(&out, 0, sizeof(out));
    uint16_t schema = 0u;
    TEST_ASSERT(sc_param_blob_decode(k_ecu_descs, k_ecu_descs_count, &out, blob,
                                     sizeof(blob), &schema),
                "decode succeeds");
    TEST_ASSERT(schema == 2u, "decoded schema is 2");
    TEST_ASSERT(out.fan_coolant_start_c == 110, "field round-tripped");
    TEST_ASSERT(out.nominal_rpm == 900, "V2-only field round-tripped");
    return 0;
}

static int test_blob_round_trip_v1_upgrade_path(void) {
    /* V1 blob: 5 fields (no nominal_rpm) + CRC. We synthesise it by
     * temporarily flagging nominal_rpm as schema_since=2 (already true)
     * and asking the encoder for schema=1. */
    ecu_values_t in;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &in);
    in.fan_coolant_start_c = 100;
    in.nominal_rpm = 850; /* in-memory before encode; will not be persisted. */

    uint8_t blob[16];
    const size_t written = sc_param_blob_encode(
        k_ecu_descs, k_ecu_descs_count, &in, 1, blob, sizeof(blob));
    TEST_ASSERT(written == 16u, "encode writes 16 bytes (V1)");
    TEST_ASSERT(blob[0] == 0x01 && blob[1] == 0x00, "schema LE = 1");

    /* Decode V1 blob into a fresh values struct that was pre-loaded
     * with V2 defaults. nominal_rpm should retain the default rather
     * than be overwritten by anything from the V1 buffer. */
    ecu_values_t out;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &out);
    out.nominal_rpm = 999; /* sentinel - must NOT be touched by V1 decode. */

    uint16_t schema = 0u;
    TEST_ASSERT(sc_param_blob_decode(k_ecu_descs, k_ecu_descs_count, &out, blob,
                                     sizeof(blob), &schema),
                "V1 decode succeeds");
    TEST_ASSERT(schema == 1u, "decoded schema is 1");
    TEST_ASSERT(out.fan_coolant_start_c == 100, "V1 field decoded");
    TEST_ASSERT(out.nominal_rpm == 999,
                "V2-only field left at pre-decode sentinel");
    return 0;
}

static int test_blob_decode_rejects_bad_inputs(void) {
    ecu_values_t in;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &in);

    uint8_t blob[18];
    sc_param_blob_encode(k_ecu_descs, k_ecu_descs_count, &in, 2, blob,
                         sizeof(blob));

    ecu_values_t out;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &out);

    /* Wrong size: pass V2 buffer but tell decoder it is 17 bytes. */
    TEST_ASSERT(!sc_param_blob_decode(k_ecu_descs, k_ecu_descs_count, &out,
                                      blob, 17u, NULL),
                "wrong size rejected");

    /* Bad CRC: flip last byte. */
    uint8_t corrupted[18];
    memcpy(corrupted, blob, sizeof(blob));
    corrupted[17] ^= 0xFF;
    TEST_ASSERT(!sc_param_blob_decode(k_ecu_descs, k_ecu_descs_count, &out,
                                      corrupted, sizeof(corrupted), NULL),
                "bad CRC rejected");

    /* Unknown schema: bytes [0..1] = 99. blob_size_for_schema=18 still,
     * but the CRC won't match the corrupted prefix. */
    uint8_t bad_schema[18];
    memcpy(bad_schema, blob, sizeof(blob));
    bad_schema[0] = 99;
    bad_schema[1] = 0;
    TEST_ASSERT(!sc_param_blob_decode(k_ecu_descs, k_ecu_descs_count, &out,
                                      bad_schema, sizeof(bad_schema), NULL),
                "unknown schema rejected (CRC mismatch)");

    /* Tiny buffer (below 6-byte floor). */
    uint8_t tiny[3] = {0};
    TEST_ASSERT(!sc_param_blob_decode(k_ecu_descs, k_ecu_descs_count, &out,
                                      tiny, sizeof(tiny), NULL),
                "tiny buffer rejected");

    return 0;
}

static int test_crc32_reference_vector(void) {
    /* Same vector as JaszczurHAL CRC8 self-test inspirits - "123456789"
     * reference for CRC32/PKZIP is 0xCBF43926 (well-known). */
    const uint8_t buf[] = "123456789";
    TEST_ASSERT(sc_param_crc32(buf, 9u) == 0xCBF43926u,
                "CRC32 reference vector");
    TEST_ASSERT(sc_param_crc32(NULL, 9u) == 0u, "NULL data returns 0");
    return 0;
}

/* ── Phase 8.2 — staging-mirror writes ───────────────────────────── */

static int test_reply_set_param_writes_only_staging(void) {
    ecu_values_t staging;
    ecu_values_t active;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &staging);
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &active);

    capture_t cap;
    capture_reset(&cap);
    const bool ok = sc_param_reply_set_param(
        k_ecu_descs, k_ecu_descs_count, &staging, &active,
        "fan_coolant_start_c", 110, capture_emit, &cap);
    TEST_ASSERT(ok, "set_param happy path returns true");
    TEST_ASSERT(cap.count == 1u, "one reply line");
    TEST_ASSERT(strcmp(cap.lines[0],
                       "SC_OK PARAM_SET id=fan_coolant_start_c"
                       " staged=110 active=95") == 0,
                "happy-path reply format");
    TEST_ASSERT(staging.fan_coolant_start_c == 110, "staging mutated");
    TEST_ASSERT(active.fan_coolant_start_c == 95, "active untouched");
    /* Other staging fields stay at defaults too. */
    TEST_ASSERT(staging.heater_stop_c == 75,
                "unrelated staging field untouched");
    return 0;
}

static int test_reply_set_param_unknown_id_rejected(void) {
    ecu_values_t staging;
    ecu_values_t active;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &staging);
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &active);

    capture_t cap;
    capture_reset(&cap);
    const bool ok = sc_param_reply_set_param(
        k_ecu_descs, k_ecu_descs_count, &staging, &active, "nope", 0,
        capture_emit, &cap);
    TEST_ASSERT(!ok, "unknown id returns false");
    TEST_ASSERT(cap.count == 1u, "one reply line");
    TEST_ASSERT(strcmp(cap.lines[0], "SC_INVALID_PARAM_ID id=nope") == 0,
                "unknown id reply format");
    /* Sanity: a known-default field is still at its default value, i.e.
     * the helper did not mutate anything before the lookup miss. */
    TEST_ASSERT(staging.fan_coolant_start_c == 95, "staging untouched");
    return 0;
}

static int test_reply_set_param_read_only_rejected(void) {
    /* Clocks fixture has all RO descriptors. Pre-zero staging so we can
     * assert nothing leaks into it. */
    clocks_values_t staging = {0};
    clocks_values_t active = {0};

    capture_t cap;
    capture_reset(&cap);
    const bool ok = sc_param_reply_set_param(
        k_clocks_descs, k_clocks_descs_count, &staging, &active,
        "coolant_warn_c", 100, capture_emit, &cap);
    TEST_ASSERT(!ok, "RO descriptor returns false");
    TEST_ASSERT(cap.count == 1u, "one reply line");
    TEST_ASSERT(strcmp(cap.lines[0],
                       "SC_BAD_REQUEST read_only id=coolant_warn_c") == 0,
                "RO reply format");
    TEST_ASSERT(staging.coolant_warn_c == 0, "RO staging slot untouched");
    return 0;
}

static int test_reply_set_param_out_of_range_rejected(void) {
    ecu_values_t staging;
    ecu_values_t active;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &staging);
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &active);

    capture_t cap;
    capture_reset(&cap);
    /* fan_coolant_start_c [70, 130]: 200 above max. */
    const bool ok = sc_param_reply_set_param(
        k_ecu_descs, k_ecu_descs_count, &staging, &active,
        "fan_coolant_start_c", 200, capture_emit, &cap);
    TEST_ASSERT(!ok, "OOR returns false");
    TEST_ASSERT(cap.count == 1u, "one reply line");
    TEST_ASSERT(strcmp(cap.lines[0],
                       "SC_BAD_REQUEST out_of_range"
                       " id=fan_coolant_start_c min=70 max=130") == 0,
                "OOR reply format");
    TEST_ASSERT(staging.fan_coolant_start_c == 95,
                "staging untouched on OOR");
    return 0;
}

static int test_reply_set_param_null_active_reports_staged_twice(void) {
    ecu_values_t staging;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &staging);

    capture_t cap;
    capture_reset(&cap);
    const bool ok = sc_param_reply_set_param(
        k_ecu_descs, k_ecu_descs_count, &staging, /*active_ctx=*/NULL,
        "fan_coolant_start_c", 88, capture_emit, &cap);
    TEST_ASSERT(ok, "NULL active still writes staging");
    TEST_ASSERT(cap.count == 1u, "one reply line");
    TEST_ASSERT(strcmp(cap.lines[0],
                       "SC_OK PARAM_SET id=fan_coolant_start_c"
                       " staged=88 active=88") == 0,
                "active mirrors staged when active_ctx=NULL");
    TEST_ASSERT(staging.fan_coolant_start_c == 88, "staging mutated");
    return 0;
}

static int test_copy_active_to_staging_round_trip(void) {
    /* Active carries non-default values; staging zeroed. After copy the
     * 6 writable ECU fields match active. */
    ecu_values_t active = {
        .fan_coolant_start_c = 100, .fan_coolant_stop_c = 80,
        .fan_air_start_c = 55,      .fan_air_stop_c = 30,
        .heater_stop_c = 60,        .nominal_rpm = 1100,
    };
    ecu_values_t staging;
    memset(&staging, 0, sizeof(staging));

    const size_t copied = sc_param_copy_active_to_staging(
        k_ecu_descs, k_ecu_descs_count, &active, &staging);
    TEST_ASSERT(copied == 6u, "all 6 ECU writable fields copied");
    TEST_ASSERT(staging.fan_coolant_start_c == 100, "field 0 copied");
    TEST_ASSERT(staging.fan_coolant_stop_c == 80, "field 1 copied");
    TEST_ASSERT(staging.fan_air_start_c == 55, "field 2 copied");
    TEST_ASSERT(staging.fan_air_stop_c == 30, "field 3 copied");
    TEST_ASSERT(staging.heater_stop_c == 60, "field 4 copied");
    TEST_ASSERT(staging.nominal_rpm == 1100, "field 5 copied");
    return 0;
}

static int test_copy_staging_to_active_round_trip(void) {
    ecu_values_t staging = {
        .fan_coolant_start_c = 105, .fan_coolant_stop_c = 90,
        .fan_air_start_c = 70,      .fan_air_stop_c = 40,
        .heater_stop_c = 65,        .nominal_rpm = 950,
    };
    ecu_values_t active;
    memset(&active, 0, sizeof(active));

    const size_t copied = sc_param_copy_staging_to_active(
        k_ecu_descs, k_ecu_descs_count, &staging, &active);
    TEST_ASSERT(copied == 6u, "all 6 ECU writable fields copied");
    /* Spot-check both ends and the middle. */
    TEST_ASSERT(active.fan_coolant_start_c == 105, "field 0 copied");
    TEST_ASSERT(active.fan_air_stop_c == 40, "field 3 copied");
    TEST_ASSERT(active.nominal_rpm == 950, "field 5 copied");
    return 0;
}

static int test_copy_preserves_readonly_slots(void) {
    /* Clocks fixture: all RO. Copy must not touch the destination. */
    clocks_values_t staging;
    clocks_values_t active;
    memset(&staging, 0xAB, sizeof(staging));
    memset(&active, 0x12, sizeof(active));

    const size_t copied = sc_param_copy_active_to_staging(
        k_clocks_descs, k_clocks_descs_count, &active, &staging);
    TEST_ASSERT(copied == 0u, "RO descriptors yield 0 copies");
    TEST_ASSERT(staging.coolant_warn_c == (int16_t)0xABABu,
                "RO destination slot untouched");
    TEST_ASSERT(staging.oil_max_c == (int16_t)0xABABu,
                "RO destination slot untouched");
    return 0;
}

static int test_copy_null_inputs_safe(void) {
    ecu_values_t v;
    sc_param_load_defaults(k_ecu_descs, k_ecu_descs_count, &v);

    TEST_ASSERT(sc_param_copy_active_to_staging(NULL, k_ecu_descs_count, &v,
                                                &v) == 0u,
                "NULL descs returns 0");
    TEST_ASSERT(sc_param_copy_active_to_staging(k_ecu_descs, k_ecu_descs_count,
                                                NULL, &v) == 0u,
                "NULL active returns 0");
    TEST_ASSERT(sc_param_copy_active_to_staging(k_ecu_descs, k_ecu_descs_count,
                                                &v, NULL) == 0u,
                "NULL staging returns 0");
    TEST_ASSERT(sc_param_copy_staging_to_active(NULL, k_ecu_descs_count, &v,
                                                &v) == 0u,
                "NULL descs (reverse) returns 0");
    return 0;
}

/* ── Driver ─────────────────────────────────────────────────────── */

int main(void) {
    int failures = 0;
    failures += test_find_by_id_paths();
    failures += test_validate_range_boundaries();
    failures += test_get_set_i16_guards();
    failures += test_load_defaults_skips_readonly();
    failures += test_reply_param_list();
    failures += test_reply_param_values();
    failures += test_reply_get_param_emits_general_for_empty_group();
    failures += test_reply_get_param_happy_and_invalid();
    failures += test_blob_size_for_schema();
    failures += test_blob_round_trip_v2();
    failures += test_blob_round_trip_v1_upgrade_path();
    failures += test_blob_decode_rejects_bad_inputs();
    failures += test_crc32_reference_vector();
    failures += test_reply_set_param_writes_only_staging();
    failures += test_reply_set_param_unknown_id_rejected();
    failures += test_reply_set_param_read_only_rejected();
    failures += test_reply_set_param_out_of_range_rejected();
    failures += test_reply_set_param_null_active_reports_staged_twice();
    failures += test_copy_active_to_staging_round_trip();
    failures += test_copy_staging_to_active_round_trip();
    failures += test_copy_preserves_readonly_slots();
    failures += test_copy_null_inputs_safe();

    if (failures == 0) {
        printf("[OK] sc_param: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "[FAIL] sc_param: %d test(s) failed\n", failures);
    return 1;
}

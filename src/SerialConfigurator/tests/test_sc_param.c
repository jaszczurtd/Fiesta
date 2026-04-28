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
                        fan_coolant_start_c, 70, 130, 95, 1),
    SC_PARAM_SCALAR_I16("fan_coolant_stop_c", ecu_values_t,
                        fan_coolant_stop_c, 50, 120, 85, 1),
    SC_PARAM_SCALAR_I16("fan_air_start_c", ecu_values_t,
                        fan_air_start_c, 20, 90, 60, 1),
    SC_PARAM_SCALAR_I16("fan_air_stop_c", ecu_values_t,
                        fan_air_stop_c, -20, 80, 50, 1),
    SC_PARAM_SCALAR_I16("heater_stop_c", ecu_values_t,
                        heater_stop_c, 40, 100, 75, 1),
    SC_PARAM_SCALAR_I16("nominal_rpm", ecu_values_t, nominal_rpm, 700, 1200,
                        850, 2),
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
                                         coolant_warn_c, 60, 120, 95, 1),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("coolant_max_c", clocks_values_t,
                                         coolant_max_c, 80, 130, 105, 1),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("oil_warn_c", clocks_values_t,
                                         oil_warn_c, 60, 130, 105, 1),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("oil_max_c", clocks_values_t,
                                         oil_max_c, 80, 140, 120, 1),
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
                       " default=75") == 0,
                "happy path reply matches legacy ECU format");

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
    /* fan_coolant_start_c = 110 → 0x006E. */
    TEST_ASSERT(blob[2] == 0x6E && blob[3] == 0x00,
                "fan_coolant_start_c LE encoded");
    /* nominal_rpm = 900 → 0x0384 (LE: 0x84 0x03). */
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

/* ── Driver ─────────────────────────────────────────────────────── */

int main(void) {
    int failures = 0;
    failures += test_find_by_id_paths();
    failures += test_validate_range_boundaries();
    failures += test_get_set_i16_guards();
    failures += test_load_defaults_skips_readonly();
    failures += test_reply_param_list();
    failures += test_reply_param_values();
    failures += test_reply_get_param_happy_and_invalid();
    failures += test_blob_size_for_schema();
    failures += test_blob_round_trip_v2();
    failures += test_blob_round_trip_v1_upgrade_path();
    failures += test_blob_decode_rejects_bad_inputs();
    failures += test_crc32_reference_vector();

    if (failures == 0) {
        printf("[OK] sc_param: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "[FAIL] sc_param: %d test(s) failed\n", failures);
    return 1;
}

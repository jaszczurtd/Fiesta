#pragma once

/**
 * @file sc_param_types.h
 * @brief Tagged-union parameter descriptor used by the SerialConfigurator
 *        framework on the firmware side.
 *
 * Modules declare a static const array of @ref sc_param_descriptor_t (one
 * per wire-visible parameter) and pass it to the generic helpers in
 * @ref sc_param_handlers.h. The descriptor carries everything those
 * helpers need: wire id, kind tag, flags, schema-since gate, and (for
 * SCALAR_I16 today) the byte offset into the module's values struct
 * plus min/max/default bounds.
 *
 * R1.1 ships SCALAR_I16 only. Other kinds are reserved on the enum so
 * that adding axes / 1D-LUT / 2D-MAP support in a future slice does not
 * change the public API.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SC_PARAM_KIND_SCALAR_I16 = 1,
    /* Reserved for future kinds. Helpers in this slice (R1.1) treat
     * descriptors of these kinds as opaque and skip them. */
    SC_PARAM_KIND_AXIS_I16   = 2,
    SC_PARAM_KIND_MAP_1D_I16 = 3,
    SC_PARAM_KIND_MAP_2D_I16 = 4,
} sc_param_kind_t;

typedef enum {
    SC_PARAM_FLAG_NONE          = 0u,
    /** Reject @c sc_param_set_i16 and skip during blob decode writes. */
    SC_PARAM_FLAG_READ_ONLY     = 1u << 0,
    /** Skip during @c sc_param_blob_encode and @c sc_param_blob_decode. */
    SC_PARAM_FLAG_NOT_PERSISTED = 1u << 1,
} sc_param_flags_t;

typedef struct {
    /** Wire identifier (e.g. "fan_coolant_start_c"). Must be unique. */
    const char *id;
    sc_param_kind_t kind;
    /** Bitfield of @ref sc_param_flags_t. */
    uint16_t flags;
    /**
     * @brief First schema version that contains this descriptor.
     *
     * @c sc_param_blob_decode skips writing this field when the loaded
     * blob's schema is strictly less than @c schema_since, leaving
     * whatever default the values struct had at decode time. Module
     * declarations start at 1 and bump the value when a new parameter
     * is introduced - this matches ECU's V1->V2 history (V1 had 5
     * params, V2 added @c nominalRpm with @c schema_since=2).
     */
    uint16_t schema_since;
    union {
        struct {
            /** Byte offset of the @c int16_t field inside the values struct. */
            size_t value_offset;
            int16_t min_value;
            int16_t max_value;
            int16_t default_value;
        } scalar_i16;
    } as;
} sc_param_descriptor_t;

/**
 * @brief Builder for a writable SCALAR_I16 descriptor.
 *
 * Use as an element of a `static const sc_param_descriptor_t k_*[] = {...}`
 * array. Pass the host values-struct type as @p struct_t and the
 * field name as @p field; @c offsetof figures out the byte offset.
 */
#define SC_PARAM_SCALAR_I16(id_str, struct_t, field, min_v, max_v,            \
                            default_v, since)                                 \
    {                                                                         \
        .id = (id_str),                                                       \
        .kind = SC_PARAM_KIND_SCALAR_I16,                                     \
        .flags = (uint16_t)SC_PARAM_FLAG_NONE,                                \
        .schema_since = (uint16_t)(since),                                    \
        .as = { .scalar_i16 = {                                               \
            .value_offset = offsetof(struct_t, field),                        \
            .min_value = (int16_t)(min_v),                                    \
            .max_value = (int16_t)(max_v),                                    \
            .default_value = (int16_t)(default_v),                            \
        } },                                                                  \
    }

/**
 * @brief Builder for a read-only / not-persisted SCALAR_I16 descriptor.
 *
 * Targets Clocks / OilAndSpeed today: their parameters mirror
 * compile-time defines so they are visible on the wire but never
 * mutated and never persisted.
 */
#define SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED(id_str, struct_t, field,         \
                                             min_v, max_v, default_v, since)  \
    {                                                                         \
        .id = (id_str),                                                       \
        .kind = SC_PARAM_KIND_SCALAR_I16,                                     \
        .flags = (uint16_t)(SC_PARAM_FLAG_READ_ONLY                           \
                            | SC_PARAM_FLAG_NOT_PERSISTED),                   \
        .schema_since = (uint16_t)(since),                                    \
        .as = { .scalar_i16 = {                                               \
            .value_offset = offsetof(struct_t, field),                        \
            .min_value = (int16_t)(min_v),                                    \
            .max_value = (int16_t)(max_v),                                    \
            .default_value = (int16_t)(default_v),                            \
        } },                                                                  \
    }

#ifdef __cplusplus
}
#endif

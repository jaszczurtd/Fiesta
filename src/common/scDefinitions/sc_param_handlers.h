#pragma once

/**
 * @file sc_param_handlers.h
 * @brief Generic SC reply machinery, accessors, and persistence helpers
 *        driven by an array of @ref sc_param_descriptor_t.
 *
 * The intent is that a module's SC unknown-handler delegates the four
 * read-side replies (@c GET_META, @c GET_PARAM_LIST, @c GET_VALUES,
 * @c GET_PARAM) to the helpers below, passing its descriptor array,
 * its values-struct pointer, and an @ref sc_emit_fn that wraps each
 * reply payload in a framed @c hal_serial_session_println — collapsing
 * ~70% copy&paste between ECU/Clocks/OilAndSpeed @c config.c[pp].
 *
 * Helpers do not depend on JaszczurHAL: replies are emitted via the
 * caller-supplied callback, so the same code paths drive on-target
 * firmware as well as host unit tests with a capture buffer.
 */

#include "sc_param_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reply-emit callback type.
 *
 * The helper writes one reply payload per call. The caller is
 * responsible for the SC frame envelope — typically by routing this
 * callback through @c hal_serial_session_println.
 */
typedef void (*sc_emit_fn)(const char *payload, void *user);

/* ── Lookup / accessors ──────────────────────────────────────────── */

/**
 * @brief Look up a descriptor by wire id.
 *
 * Returns NULL when @p id has no matching descriptor or any input is
 * NULL.
 */
const sc_param_descriptor_t *sc_param_find_by_id(
    const sc_param_descriptor_t *descs, size_t count, const char *id);

/** Return true when @p value is within the descriptor's [min, max]. */
bool sc_param_validate_range(const sc_param_descriptor_t *desc, int16_t value);

/**
 * @brief Read the int16 field at the descriptor's offset.
 *
 * Asserts SCALAR_I16 kind. Returns 0 when @p desc / @p values_ctx is
 * NULL or the kind tag is not SCALAR_I16.
 */
int16_t sc_param_get_i16(const sc_param_descriptor_t *desc,
                         const void *values_ctx);

/**
 * @brief Write the int16 field at the descriptor's offset.
 *
 * Returns false (and writes nothing) when:
 *   - @p desc or @p values_ctx is NULL,
 *   - @c kind is not SCALAR_I16,
 *   - the descriptor has @c SC_PARAM_FLAG_READ_ONLY,
 *   - @p value is out of [min, max].
 */
bool sc_param_set_i16(const sc_param_descriptor_t *desc, void *values_ctx,
                      int16_t value);

/**
 * @brief Apply default values to all writable scalar descriptors.
 *
 * Read-only descriptors are skipped (their values_ctx slot is left
 * untouched). Returns the number of fields written.
 */
size_t sc_param_load_defaults(const sc_param_descriptor_t *descs, size_t count,
                              void *values_ctx);

/* ── Reply emitters ─────────────────────────────────────────────── */

/**
 * @brief Emit "SC_OK PARAM_LIST <id1>,<id2>,...".
 *
 * Truncates silently if the joined line exceeds the helper's internal
 * buffer (@c 256 bytes) — same behaviour as the legacy ECU emitter.
 */
void sc_param_reply_get_param_list(const sc_param_descriptor_t *descs,
                                   size_t count, sc_emit_fn emit,
                                   void *emit_user);

/**
 * @brief Emit "SC_OK PARAM_VALUES id1=<v1> id2=<v2> ..." across SCALAR_I16
 *        descriptors in declaration order.
 */
void sc_param_reply_get_values_i16(const sc_param_descriptor_t *descs,
                                   size_t count, const void *values_ctx,
                                   sc_emit_fn emit, void *emit_user);

/**
 * @brief Emit either
 *        "SC_OK PARAM id=<id> value=... min=... max=... default=..."
 *        or
 *        "SC_INVALID_PARAM_ID id=<requested_id>".
 *
 * Caller is responsible for parsing the payload that follows
 * "SC_GET_PARAM " — empty, garbage, or oversize ids are bad-request
 * concerns handled at the call site.
 */
void sc_param_reply_get_param(const sc_param_descriptor_t *descs, size_t count,
                              const void *values_ctx, const char *requested_id,
                              sc_emit_fn emit, void *emit_user);

/* ── Persistence (schema-versioned, CRC32 trailer) ──────────────── */

/**
 * @brief Compute the bytes-on-the-wire size produced by
 *        @c sc_param_blob_encode for the given @p schema.
 *
 * Returns 0 when no descriptor qualifies (every descriptor is
 * NOT_PERSISTED or @c schema_since > @p schema), which the encoder
 * treats as a failure (no blob).
 */
size_t sc_param_blob_size_for_schema(const sc_param_descriptor_t *descs,
                                     size_t count, uint16_t schema);

/**
 * @brief Encode a schema-versioned blob with CRC32 trailer.
 *
 * Layout (little-endian throughout):
 *     [0..1]            schema (uint16_t LE)
 *     [2..N-5]          per-descriptor payload, declaration order:
 *                       SCALAR_I16 → 2 bytes (LE) when
 *                       @c flags & NOT_PERSISTED == 0 AND
 *                       @c schema_since <= @p schema; otherwise skipped
 *     [N-4..N-1]        CRC32 over [0..N-5], polynomial 0xEDB88320 (PKZIP),
 *                       initial 0xFFFFFFFF, final XOR 0xFFFFFFFF
 *
 * Returns the number of bytes written, or 0 on failure (NULL inputs,
 * insufficient @p out_buf_size, no qualifying descriptors).
 */
size_t sc_param_blob_encode(const sc_param_descriptor_t *descs, size_t count,
                            const void *values_ctx, uint16_t schema,
                            uint8_t *out_buf, size_t out_buf_size);

/**
 * @brief Decode a blob produced by @c sc_param_blob_encode.
 *
 * Validates CRC32, reads the schema, and writes only the descriptor
 * fields whose @c schema_since is <= the recovered schema. Fields
 * newer than the blob's schema retain whatever @p values_ctx carried
 * before the call (typically pre-loaded defaults via
 * @c sc_param_load_defaults).
 *
 * Returns false on:
 *   - NULL inputs,
 *   - buffer length not equal to the size predicted by
 *     @c sc_param_blob_size_for_schema for the recovered schema,
 *   - CRC mismatch.
 *
 * On success the recovered schema is optionally written to
 * @p out_schema (NULL is allowed).
 */
bool sc_param_blob_decode(const sc_param_descriptor_t *descs, size_t count,
                          void *values_ctx, const uint8_t *in_buf,
                          size_t in_buf_size, uint16_t *out_schema);

/* ── CRC32 utility ──────────────────────────────────────────────── */

/**
 * @brief PKZIP CRC32 (poly 0xEDB88320, init 0xFFFFFFFF, final XOR 0xFFFFFFFF).
 *
 * Exposed so module-level tests can pre-compute expected blob
 * trailers without re-implementing the algorithm.
 */
uint32_t sc_param_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

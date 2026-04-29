#include "sc_param_handlers.h"
#include "sc_protocol.h"

#include <stdio.h>
#include <string.h>

/* ── Internal helpers ─────────────────────────────────────────────── */

static int16_t *scalar_i16_lvalue(const sc_param_descriptor_t *desc,
                                  void *ctx) {
    return (int16_t *)((char *)ctx + desc->as.scalar_i16.value_offset);
}

static const int16_t *scalar_i16_rvalue(const sc_param_descriptor_t *desc,
                                        const void *ctx) {
    return (const int16_t *)((const char *)ctx + desc->as.scalar_i16.value_offset);
}

static void write_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool descriptor_persists_at_schema(const sc_param_descriptor_t *desc,
                                          uint16_t schema) {
    if (desc->kind != SC_PARAM_KIND_SCALAR_I16) {
        return false;
    }
    if ((desc->flags & SC_PARAM_FLAG_NOT_PERSISTED) != 0u) {
        return false;
    }
    if (desc->schema_since > schema) {
        return false;
    }
    return true;
}

/* ── CRC32 (PKZIP) ────────────────────────────────────────────────── */

uint32_t sc_param_crc32(const uint8_t *data, size_t len) {
    if (data == NULL) {
        return 0u;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < len; ++i) {
        crc ^= (uint32_t)data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/* ── Lookup / accessors ───────────────────────────────────────────── */

const sc_param_descriptor_t *sc_param_find_by_id(
    const sc_param_descriptor_t *descs, size_t count, const char *id) {
    if (descs == NULL || id == NULL) {
        return NULL;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (descs[i].id != NULL && strcmp(descs[i].id, id) == 0) {
            return &descs[i];
        }
    }
    return NULL;
}

bool sc_param_validate_range(const sc_param_descriptor_t *desc, int16_t value) {
    if (desc == NULL || desc->kind != SC_PARAM_KIND_SCALAR_I16) {
        return false;
    }
    return value >= desc->as.scalar_i16.min_value &&
           value <= desc->as.scalar_i16.max_value;
}

int16_t sc_param_get_i16(const sc_param_descriptor_t *desc,
                         const void *values_ctx) {
    if (desc == NULL || values_ctx == NULL ||
        desc->kind != SC_PARAM_KIND_SCALAR_I16) {
        return 0;
    }
    return *scalar_i16_rvalue(desc, values_ctx);
}

bool sc_param_set_i16(const sc_param_descriptor_t *desc, void *values_ctx,
                      int16_t value) {
    if (desc == NULL || values_ctx == NULL ||
        desc->kind != SC_PARAM_KIND_SCALAR_I16) {
        return false;
    }
    if ((desc->flags & SC_PARAM_FLAG_READ_ONLY) != 0u) {
        return false;
    }
    if (!sc_param_validate_range(desc, value)) {
        return false;
    }
    *scalar_i16_lvalue(desc, values_ctx) = value;
    return true;
}

size_t sc_param_load_defaults(const sc_param_descriptor_t *descs, size_t count,
                              void *values_ctx) {
    if (descs == NULL || values_ctx == NULL) {
        return 0u;
    }
    size_t written = 0u;
    for (size_t i = 0u; i < count; ++i) {
        const sc_param_descriptor_t *d = &descs[i];
        if (d->kind != SC_PARAM_KIND_SCALAR_I16) {
            continue;
        }
        if ((d->flags & SC_PARAM_FLAG_READ_ONLY) != 0u) {
            continue;
        }
        *scalar_i16_lvalue(d, values_ctx) = d->as.scalar_i16.default_value;
        ++written;
    }
    return written;
}

/* ── Reply emitters ───────────────────────────────────────────────── */

#define SC_PARAM_REPLY_BUF_BYTES 256u

/* Truncation sentinels appended when the response cannot fit every id.
 * `*` is not a valid param-id character on the host parser, so an
 * extra `,*` (PARAM_LIST) or ` *=*` (PARAM_VALUES) token unambiguously
 * signals truncation: the host's existing token-validation path sets
 * `parsed->truncated = true` when it sees the sentinel. */
#define SC_PARAM_TRUNC_MARK_LIST   ",*"
#define SC_PARAM_TRUNC_MARK_VALUES " *=*"

void sc_param_reply_get_param_list(const sc_param_descriptor_t *descs,
                                   size_t count, sc_emit_fn emit,
                                   void *emit_user) {
    if (emit == NULL) {
        return;
    }
    char response[SC_PARAM_REPLY_BUF_BYTES];
    const size_t mark_len = sizeof(SC_PARAM_TRUNC_MARK_LIST) - 1u;
    size_t used = (size_t)snprintf(response, sizeof(response), "%s",
                                   SC_REPLY_PARAM_LIST_HEAD);
    if (used >= sizeof(response)) {
        response[sizeof(response) - 1u] = '\0';
        emit(response, emit_user);
        return;
    }

    bool first = true;
    bool truncated = false;
    if (descs != NULL) {
        for (size_t i = 0u; i < count; ++i) {
            if (descs[i].id == NULL) {
                continue;
            }
            const char *sep = first ? " " : ",";
            const size_t budget = sizeof(response) - used;
            int written = snprintf(response + used, budget,
                                   "%s%s", sep, descs[i].id);
            if (written < 0) {
                truncated = true;
                break;
            }
            const size_t chunk = (size_t)written;
            /* Refuse to consume the bytes reserved for the trailing
             * truncation marker so the sentinel always fits when needed. */
            if (chunk + mark_len + 1u > budget) {
                response[used] = '\0';
                truncated = true;
                break;
            }
            used += chunk;
            first = false;
        }
    }
    if (truncated && (used + mark_len + 1u) <= sizeof(response)) {
        memcpy(response + used, SC_PARAM_TRUNC_MARK_LIST, mark_len + 1u);
        used += mark_len;
    }
    response[sizeof(response) - 1u] = '\0';
    emit(response, emit_user);
}

void sc_param_reply_get_values_i16(const sc_param_descriptor_t *descs,
                                   size_t count, const void *values_ctx,
                                   sc_emit_fn emit, void *emit_user) {
    if (emit == NULL) {
        return;
    }
    char response[SC_PARAM_REPLY_BUF_BYTES];
    const size_t mark_len = sizeof(SC_PARAM_TRUNC_MARK_VALUES) - 1u;
    size_t used = (size_t)snprintf(response, sizeof(response), "%s",
                                   SC_REPLY_PARAM_VALUES_HEAD);
    if (used >= sizeof(response)) {
        response[sizeof(response) - 1u] = '\0';
        emit(response, emit_user);
        return;
    }

    bool truncated = false;
    if (descs != NULL && values_ctx != NULL) {
        for (size_t i = 0u; i < count; ++i) {
            const sc_param_descriptor_t *d = &descs[i];
            if (d->kind != SC_PARAM_KIND_SCALAR_I16 || d->id == NULL) {
                continue;
            }
            const size_t budget = sizeof(response) - used;
            int written = snprintf(response + used, budget,
                                   " %s=%d", d->id,
                                   (int)*scalar_i16_rvalue(d, values_ctx));
            if (written < 0) {
                truncated = true;
                break;
            }
            const size_t chunk = (size_t)written;
            if (chunk + mark_len + 1u > budget) {
                response[used] = '\0';
                truncated = true;
                break;
            }
            used += chunk;
        }
    }
    if (truncated && (used + mark_len + 1u) <= sizeof(response)) {
        memcpy(response + used, SC_PARAM_TRUNC_MARK_VALUES, mark_len + 1u);
        used += mark_len;
    }
    response[sizeof(response) - 1u] = '\0';
    emit(response, emit_user);
}

void sc_param_reply_get_param(const sc_param_descriptor_t *descs, size_t count,
                              const void *values_ctx, const char *requested_id,
                              sc_emit_fn emit, void *emit_user) {
    if (emit == NULL) {
        return;
    }
    char response[SC_PARAM_REPLY_BUF_BYTES];

    const sc_param_descriptor_t *desc =
        sc_param_find_by_id(descs, count, requested_id);
    if (desc == NULL) {
        snprintf(response, sizeof(response), SC_REPLY_INVALID_PARAM_ID_FMT,
                 (requested_id != NULL) ? requested_id : "");
        emit(response, emit_user);
        return;
    }

    if (desc->kind != SC_PARAM_KIND_SCALAR_I16 || values_ctx == NULL) {
        emit(SC_STATUS_BAD_REQUEST, emit_user);
        return;
    }

    snprintf(response, sizeof(response), SC_REPLY_PARAM_FMT, desc->id,
             (int)*scalar_i16_rvalue(desc, values_ctx),
             (int)desc->as.scalar_i16.min_value,
             (int)desc->as.scalar_i16.max_value,
             (int)desc->as.scalar_i16.default_value);
    emit(response, emit_user);
}

/* ── Phase 8 — staging-mirror writes ──────────────────────────────── */

bool sc_param_reply_set_param(const sc_param_descriptor_t *descs, size_t count,
                              void *staging_ctx, const void *active_ctx,
                              const char *requested_id, int16_t value,
                              sc_emit_fn emit, void *emit_user) {
    if (emit == NULL) {
        return false;
    }
    char response[SC_PARAM_REPLY_BUF_BYTES];

    const sc_param_descriptor_t *desc =
        sc_param_find_by_id(descs, count, requested_id);
    if (desc == NULL) {
        snprintf(response, sizeof(response), SC_REPLY_INVALID_PARAM_ID_FMT,
                 (requested_id != NULL) ? requested_id : "");
        emit(response, emit_user);
        return false;
    }

    if (desc->kind != SC_PARAM_KIND_SCALAR_I16 || staging_ctx == NULL) {
        emit(SC_STATUS_BAD_REQUEST, emit_user);
        return false;
    }

    if ((desc->flags & SC_PARAM_FLAG_READ_ONLY) != 0u) {
        snprintf(response, sizeof(response),
                 SC_REPLY_BAD_REQUEST_READ_ONLY_FMT, desc->id);
        emit(response, emit_user);
        return false;
    }

    if (!sc_param_validate_range(desc, value)) {
        snprintf(response, sizeof(response),
                 SC_REPLY_BAD_REQUEST_OUT_OF_RANGE_FMT, desc->id,
                 (int)desc->as.scalar_i16.min_value,
                 (int)desc->as.scalar_i16.max_value);
        emit(response, emit_user);
        return false;
    }

    *scalar_i16_lvalue(desc, staging_ctx) = value;

    const int16_t active_val =
        (active_ctx != NULL) ? *scalar_i16_rvalue(desc, active_ctx) : value;

    snprintf(response, sizeof(response), SC_REPLY_PARAM_SET_FMT, desc->id,
             (int)value, (int)active_val);
    emit(response, emit_user);
    return true;
}

static size_t copy_writable_scalars(const sc_param_descriptor_t *descs,
                                    size_t count, const void *src_ctx,
                                    void *dst_ctx) {
    if (descs == NULL || src_ctx == NULL || dst_ctx == NULL) {
        return 0u;
    }
    size_t copied = 0u;
    for (size_t i = 0u; i < count; ++i) {
        const sc_param_descriptor_t *d = &descs[i];
        if (d->kind != SC_PARAM_KIND_SCALAR_I16) {
            continue;
        }
        if ((d->flags & SC_PARAM_FLAG_READ_ONLY) != 0u) {
            continue;
        }
        *scalar_i16_lvalue(d, dst_ctx) = *scalar_i16_rvalue(d, src_ctx);
        ++copied;
    }
    return copied;
}

size_t sc_param_copy_active_to_staging(const sc_param_descriptor_t *descs,
                                       size_t count, const void *active_ctx,
                                       void *staging_ctx) {
    return copy_writable_scalars(descs, count, active_ctx, staging_ctx);
}

size_t sc_param_copy_staging_to_active(const sc_param_descriptor_t *descs,
                                       size_t count, const void *staging_ctx,
                                       void *active_ctx) {
    return copy_writable_scalars(descs, count, staging_ctx, active_ctx);
}

/* ── Persistence ──────────────────────────────────────────────────── */

size_t sc_param_blob_size_for_schema(const sc_param_descriptor_t *descs,
                                     size_t count, uint16_t schema) {
    if (descs == NULL) {
        return 0u;
    }
    size_t bytes = 2u; /* schema header */
    size_t included = 0u;
    for (size_t i = 0u; i < count; ++i) {
        if (!descriptor_persists_at_schema(&descs[i], schema)) {
            continue;
        }
        bytes += 2u; /* SCALAR_I16 -> 2 bytes LE */
        ++included;
    }
    if (included == 0u) {
        return 0u;
    }
    bytes += 4u; /* CRC32 trailer */
    return bytes;
}

size_t sc_param_blob_encode(const sc_param_descriptor_t *descs, size_t count,
                            const void *values_ctx, uint16_t schema,
                            uint8_t *out_buf, size_t out_buf_size) {
    if (descs == NULL || values_ctx == NULL || out_buf == NULL) {
        return 0u;
    }
    const size_t total = sc_param_blob_size_for_schema(descs, count, schema);
    if (total == 0u || out_buf_size < total) {
        return 0u;
    }

    write_u16_le(&out_buf[0], schema);
    size_t off = 2u;
    for (size_t i = 0u; i < count; ++i) {
        const sc_param_descriptor_t *d = &descs[i];
        if (!descriptor_persists_at_schema(d, schema)) {
            continue;
        }
        const int16_t val = *scalar_i16_rvalue(d, values_ctx);
        write_u16_le(&out_buf[off], (uint16_t)val);
        off += 2u;
    }

    const uint32_t crc = sc_param_crc32(out_buf, off);
    write_u32_le(&out_buf[off], crc);
    return total;
}

bool sc_param_blob_decode(const sc_param_descriptor_t *descs, size_t count,
                          void *values_ctx, const uint8_t *in_buf,
                          size_t in_buf_size, uint16_t *out_schema) {
    if (descs == NULL || values_ctx == NULL || in_buf == NULL) {
        return false;
    }
    if (in_buf_size < 6u) {
        /* Smallest possible blob would be schema(2) + zero fields + CRC(4),
         * but blob_size_for_schema requires at least one persisted field, so
         * 6 is below any valid encoding produced by sc_param_blob_encode. */
        return false;
    }

    const uint16_t schema = read_u16_le(&in_buf[0]);
    const size_t expected = sc_param_blob_size_for_schema(descs, count, schema);
    if (expected == 0u || in_buf_size != expected) {
        return false;
    }

    const uint32_t expected_crc = read_u32_le(&in_buf[expected - 4u]);
    const uint32_t actual_crc = sc_param_crc32(in_buf, expected - 4u);
    if (expected_crc != actual_crc) {
        return false;
    }

    size_t off = 2u;
    for (size_t i = 0u; i < count; ++i) {
        const sc_param_descriptor_t *d = &descs[i];
        if (!descriptor_persists_at_schema(d, schema)) {
            continue;
        }
        const uint16_t raw = read_u16_le(&in_buf[off]);
        *scalar_i16_lvalue(d, values_ctx) = (int16_t)raw;
        off += 2u;
    }

    if (out_schema != NULL) {
        *out_schema = schema;
    }
    return true;
}

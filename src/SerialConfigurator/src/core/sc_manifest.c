#include "sc_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SC_MANIFEST_MAX_JSON_SIZE   (64u * 1024u)
#define SC_MANIFEST_MAX_ARTIFACT_SIZE (8u * 1024u * 1024u)

const char *sc_manifest_status_str(sc_manifest_status_t status)
{
    switch (status) {
    case SC_MANIFEST_OK:                          return "OK";
    case SC_MANIFEST_ERR_NULL_ARG:                return "NULL_ARG";
    case SC_MANIFEST_ERR_BAD_JSON:                return "BAD_JSON";
    case SC_MANIFEST_ERR_MISSING_FIELD:           return "MISSING_FIELD";
    case SC_MANIFEST_ERR_DUPLICATE_FIELD:         return "DUPLICATE_FIELD";
    case SC_MANIFEST_ERR_FIELD_TOO_LONG:          return "FIELD_TOO_LONG";
    case SC_MANIFEST_ERR_FIELD_EMPTY:             return "FIELD_EMPTY";
    case SC_MANIFEST_ERR_BAD_SHA256_FORMAT:       return "BAD_SHA256_FORMAT";
    case SC_MANIFEST_ERR_UNKNOWN_FIELD:           return "UNKNOWN_FIELD";
    case SC_MANIFEST_ERR_FILE_OPEN:               return "FILE_OPEN";
    case SC_MANIFEST_ERR_FILE_READ:               return "FILE_READ";
    case SC_MANIFEST_ERR_FILE_TOO_LARGE:          return "FILE_TOO_LARGE";
    case SC_MANIFEST_ERR_HASH_BACKEND:            return "HASH_BACKEND";
    case SC_MANIFEST_ERR_ARTIFACT_HASH_MISMATCH:  return "ARTIFACT_HASH_MISMATCH";
    case SC_MANIFEST_ERR_MODULE_MISMATCH:         return "MODULE_MISMATCH";
    case SC_MANIFEST_ERR_SIGNATURE_NOT_SUPPORTED: return "SIGNATURE_NOT_SUPPORTED";
    }
    return "UNKNOWN";
}

/* ── tiny JSON reader ────────────────────────────────────────────────────── */

typedef struct {
    const char *p;
    const char *end;
} sc_json_cursor_t;

static void skip_ws(sc_json_cursor_t *c)
{
    while (c->p < c->end) {
        const char ch = *c->p;
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            c->p++;
        } else {
            return;
        }
    }
}

static bool consume_char(sc_json_cursor_t *c, char expected)
{
    skip_ws(c);
    if (c->p >= c->end || *c->p != expected) {
        return false;
    }
    c->p++;
    return true;
}

/*
 * Parse a JSON string token starting at the opening quote. On success the
 * cursor advances past the closing quote and the payload is written to
 * @p out (NUL-terminated). Out-buffer overflow yields false; callers map
 * that to FIELD_TOO_LONG.
 *
 * Supported escape sequences: \\ \" \/ \n \r \t. Any other escape is a
 * parse error. \uXXXX and unicode handling are intentionally unsupported.
 */
static bool parse_string(sc_json_cursor_t *c, char *out, size_t out_size,
                         size_t *out_len)
{
    if (out_size == 0u) {
        return false;
    }
    if (!consume_char(c, '"')) {
        return false;
    }
    size_t written = 0u;
    while (c->p < c->end) {
        char ch = *c->p++;
        if (ch == '"') {
            if (written + 1u > out_size) {
                return false;
            }
            out[written] = '\0';
            if (out_len != NULL) {
                *out_len = written;
            }
            return true;
        }
        if (ch == '\\') {
            if (c->p >= c->end) {
                return false;
            }
            const char esc = *c->p++;
            switch (esc) {
            case '"':  ch = '"'; break;
            case '\\': ch = '\\'; break;
            case '/':  ch = '/'; break;
            case 'n':  ch = '\n'; break;
            case 'r':  ch = '\r'; break;
            case 't':  ch = '\t'; break;
            default: return false;
            }
        } else if ((unsigned char)ch < 0x20u) {
            /* Raw control characters are not valid in JSON strings. */
            return false;
        }
        if (written + 1u >= out_size) {
            return false;
        }
        out[written++] = ch;
    }
    return false; /* unterminated string */
}

/* ── manifest field parsing ──────────────────────────────────────────────── */

static bool is_lower_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static bool decode_lower_hex(const char *hex, uint8_t *out, size_t out_len)
{
    for (size_t i = 0u; i < out_len; ++i) {
        const char hi = hex[i * 2u];
        const char lo = hex[i * 2u + 1u];
        if (!is_lower_hex(hi) || !is_lower_hex(lo)) {
            return false;
        }
        const uint8_t hi_v = (uint8_t)((hi <= '9') ? (hi - '0') : (10 + (hi - 'a')));
        const uint8_t lo_v = (uint8_t)((lo <= '9') ? (lo - '0') : (10 + (lo - 'a')));
        out[i] = (uint8_t)((hi_v << 4) | lo_v);
    }
    return true;
}

typedef struct {
    bool seen_module_name;
    bool seen_fw_version;
    bool seen_build_id;
    bool seen_sha256;
    bool seen_signature;
} sc_manifest_seen_t;

static sc_manifest_status_t store_field(sc_manifest_t *out,
                                        sc_manifest_seen_t *seen,
                                        const char *key,
                                        size_t key_len,
                                        sc_json_cursor_t *c)
{
    char value_buf[SC_MANIFEST_SIGNATURE_MAX + 1u];
    size_t value_len = 0u;
    if (!parse_string(c, value_buf, sizeof(value_buf), &value_len)) {
        /* Could be malformed JSON or the value didn't fit in the largest
         * accepted field buffer (signature). The caller distinguishes
         * empty-string from too-long below where it has the actual cap. */
        return SC_MANIFEST_ERR_BAD_JSON;
    }

    char *dst = NULL;
    size_t dst_cap = 0u;
    bool *seen_flag = NULL;

    if (key_len == 11u && memcmp(key, "module_name", 11u) == 0) {
        dst = out->module_name;
        dst_cap = SC_MANIFEST_MODULE_NAME_MAX;
        seen_flag = &seen->seen_module_name;
    } else if (key_len == 10u && memcmp(key, "fw_version", 10u) == 0) {
        dst = out->fw_version;
        dst_cap = SC_MANIFEST_FW_VERSION_MAX;
        seen_flag = &seen->seen_fw_version;
    } else if (key_len == 8u && memcmp(key, "build_id", 8u) == 0) {
        dst = out->build_id;
        dst_cap = SC_MANIFEST_BUILD_ID_MAX;
        seen_flag = &seen->seen_build_id;
    } else if (key_len == 6u && memcmp(key, "sha256", 6u) == 0) {
        dst = out->sha256_hex;
        dst_cap = SC_MANIFEST_SHA256_HEX_LEN;
        seen_flag = &seen->seen_sha256;
    } else if (key_len == 9u && memcmp(key, "signature", 9u) == 0) {
        dst = out->signature;
        dst_cap = SC_MANIFEST_SIGNATURE_MAX;
        seen_flag = &seen->seen_signature;
    } else {
        return SC_MANIFEST_ERR_UNKNOWN_FIELD;
    }

    if (*seen_flag) {
        return SC_MANIFEST_ERR_DUPLICATE_FIELD;
    }
    *seen_flag = true;

    if (value_len > dst_cap) {
        return SC_MANIFEST_ERR_FIELD_TOO_LONG;
    }
    if (value_len == 0u) {
        return SC_MANIFEST_ERR_FIELD_EMPTY;
    }
    memcpy(dst, value_buf, value_len);
    dst[value_len] = '\0';

    if (seen_flag == &seen->seen_sha256) {
        if (value_len != SC_MANIFEST_SHA256_HEX_LEN) {
            return SC_MANIFEST_ERR_BAD_SHA256_FORMAT;
        }
        if (!decode_lower_hex(out->sha256_hex, out->sha256,
                              SC_CRYPTO_SHA256_DIGEST_BYTES)) {
            return SC_MANIFEST_ERR_BAD_SHA256_FORMAT;
        }
    }

    return SC_MANIFEST_OK;
}

sc_manifest_status_t sc_manifest_parse(const char *json,
                                       size_t json_len,
                                       sc_manifest_t *out)
{
    if (json == NULL || out == NULL) {
        return SC_MANIFEST_ERR_NULL_ARG;
    }
    memset(out, 0, sizeof(*out));

    sc_json_cursor_t c = { json, json + json_len };
    sc_manifest_seen_t seen = { false, false, false, false, false };

    if (!consume_char(&c, '{')) {
        return SC_MANIFEST_ERR_BAD_JSON;
    }
    skip_ws(&c);

    if (c.p < c.end && *c.p == '}') {
        c.p++;
        return SC_MANIFEST_ERR_MISSING_FIELD;
    }

    bool first = true;
    while (true) {
        skip_ws(&c);
        if (!first) {
            if (!consume_char(&c, ',')) {
                if (consume_char(&c, '}')) {
                    break;
                }
                return SC_MANIFEST_ERR_BAD_JSON;
            }
            skip_ws(&c);
        }
        first = false;

        char key_buf[32];
        size_t key_len = 0u;
        if (!parse_string(&c, key_buf, sizeof(key_buf), &key_len)) {
            return SC_MANIFEST_ERR_BAD_JSON;
        }
        if (!consume_char(&c, ':')) {
            return SC_MANIFEST_ERR_BAD_JSON;
        }
        skip_ws(&c);

        const sc_manifest_status_t st = store_field(out, &seen, key_buf,
                                                    key_len, &c);
        if (st != SC_MANIFEST_OK) {
            return st;
        }

        skip_ws(&c);
        if (c.p < c.end && *c.p == '}') {
            c.p++;
            break;
        }
    }

    /* Trailing whitespace is OK; anything else after the closing brace is
     * a parse error so we don't silently swallow garbage. */
    skip_ws(&c);
    if (c.p != c.end) {
        return SC_MANIFEST_ERR_BAD_JSON;
    }

    if (!seen.seen_module_name || !seen.seen_fw_version ||
        !seen.seen_build_id || !seen.seen_sha256) {
        return SC_MANIFEST_ERR_MISSING_FIELD;
    }
    out->has_signature = seen.seen_signature;

    return SC_MANIFEST_OK;
}

/* ── file IO helpers ─────────────────────────────────────────────────────── */

static sc_manifest_status_t read_file_bounded(const char *path,
                                              size_t max_size,
                                              uint8_t **out_buf,
                                              size_t *out_size,
                                              sc_manifest_status_t too_large)
{
    *out_buf = NULL;
    *out_size = 0u;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return SC_MANIFEST_ERR_FILE_OPEN;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        (void)fclose(f);
        return SC_MANIFEST_ERR_FILE_READ;
    }
    const long len = ftell(f);
    if (len < 0) {
        (void)fclose(f);
        return SC_MANIFEST_ERR_FILE_READ;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        (void)fclose(f);
        return SC_MANIFEST_ERR_FILE_READ;
    }
    if ((size_t)len > max_size) {
        (void)fclose(f);
        return too_large;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)len + 1u);
    if (buf == NULL) {
        (void)fclose(f);
        return SC_MANIFEST_ERR_FILE_READ;
    }
    const size_t n = fread(buf, 1u, (size_t)len, f);
    (void)fclose(f);
    if (n != (size_t)len) {
        free(buf);
        return SC_MANIFEST_ERR_FILE_READ;
    }
    buf[len] = '\0'; /* convenient for debug printing of JSON, harmless for binary. */

    *out_buf = buf;
    *out_size = (size_t)len;
    return SC_MANIFEST_OK;
}

sc_manifest_status_t sc_manifest_load_file(const char *path,
                                           sc_manifest_t *out)
{
    if (path == NULL || out == NULL) {
        return SC_MANIFEST_ERR_NULL_ARG;
    }
    uint8_t *buf = NULL;
    size_t size = 0u;
    const sc_manifest_status_t io = read_file_bounded(path,
                                                      SC_MANIFEST_MAX_JSON_SIZE,
                                                      &buf,
                                                      &size,
                                                      SC_MANIFEST_ERR_FILE_TOO_LARGE);
    if (io != SC_MANIFEST_OK) {
        return io;
    }
    const sc_manifest_status_t st = sc_manifest_parse((const char *)buf, size, out);
    free(buf);
    return st;
}

sc_manifest_status_t sc_manifest_verify_artifact(
    const sc_manifest_t *manifest,
    const char *artifact_path)
{
    if (manifest == NULL || artifact_path == NULL) {
        return SC_MANIFEST_ERR_NULL_ARG;
    }

    uint8_t *buf = NULL;
    size_t size = 0u;
    const sc_manifest_status_t io = read_file_bounded(artifact_path,
                                                      SC_MANIFEST_MAX_ARTIFACT_SIZE,
                                                      &buf,
                                                      &size,
                                                      SC_MANIFEST_ERR_FILE_TOO_LARGE);
    if (io != SC_MANIFEST_OK) {
        return io;
    }

    uint8_t actual[SC_CRYPTO_SHA256_DIGEST_BYTES];
    const bool ok = sc_crypto_sha256(buf, size, actual);
    free(buf);
    if (!ok) {
        return SC_MANIFEST_ERR_HASH_BACKEND;
    }

    /* Constant-time comparison — manifest hash is public, but staying
     * branch-free keeps a uniform pattern with the auth path. */
    uint8_t diff = 0u;
    for (size_t i = 0u; i < SC_CRYPTO_SHA256_DIGEST_BYTES; ++i) {
        diff = (uint8_t)(diff | (actual[i] ^ manifest->sha256[i]));
    }
    if (diff != 0u) {
        return SC_MANIFEST_ERR_ARTIFACT_HASH_MISMATCH;
    }
    return SC_MANIFEST_OK;
}

sc_manifest_status_t sc_manifest_check_module_match(
    const sc_manifest_t *manifest,
    const char *expected_module_name)
{
    if (manifest == NULL || expected_module_name == NULL) {
        return SC_MANIFEST_ERR_NULL_ARG;
    }
    if (strcmp(manifest->module_name, expected_module_name) != 0) {
        return SC_MANIFEST_ERR_MODULE_MISMATCH;
    }
    return SC_MANIFEST_OK;
}

sc_manifest_status_t sc_manifest_verify_signature(
    const sc_manifest_t *manifest)
{
    (void)manifest;
    /* Phase 4 ships the hook only; ed25519 backend lands later. */
    return SC_MANIFEST_ERR_SIGNATURE_NOT_SUPPORTED;
}

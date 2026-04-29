#include "sc_core.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sc_auth.h>
#include <sc_crypto.h>
#include "sc_flash.h"
#include "sc_manifest.h"
#include "sc_protocol.h"
#include "sc_transport.h"
#include "../../common/scDefinitions/sc_fiesta_module_tokens.h"

typedef struct ScModuleDef {
    const char *token;
    const char *display_name;
} ScModuleDef;

static const ScModuleDef k_module_defs[SC_MODULE_COUNT] = {
    { SC_MODULE_TOKEN_ECU, SC_MODULE_ECU },
    { SC_MODULE_TOKEN_CLOCKS, SC_MODULE_CLOCKS },
    { SC_MODULE_TOKEN_OIL_AND_SPEED, SC_MODULE_OIL_AND_SPEED },
};

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == 0 || dst_size == 0u) {
        return;
    }

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    /* Bounded copy without snprintf("%s", ...) which trips
     * -Werror=format-truncation when GCC cannot prove src fits. */
    const size_t src_len = strlen(src);
    const size_t copy_len = (src_len < (dst_size - 1u)) ? src_len : (dst_size - 1u);
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static void copy_span(
    char *dst,
    size_t dst_size,
    const char *start,
    const char *end
)
{
    if (dst == 0 || dst_size == 0u) {
        return;
    }

    if (start == 0 || end == 0 || end <= start) {
        dst[0] = '\0';
        return;
    }

    size_t len = (size_t)(end - start);
    if (len >= dst_size) {
        len = dst_size - 1u;
    }

    (void)memcpy(dst, start, len);
    dst[len] = '\0';
}

static void log_append(char *log_output, size_t log_output_size, const char *format, ...)
{
    if (log_output == 0 || log_output_size == 0u || format == 0) {
        return;
    }

    const size_t current_len = strlen(log_output);
    if (current_len >= log_output_size - 1u) {
        return;
    }

    va_list args;
    va_start(args, format);
    (void)vsnprintf(
        log_output + current_len,
        log_output_size - current_len,
        format,
        args
    );
    va_end(args);
}

static void identity_reset(ScIdentityData *identity)
{
    if (identity == 0) {
        return;
    }

    identity->valid = false;
    identity->module_name[0] = '\0';
    identity->proto_version = 0;
    identity->proto_present = false;
    identity->session_id = 0u;
    identity->session_present = false;
    identity->fw_version[0] = '\0';
    identity->build_id[0] = '\0';
    identity->uid[0] = '\0';
}

static bool parse_int_strict(const char *text, int *value)
{
    if (text == 0 || value == 0 || text[0] == '\0') {
        return false;
    }

    errno = 0;
    char *end = 0;
    const long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }

    *value = (int)parsed;
    return true;
}

static bool parse_u32_strict(const char *text, uint32_t *value)
{
    if (text == 0 || value == 0 || text[0] == '\0') {
        return false;
    }

    errno = 0;
    char *end = 0;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool text_is_printable_ascii(const char *text, size_t len)
{
    if (text == 0) {
        return false;
    }

    for (size_t i = 0u; i < len; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c < 0x20u || c > 0x7Eu) {
            return false;
        }
    }

    return true;
}

static bool is_identity_separator_char(char c)
{
    return c == ' ' || c == ';' || c == '\t';
}

static bool value_looks_like_base64_payload(const char *value)
{
    if (value == 0) {
        return false;
    }

    const size_t len = strlen(value);
    if (len < 16u) {
        return false;
    }

    size_t trailing_padding = 0u;
    while (trailing_padding < len && value[len - 1u - trailing_padding] == '=') {
        trailing_padding++;
    }
    if (trailing_padding > 2u) {
        return false;
    }

    const size_t payload_len = len - trailing_padding;
    for (size_t i = 0u; i < len; ++i) {
        const char c = value[i];
        const bool ok =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '+' || c == '/' || c == '=';
        if (!ok) {
            return false;
        }

        if (c == '=' && i < payload_len) {
            return false;
        }
    }

    /*
     * Accept unpadded Base64 variants (len%4 == 2 or 3), because some devices
     * sporadically lose trailing '=' padding on noisy links.
     */
    if ((payload_len % 4u) == 1u) {
        return false;
    }

    return true;
}

static bool decode_build_base64_relaxed(const char *value, char *decoded, size_t decoded_size)
{
    if (value == 0 || decoded == 0 || decoded_size < 2u) {
        return false;
    }

    size_t value_len = strlen(value);
    if (value_len == 0u || value_len + 3u >= SC_IDENTITY_FIELD_MAX) {
        return false;
    }

    char normalized[SC_IDENTITY_FIELD_MAX];
    copy_string(normalized, sizeof(normalized), value);

    size_t trailing_padding = 0u;
    while (trailing_padding < value_len && normalized[value_len - 1u - trailing_padding] == '=') {
        trailing_padding++;
    }
    if (trailing_padding > 2u) {
        return false;
    }

    const size_t payload_len = value_len - trailing_padding;
    if ((payload_len % 4u) == 1u) {
        return false;
    }

    size_t normalized_len = value_len;
    if (trailing_padding == 0u) {
        const size_t rem = payload_len % 4u;
        const size_t add_padding = (rem == 0u) ? 0u : (4u - rem);
        if (add_padding > 0u) {
            if (normalized_len + add_padding >= sizeof(normalized)) {
                return false;
            }

            for (size_t i = 0u; i < add_padding; ++i) {
                normalized[normalized_len++] = '=';
            }
            normalized[normalized_len] = '\0';
        }
    }

    memset(decoded, 0, decoded_size);
    size_t decoded_len = 0u;
    const bool decoded_ok = sc_crypto_base64_decode(
        normalized,
        normalized_len,
        (uint8_t *)decoded,
        decoded_size - 1u,
        &decoded_len
    );
    if (!decoded_ok || decoded_len == 0u || !text_is_printable_ascii(decoded, decoded_len)) {
        return false;
    }

    return true;
}

static void identity_set_field(ScIdentityData *identity, const char *key, const char *value)
{
    if (identity == 0 || key == 0 || value == 0) {
        return;
    }

    if (strcmp(key, "module") == 0) {
        copy_string(identity->module_name, sizeof(identity->module_name), value);
        return;
    }

    if (strcmp(key, "proto") == 0) {
        int proto = 0;
        if (parse_int_strict(value, &proto)) {
            identity->proto_version = proto;
            identity->proto_present = true;
        }
        return;
    }

    if (strcmp(key, "session") == 0) {
        uint32_t session_id = 0u;
        if (parse_u32_strict(value, &session_id)) {
            identity->session_id = session_id;
            identity->session_present = true;
        }
        return;
    }

    if (strcmp(key, "fw") == 0) {
        copy_string(identity->fw_version, sizeof(identity->fw_version), value);
        return;
    }

    if (strcmp(key, "build") == 0) {
        char decoded[SC_IDENTITY_FIELD_MAX];
        if (decode_build_base64_relaxed(value, decoded, sizeof(decoded))) {
            copy_string(identity->build_id, sizeof(identity->build_id), decoded);
        } else if (value_looks_like_base64_payload(value)) {
            /*
             * Encoded build payload appears corrupted/truncated.
             * Keep empty here so upper layers can fall back to HELLO identity.
             */
            identity->build_id[0] = '\0';
        } else {
            /* Compatibility fallback: some firmware may send raw build text. */
            copy_string(identity->build_id, sizeof(identity->build_id), value);
        }
        return;
    }

    if (strcmp(key, "uid") == 0) {
        copy_string(identity->uid, sizeof(identity->uid), value);
    }
}

static void parse_identity_fields(const char *response, ScIdentityData *identity)
{
    if (identity == 0) {
        return;
    }

    identity_reset(identity);
    if (response == 0 || response[0] == '\0') {
        return;
    }

    static const char *k_fields[] = { "module", "proto", "session", "fw", "build", "uid" };
    const size_t field_count = sizeof(k_fields) / sizeof(k_fields[0]);

    const char *response_end = response + strlen(response);
    for (size_t fi = 0u; fi < field_count; ++fi) {
        const char *key = k_fields[fi];
        const size_t key_len = strlen(key);
        const bool is_build = strcmp(key, "build") == 0;

        const char *field_start = 0;
        const char *value_start = 0;

        for (const char *p = response; p[0] != '\0'; ++p) {
            const bool boundary = (p == response) || is_identity_separator_char(p[-1]);
            if (!boundary) {
                continue;
            }

            if (strncmp(p, key, key_len) != 0) {
                continue;
            }

            if (p[key_len] == '=') {
                field_start = p;
                value_start = p + key_len + 1u;
                break;
            }

            if (is_build && p[key_len] != '\0' && !is_identity_separator_char(p[key_len])) {
                /* Firmware compatibility: accept malformed "build<value>" token. */
                field_start = p;
                value_start = p + key_len;
                while (value_start[0] == '=' || value_start[0] == ':') {
                    value_start++;
                }
                if (value_start[0] == '\0') {
                    field_start = 0;
                    value_start = 0;
                }
                break;
            }
        }

        if (field_start == 0 || value_start == 0 || value_start[0] == '\0') {
            continue;
        }

        const char *value_end = response_end;
        for (const char *scan = value_start + 1u; scan[0] != '\0'; ++scan) {
            if (!is_identity_separator_char(scan[0])) {
                continue;
            }

            const char *candidate = scan + 1u;
            for (size_t next_i = 0u; next_i < field_count; ++next_i) {
                const char *next_key = k_fields[next_i];
                const size_t next_key_len = strlen(next_key);
                const bool next_is_build = strcmp(next_key, "build") == 0;

                if (strncmp(candidate, next_key, next_key_len) != 0) {
                    continue;
                }

                if (candidate[next_key_len] == '=' ||
                    (next_is_build &&
                     candidate[next_key_len] != '\0' &&
                     !is_identity_separator_char(candidate[next_key_len]))) {
                    value_end = scan;
                    goto found_value_end;
                }
            }
        }

found_value_end:
        while (value_end > value_start && is_identity_separator_char(value_end[-1])) {
            value_end--;
        }

        char value[SC_IDENTITY_FIELD_MAX];
        copy_span(value, sizeof(value), value_start, value_end);
        identity_set_field(identity, key, value);
    }

    identity->valid = identity->module_name[0] != '\0';
}

static bool parse_hello_identity(const char *response, ScIdentityData *identity)
{
    if (response == 0 || identity == 0 ||
        strncmp(response, SC_REPLY_HELLO_HEAD,
                sizeof(SC_REPLY_HELLO_HEAD) - 1u) != 0) {
        identity_reset(identity);
        return false;
    }

    parse_identity_fields(response, identity);
    return identity->valid;
}

static bool parse_meta_identity(const char *response, ScIdentityData *identity)
{
    static const char k_meta_head[] = SC_STATUS_OK " " SC_REPLY_TAG_META;
    if (response == 0 || identity == 0 ||
        strncmp(response, k_meta_head, sizeof(k_meta_head) - 1u) != 0) {
        identity_reset(identity);
        return false;
    }

    parse_identity_fields(response, identity);
    return identity->valid;
}

static int module_index_from_token(const char *token)
{
    if (token == 0) {
        return -1;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        if (strcmp(token, k_module_defs[i].token) == 0) {
            return (int)i;
        }
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        if (strcmp(token, k_module_defs[i].display_name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static void command_result_reset(ScCommandResult *result)
{
    if (result == 0) {
        return;
    }

    result->status = SC_COMMAND_STATUS_UNPARSEABLE;
    result->status_token[0] = '\0';
    result->topic[0] = '\0';
    result->details[0] = '\0';
    result->response[0] = '\0';
}

static const char *skip_spaces(const char *cursor)
{
    if (cursor == 0) {
        return 0;
    }

    while (cursor[0] == ' ') {
        cursor++;
    }

    return cursor;
}

static const char *token_end(const char *cursor)
{
    if (cursor == 0) {
        return 0;
    }

    while (cursor[0] != '\0' && cursor[0] != ' ') {
        cursor++;
    }

    return cursor;
}

static ScCommandStatus command_status_from_token(const char *token)
{
    if (token == 0 || token[0] == '\0') {
        return SC_COMMAND_STATUS_UNPARSEABLE;
    }

    if (strcmp(token, SC_STATUS_OK) == 0) {
        return SC_COMMAND_STATUS_OK;
    }
    if (strcmp(token, SC_STATUS_UNKNOWN_CMD) == 0) {
        return SC_COMMAND_STATUS_UNKNOWN_CMD;
    }
    if (strcmp(token, SC_STATUS_BAD_REQUEST) == 0) {
        return SC_COMMAND_STATUS_BAD_REQUEST;
    }
    if (strcmp(token, SC_STATUS_NOT_READY) == 0) {
        return SC_COMMAND_STATUS_NOT_READY;
    }
    if (strcmp(token, SC_STATUS_INVALID_PARAM_ID) == 0) {
        return SC_COMMAND_STATUS_INVALID_PARAM_ID;
    }

    /* Generic SC_* prefix: any unrecognised token whose name starts with
     * SC_ is treated as an "other" SC status so the host can carry it
     * forward verbatim instead of dropping the response. The "SC_" string
     * here is the protocol prefix shared by every status, not a vocabulary
     * token in its own right. */
    if (strncmp(token, "SC_", 3) == 0) {
        return SC_COMMAND_STATUS_OTHER;
    }

    return SC_COMMAND_STATUS_UNPARSEABLE;
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error == 0 || error_size == 0u) {
        return;
    }

    copy_string(error, error_size, message != 0 ? message : "");
}

static bool param_id_char_is_valid(char c)
{
    const unsigned char uc = (unsigned char)c;
    return (uc >= (unsigned char)'a' && uc <= (unsigned char)'z') ||
        (uc >= (unsigned char)'A' && uc <= (unsigned char)'Z') ||
        (uc >= (unsigned char)'0' && uc <= (unsigned char)'9') ||
        c == '_' || c == '-' || c == '.';
}

static bool param_id_is_valid(const char *id)
{
    if (id == 0 || id[0] == '\0') {
        return false;
    }

    for (size_t i = 0u; id[i] != '\0'; ++i) {
        if (!param_id_char_is_valid(id[i])) {
            return false;
        }
    }

    return true;
}

static bool param_list_separator_char(char c)
{
    return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool strings_equal_case_insensitive(const char *a, const char *b)
{
    if (a == 0 || b == 0) {
        return false;
    }

    size_t i = 0u;
    while (a[i] != '\0' && b[i] != '\0') {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
        i++;
    }

    return a[i] == '\0' && b[i] == '\0';
}

static bool parse_i64_strict(const char *text, int64_t *value)
{
    if (text == 0 || value == 0 || text[0] == '\0') {
        return false;
    }

    errno = 0;
    char *end = 0;
    const long long parsed = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }

    *value = (int64_t)parsed;
    return true;
}

static bool parse_u64_strict(const char *text, uint64_t *value)
{
    if (text == 0 || value == 0 || text[0] == '\0') {
        return false;
    }

    if (text[0] == '-') {
        return false;
    }

    errno = 0;
    char *end = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }

    *value = (uint64_t)parsed;
    return true;
}

static bool parse_double_strict(const char *text, double *value)
{
    if (text == 0 || value == 0 || text[0] == '\0') {
        return false;
    }

    errno = 0;
    char *end = 0;
    const double parsed = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }

    *value = parsed;
    return true;
}

static bool text_maybe_float(const char *text)
{
    if (text == 0 || text[0] == '\0') {
        return false;
    }

    for (size_t i = 0u; text[i] != '\0'; ++i) {
        const char c = text[i];
        if (c == '.' || c == 'e' || c == 'E') {
            return true;
        }
    }

    return false;
}

static void typed_value_reset(ScTypedValue *value)
{
    if (value == 0) {
        return;
    }

    value->type = SC_VALUE_TYPE_UNKNOWN;
    value->bool_value = false;
    value->int_value = 0;
    value->uint_value = 0u;
    value->float_value = 0.0;
    value->raw[0] = '\0';
}

static bool typed_value_is_numeric(const ScTypedValue *value)
{
    if (value == 0) {
        return false;
    }

    return value->type == SC_VALUE_TYPE_INT ||
        value->type == SC_VALUE_TYPE_UINT ||
        value->type == SC_VALUE_TYPE_FLOAT ||
        value->type == SC_VALUE_TYPE_BOOL;
}

static double typed_value_as_double(const ScTypedValue *value)
{
    if (value == 0) {
        return 0.0;
    }

    switch (value->type) {
        case SC_VALUE_TYPE_BOOL:
            return value->bool_value ? 1.0 : 0.0;
        case SC_VALUE_TYPE_INT:
            return (double)value->int_value;
        case SC_VALUE_TYPE_UINT:
            return (double)value->uint_value;
        case SC_VALUE_TYPE_FLOAT:
            return value->float_value;
        default:
            return 0.0;
    }
}

static void typed_value_from_text(const char *text, ScTypedValue *value)
{
    typed_value_reset(value);
    if (value == 0 || text == 0) {
        return;
    }

    copy_string(value->raw, sizeof(value->raw), text);

    if (strings_equal_case_insensitive(text, "true") ||
        strings_equal_case_insensitive(text, "on")) {
        value->type = SC_VALUE_TYPE_BOOL;
        value->bool_value = true;
        return;
    }

    if (strings_equal_case_insensitive(text, "false") ||
        strings_equal_case_insensitive(text, "off")) {
        value->type = SC_VALUE_TYPE_BOOL;
        value->bool_value = false;
        return;
    }

    if (text_maybe_float(text)) {
        double parsed = 0.0;
        if (parse_double_strict(text, &parsed)) {
            value->type = SC_VALUE_TYPE_FLOAT;
            value->float_value = parsed;
            return;
        }
    }

    int64_t signed_value = 0;
    if (parse_i64_strict(text, &signed_value)) {
        if (text[0] != '-' && text[0] != '+') {
            uint64_t unsigned_value = 0u;
            if (parse_u64_strict(text, &unsigned_value)) {
                value->type = SC_VALUE_TYPE_UINT;
                value->uint_value = unsigned_value;
                return;
            }
        }

        value->type = SC_VALUE_TYPE_INT;
        value->int_value = signed_value;
        return;
    }

    uint64_t unsigned_value = 0u;
    if (parse_u64_strict(text, &unsigned_value)) {
        value->type = SC_VALUE_TYPE_UINT;
        value->uint_value = unsigned_value;
        return;
    }

    value->type = SC_VALUE_TYPE_TEXT;
}

static void param_list_reset(ScParamListData *parsed)
{
    if (parsed == 0) {
        return;
    }

    parsed->count = 0u;
    parsed->truncated = false;
    for (size_t i = 0u; i < SC_PARAM_ITEMS_MAX; ++i) {
        parsed->ids[i][0] = '\0';
    }
}

static void param_values_reset(ScParamValuesData *parsed)
{
    if (parsed == 0) {
        return;
    }

    parsed->count = 0u;
    parsed->truncated = false;
    for (size_t i = 0u; i < SC_PARAM_ITEMS_MAX; ++i) {
        parsed->entries[i].id[0] = '\0';
        typed_value_reset(&parsed->entries[i].value);
    }
}

static void param_detail_reset(ScParamDetailData *parsed)
{
    if (parsed == 0) {
        return;
    }

    parsed->valid = false;
    parsed->id[0] = '\0';
    parsed->has_value = false;
    typed_value_reset(&parsed->value);
    parsed->has_min = false;
    typed_value_reset(&parsed->min);
    parsed->has_max = false;
    typed_value_reset(&parsed->max);
    parsed->has_default = false;
    typed_value_reset(&parsed->default_value);
}

static bool parse_next_token(
    const char **cursor,
    char *token,
    size_t token_size
)
{
    if (cursor == 0 || token == 0 || token_size == 0u || *cursor == 0) {
        return false;
    }

    const char *start = skip_spaces(*cursor);
    if (start == 0 || start[0] == '\0') {
        token[0] = '\0';
        *cursor = start;
        return false;
    }

    const char *end = token_end(start);
    copy_span(token, token_size, start, end);
    *cursor = end;
    return token[0] != '\0';
}

static void parse_sc_command_result(const char *response, ScCommandResult *result)
{
    if (result == 0) {
        return;
    }

    command_result_reset(result);
    copy_string(result->response, sizeof(result->response), response);

    if (response == 0 || response[0] == '\0') {
        return;
    }

    const char *cursor = skip_spaces(response);
    if (cursor == 0 || cursor[0] == '\0') {
        return;
    }

    const char *status_start = cursor;
    const char *status_end = token_end(status_start);
    copy_span(result->status_token, sizeof(result->status_token), status_start, status_end);
    result->status = command_status_from_token(result->status_token);

    cursor = skip_spaces(status_end);
    if (cursor == 0 || cursor[0] == '\0') {
        return;
    }

    const char *topic_start = cursor;
    const char *topic_end_ptr = token_end(topic_start);
    const bool has_equals = memchr(topic_start, '=', (size_t)(topic_end_ptr - topic_start)) != 0;
    if (!has_equals) {
        copy_span(result->topic, sizeof(result->topic), topic_start, topic_end_ptr);
        cursor = skip_spaces(topic_end_ptr);
    }

    if (strcmp(result->status_token, "ERR") == 0 && strcmp(result->topic, "UNKNOWN") == 0) {
        result->status = SC_COMMAND_STATUS_UNKNOWN_CMD;
        copy_string(result->status_token, sizeof(result->status_token),
                    SC_STATUS_UNKNOWN_CMD);
        result->topic[0] = '\0';
    }

    copy_string(result->details, sizeof(result->details), cursor != 0 ? cursor : "");
}

static bool sc_core_send_sc_command_internal(
    ScCore *core,
    size_t module_index,
    const char *command,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    if (result != 0) {
        command_result_reset(result);
    }

    if (core == 0) {
        log_append(log_output, log_output_size, "[ERROR] Core is not initialized.\n");
        return false;
    }

    if (result == 0) {
        log_append(log_output, log_output_size, "[ERROR] Command result buffer is NULL.\n");
        return false;
    }

    if (command == 0 || command[0] == '\0') {
        log_append(log_output, log_output_size, "[ERROR] SC command is empty.\n");
        return false;
    }

    if (module_index >= SC_MODULE_COUNT) {
        log_append(log_output, log_output_size, "[ERROR] Module index out of range: %zu\n", module_index);
        return false;
    }

    ScModuleStatus *status = &core->modules[module_index];
    if (!status->detected || status->port_path[0] == '\0') {
        log_append(
            log_output,
            log_output_size,
            "[ERROR] Module '%s' is not currently detected.\n",
            status->display_name
        );
        return false;
    }

    if (status->target_ambiguous) {
        log_append(
            log_output,
            log_output_size,
            "[ERROR] Refusing command '%s' for '%s': ambiguous target (%zu instances).\n",
            command,
            status->display_name,
            status->detected_instances
        );
        return false;
    }

    char response[SC_HELLO_RESPONSE_MAX];
    char error[256];
    response[0] = '\0';
    error[0] = '\0';

    if (!sc_transport_send_sc_command(
            &core->transport,
            status->port_path,
            command,
            response,
            sizeof(response),
            error,
            sizeof(error)
        )) {
        log_append(
            log_output,
            log_output_size,
            "[ERROR] %s on %s failed: %s\n",
            command,
            status->port_path,
            error
        );
        return false;
    }

    parse_sc_command_result(response, result);
    log_append(
        log_output,
        log_output_size,
        "%s on %s -> %s\n",
        command,
        status->port_path,
        result->response
    );
    return true;
}

void sc_core_init(ScCore *core)
{
    if (core == 0) {
        return;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        core->modules[i].display_name = k_module_defs[i].display_name;
    }

    sc_transport_init_default(&core->transport);
    sc_core_reset_detection(core);
}

void sc_core_set_transport(ScCore *core, const ScTransport *transport)
{
    if (core == 0) {
        return;
    }

    if (transport == 0) {
        sc_transport_init_default(&core->transport);
        return;
    }

    core->transport = *transport;
}

void sc_core_reset_detection(ScCore *core)
{
    if (core == 0) {
        return;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        core->modules[i].detected = false;
        core->modules[i].detected_instances = 0u;
        core->modules[i].target_ambiguous = false;
        core->modules[i].port_path[0] = '\0';
        core->modules[i].hello_response[0] = '\0';
        identity_reset(&core->modules[i].hello_identity);
        identity_reset(&core->modules[i].meta_identity);
    }
}

void sc_core_detect_modules(ScCore *core, char *log_output, size_t log_output_size)
{
    if (log_output != 0 && log_output_size > 0u) {
        log_output[0] = '\0';
    }

    if (core == 0) {
        log_append(log_output, log_output_size, "[ERROR] Core is not initialized.\n");
        return;
    }

    sc_core_reset_detection(core);
    log_append(log_output, log_output_size, "Detecting Fiesta modules with HELLO...\n");

    ScTransportCandidateList candidates;
    char list_error[256];
    list_error[0] = '\0';

    if (!sc_transport_list_candidates(&core->transport, &candidates, list_error, sizeof(list_error))) {
        log_append(
            log_output,
            log_output_size,
            "[ERROR] Candidate enumeration failed: %s\n",
            list_error
        );
        return;
    }

    if (candidates.count == 0u) {
        log_append(
            log_output,
            log_output_size,
            "No Fiesta serial devices found in /dev/serial/by-id.\n"
        );
        return;
    }

    log_append(log_output, log_output_size, "Candidates: %zu\n", candidates.count);
    if (candidates.truncated) {
        log_append(
            log_output,
            log_output_size,
            "[WARN] Candidate list was truncated at %u entries.\n",
            SC_TRANSPORT_MAX_CANDIDATES
        );
    }

    for (size_t i = 0u; i < candidates.count; ++i) {
        const char *candidate_path = candidates.paths[i];
        char device_path[SC_PORT_PATH_MAX];
        char resolve_error[256];
        device_path[0] = '\0';
        resolve_error[0] = '\0';

        if (!sc_transport_resolve_device_path(
                &core->transport,
                candidate_path,
                device_path,
                sizeof(device_path),
                resolve_error,
                sizeof(resolve_error)
            )) {
            log_append(
                log_output,
                log_output_size,
                "\n[%zu/%zu] %s -> resolve failed: %s\n",
                i + 1u,
                candidates.count,
                candidate_path,
                resolve_error
            );
            continue;
        }

        log_append(
            log_output,
            log_output_size,
            "\n[%zu/%zu] %s -> %s\n",
            i + 1u,
            candidates.count,
            candidate_path,
            device_path
        );

        char response[SC_HELLO_RESPONSE_MAX];
        char error[256];
        response[0] = '\0';
        error[0] = '\0';

        if (!sc_transport_send_hello(
                &core->transport,
                device_path,
                response,
                sizeof(response),
                error,
                sizeof(error)
            )) {
            log_append(log_output, log_output_size, "HELLO failed: %s\n", error);
            continue;
        }

        log_append(log_output, log_output_size, "HELLO response: %s\n", response);

        if (strncmp(response, SC_REPLY_HELLO_HEAD,
                    sizeof(SC_REPLY_HELLO_HEAD) - 1u) != 0) {
            log_append(log_output, log_output_size,
                       "Ignored: response is not " SC_REPLY_HELLO_HEAD ".\n");
            continue;
        }

        ScIdentityData parsed_identity;
        if (!parse_hello_identity(response, &parsed_identity)) {
            log_append(
                log_output,
                log_output_size,
                "Ignored: HELLO does not include a valid module identity.\n"
            );
            continue;
        }

        const int module_index = module_index_from_token(parsed_identity.module_name);
        if (module_index < 0) {
            log_append(
                log_output,
                log_output_size,
                "Ignored: unsupported module token '%s'.\n",
                parsed_identity.module_name
            );
            continue;
        }

        ScModuleStatus *status = &core->modules[module_index];
        status->detected_instances++;
        if (status->detected) {
            status->target_ambiguous = true;
            log_append(
                log_output,
                log_output_size,
                "Module '%s' appears multiple times (%zu instances). Marked ambiguous.\n",
                status->display_name,
                status->detected_instances
            );
        }

        status->detected = true;
        copy_string(status->port_path, sizeof(status->port_path), device_path);
        copy_string(status->hello_response, sizeof(status->hello_response), response);
        status->hello_identity = parsed_identity;

        log_append(
            log_output,
            log_output_size,
            "Detected module '%s' on %s\n",
            status->display_name,
            device_path
        );

        /* Scan all candidates even after every module type is matched
         * once: a second instance of the same module on a later port
         * must be observed so target_ambiguous can be raised. */
    }

    log_append(log_output, log_output_size, "\nDetection summary:\n");
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *status = &core->modules[i];
        log_append(
            log_output,
            log_output_size,
            "- %-12s : %s (instances=%zu%s)\n",
            status->display_name,
            status->detected ? "DETECTED" : "NOT DETECTED",
            status->detected_instances,
            status->target_ambiguous ? ", ambiguous" : ""
        );
    }
}

size_t sc_core_module_count(void)
{
    return SC_MODULE_COUNT;
}

const ScModuleStatus *sc_core_module_status(const ScCore *core, size_t index)
{
    if (core == 0 || index >= SC_MODULE_COUNT) {
        return 0;
    }

    return &core->modules[index];
}

const char *sc_command_status_name(ScCommandStatus status)
{
    switch (status) {
        case SC_COMMAND_STATUS_UNPARSEABLE:
            return "UNPARSEABLE";
        case SC_COMMAND_STATUS_OK:
            return SC_STATUS_OK;
        case SC_COMMAND_STATUS_UNKNOWN_CMD:
            return SC_STATUS_UNKNOWN_CMD;
        case SC_COMMAND_STATUS_BAD_REQUEST:
            return SC_STATUS_BAD_REQUEST;
        case SC_COMMAND_STATUS_NOT_READY:
            return SC_STATUS_NOT_READY;
        case SC_COMMAND_STATUS_INVALID_PARAM_ID:
            return SC_STATUS_INVALID_PARAM_ID;
        case SC_COMMAND_STATUS_OTHER:
            return "SC_OTHER";
        default:
            return "UNKNOWN";
    }
}

const char *sc_value_type_name(ScValueType type)
{
    switch (type) {
        case SC_VALUE_TYPE_UNKNOWN:
            return "UNKNOWN";
        case SC_VALUE_TYPE_BOOL:
            return "BOOL";
        case SC_VALUE_TYPE_INT:
            return "INT";
        case SC_VALUE_TYPE_UINT:
            return "UINT";
        case SC_VALUE_TYPE_FLOAT:
            return "FLOAT";
        case SC_VALUE_TYPE_TEXT:
            return "TEXT";
        default:
            return "UNKNOWN";
    }
}

bool sc_core_parse_param_list_result(
    const ScCommandResult *result,
    ScParamListData *parsed,
    char *error,
    size_t error_size
)
{
    set_error(error, error_size, "");
    param_list_reset(parsed);

    if (result == 0 || parsed == 0) {
        set_error(error, error_size, "param-list parse failed: invalid arguments");
        return false;
    }

    if (result->status != SC_COMMAND_STATUS_OK || strcmp(result->topic, "PARAM_LIST") != 0) {
        set_error(error, error_size, "param-list parse failed: response is not SC_OK PARAM_LIST");
        return false;
    }

    const char *cursor = result->details;
    if (cursor == 0) {
        return true;
    }

    bool saw_any_token = false;

    while (cursor[0] != '\0') {
        while (cursor[0] != '\0' && param_list_separator_char(cursor[0])) {
            cursor++;
        }

        if (cursor[0] == '\0') {
            break;
        }

        const char *token_start = cursor;
        while (cursor[0] != '\0' && !param_list_separator_char(cursor[0])) {
            cursor++;
        }

        char token[SC_PARAM_ID_MAX];
        copy_span(token, sizeof(token), token_start, cursor);
        saw_any_token = true;
        if (!param_id_is_valid(token)) {
            parsed->truncated = true;
            continue;
        }

        bool already_present = false;
        for (size_t i = 0u; i < parsed->count; ++i) {
            if (strcmp(parsed->ids[i], token) == 0) {
                already_present = true;
                break;
            }
        }

        if (already_present) {
            continue;
        }

        if (parsed->count >= SC_PARAM_ITEMS_MAX) {
            parsed->truncated = true;
            continue;
        }

        copy_string(parsed->ids[parsed->count], sizeof(parsed->ids[parsed->count]), token);
        parsed->count++;
    }

    if (parsed->count == 0u && saw_any_token) {
        set_error(error, error_size, "param-list parse failed: no valid parameter ids in payload");
        return false;
    }

    return true;
}

bool sc_core_parse_param_values_result(
    const ScCommandResult *result,
    ScParamValuesData *parsed,
    char *error,
    size_t error_size
)
{
    set_error(error, error_size, "");
    param_values_reset(parsed);

    if (result == 0 || parsed == 0) {
        set_error(error, error_size, "param-values parse failed: invalid arguments");
        return false;
    }

    if (result->status != SC_COMMAND_STATUS_OK || strcmp(result->topic, "PARAM_VALUES") != 0) {
        set_error(error, error_size, "param-values parse failed: response is not SC_OK PARAM_VALUES");
        return false;
    }

    const char *cursor = result->details;
    char token[SC_HELLO_RESPONSE_MAX];
    bool saw_any_token = false;
    while (parse_next_token(&cursor, token, sizeof(token))) {
        saw_any_token = true;
        char *equals = strchr(token, '=');
        if (equals == 0) {
            parsed->truncated = true;
            continue;
        }

        *equals = '\0';
        const char *id = token;
        const char *raw_value = equals + 1u;
        if (!param_id_is_valid(id)) {
            parsed->truncated = true;
            continue;
        }

        if (raw_value[0] == '\0') {
            parsed->truncated = true;
            continue;
        }

        bool already_present = false;
        for (size_t i = 0u; i < parsed->count; ++i) {
            if (strcmp(parsed->entries[i].id, id) == 0) {
                already_present = true;
                typed_value_from_text(raw_value, &parsed->entries[i].value);
                break;
            }
        }
        if (already_present) {
            continue;
        }

        if (parsed->count >= SC_PARAM_ITEMS_MAX) {
            parsed->truncated = true;
            continue;
        }

        copy_string(parsed->entries[parsed->count].id, sizeof(parsed->entries[parsed->count].id), id);
        typed_value_from_text(raw_value, &parsed->entries[parsed->count].value);
        parsed->count++;
    }

    if (parsed->count == 0u && saw_any_token) {
        set_error(error, error_size, "param-values parse failed: no valid id=value tokens");
        return false;
    }

    return true;
}

bool sc_core_parse_param_result(
    const ScCommandResult *result,
    ScParamDetailData *parsed,
    char *error,
    size_t error_size
)
{
    set_error(error, error_size, "");
    param_detail_reset(parsed);

    if (result == 0 || parsed == 0) {
        set_error(error, error_size, "param parse failed: invalid arguments");
        return false;
    }

    if (result->status != SC_COMMAND_STATUS_OK || strcmp(result->topic, "PARAM") != 0) {
        set_error(error, error_size, "param parse failed: response is not SC_OK PARAM");
        return false;
    }

    const char *cursor = result->details;
    char token[SC_HELLO_RESPONSE_MAX];
    while (parse_next_token(&cursor, token, sizeof(token))) {
        char *equals = strchr(token, '=');
        if (equals == 0) {
            set_error(error, error_size, "param parse failed: token without '='");
            return false;
        }

        *equals = '\0';
        const char *key = token;
        const char *raw_value = equals + 1u;
        if (raw_value[0] == '\0') {
            set_error(error, error_size, "param parse failed: empty value token");
            return false;
        }

        if (strcmp(key, "id") == 0) {
            if (!param_id_is_valid(raw_value)) {
                set_error(error, error_size, "param parse failed: invalid id");
                return false;
            }
            copy_string(parsed->id, sizeof(parsed->id), raw_value);
            continue;
        }

        if (strcmp(key, "value") == 0) {
            typed_value_from_text(raw_value, &parsed->value);
            parsed->has_value = true;
            continue;
        }

        if (strcmp(key, "min") == 0) {
            typed_value_from_text(raw_value, &parsed->min);
            parsed->has_min = true;
            continue;
        }

        if (strcmp(key, "max") == 0) {
            typed_value_from_text(raw_value, &parsed->max);
            parsed->has_max = true;
            continue;
        }

        if (strcmp(key, "default") == 0) {
            typed_value_from_text(raw_value, &parsed->default_value);
            parsed->has_default = true;
            continue;
        }
    }

    if (parsed->id[0] == '\0') {
        set_error(error, error_size, "param parse failed: missing id");
        return false;
    }

    if (!parsed->has_value) {
        set_error(error, error_size, "param parse failed: missing value");
        return false;
    }

    if (parsed->has_min && parsed->has_max &&
        typed_value_is_numeric(&parsed->min) &&
        typed_value_is_numeric(&parsed->max)) {
        if (typed_value_as_double(&parsed->min) > typed_value_as_double(&parsed->max)) {
            set_error(error, error_size, "param parse failed: min > max");
            return false;
        }
    }

    if (parsed->has_value && parsed->has_min &&
        typed_value_is_numeric(&parsed->value) &&
        typed_value_is_numeric(&parsed->min)) {
        if (typed_value_as_double(&parsed->value) < typed_value_as_double(&parsed->min)) {
            set_error(error, error_size, "param parse failed: value < min");
            return false;
        }
    }

    if (parsed->has_value && parsed->has_max &&
        typed_value_is_numeric(&parsed->value) &&
        typed_value_is_numeric(&parsed->max)) {
        if (typed_value_as_double(&parsed->value) > typed_value_as_double(&parsed->max)) {
            set_error(error, error_size, "param parse failed: value > max");
            return false;
        }
    }

    if (parsed->has_default && parsed->has_min &&
        typed_value_is_numeric(&parsed->default_value) &&
        typed_value_is_numeric(&parsed->min)) {
        if (typed_value_as_double(&parsed->default_value) < typed_value_as_double(&parsed->min)) {
            set_error(error, error_size, "param parse failed: default < min");
            return false;
        }
    }

    if (parsed->has_default && parsed->has_max &&
        typed_value_is_numeric(&parsed->default_value) &&
        typed_value_is_numeric(&parsed->max)) {
        if (typed_value_as_double(&parsed->default_value) > typed_value_as_double(&parsed->max)) {
            set_error(error, error_size, "param parse failed: default > max");
            return false;
        }
    }

    parsed->valid = true;
    return true;
}

bool sc_core_sc_get_meta(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    const bool success = sc_core_send_sc_command_internal(
        core,
        module_index,
        SC_CMD_GET_META,
        result,
        log_output,
        log_output_size
    );
    if (!success || core == 0 || result == 0 || module_index >= SC_MODULE_COUNT) {
        return success;
    }

    if (result->status == SC_COMMAND_STATUS_OK &&
        strcmp(result->topic, SC_REPLY_TAG_META) == 0) {
        ScIdentityData parsed;
        if (parse_meta_identity(result->response, &parsed)) {
            core->modules[module_index].meta_identity = parsed;
        } else {
            log_append(
                log_output,
                log_output_size,
                "[WARN] " SC_CMD_GET_META " returned " SC_STATUS_OK " "
                SC_REPLY_TAG_META
                " but identity fields were incomplete.\n"
            );
        }
    }

    return true;
}

bool sc_core_sc_get_param_list(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    return sc_core_send_sc_command_internal(
        core,
        module_index,
        SC_CMD_GET_PARAM_LIST,
        result,
        log_output,
        log_output_size
    );
}

bool sc_core_sc_get_values(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    return sc_core_send_sc_command_internal(
        core,
        module_index,
        SC_CMD_GET_VALUES,
        result,
        log_output,
        log_output_size
    );
}

bool sc_core_sc_get_param(
    ScCore *core,
    size_t module_index,
    const char *param_id,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    if (param_id == 0 || param_id[0] == '\0') {
        if (result != 0) {
            command_result_reset(result);
        }
        log_append(log_output, log_output_size,
                   "[ERROR] param_id is required for " SC_CMD_GET_PARAM ".\n");
        return false;
    }

    for (size_t i = 0u; param_id[i] != '\0'; ++i) {
        if (isspace((unsigned char)param_id[i])) {
            if (result != 0) {
                command_result_reset(result);
            }
            log_append(
                log_output,
                log_output_size,
                "[ERROR] param_id must not contain whitespace: '%s'\n",
                param_id
            );
            return false;
        }
    }

    char command[SC_HELLO_RESPONSE_MAX];
    const int written = snprintf(command, sizeof(command),
                                 SC_CMD_GET_PARAM " %s", param_id);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        if (result != 0) {
            command_result_reset(result);
        }
        log_append(log_output, log_output_size,
                   "[ERROR] " SC_CMD_GET_PARAM " command is too long.\n");
        return false;
    }

    return sc_core_send_sc_command_internal(
        core,
        module_index,
        command,
        result,
        log_output,
        log_output_size
    );
}

/* ── Phase 5: authenticated bootloader entry ───────────────────────────── */

static void set_phase5_error(char *error, size_t error_size, const char *fmt, ...)
{
    if (error == NULL || error_size == 0u) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(error, error_size, fmt, args);
    va_end(args);
}

/* Find " key=" inside @p src, copy the value (until next space or NUL) into
 * @p out. Returns true on success. The leading space in the search token
 * disambiguates against substrings (e.g. "session" vs "build_session"). */
static bool extract_kv_value(const char *src,
                             const char *key,
                             char *out,
                             size_t out_size)
{
    if (src == NULL || key == NULL || out == NULL || out_size == 0u) {
        return false;
    }
    char needle[40];
    const int n = snprintf(needle, sizeof(needle), " %s=", key);
    if (n < 0 || (size_t)n >= sizeof(needle)) {
        return false;
    }
    const char *hit = strstr(src, needle);
    if (hit == NULL) {
        return false;
    }
    const char *value = hit + (size_t)n;
    size_t len = 0u;
    while (value[len] != '\0' && value[len] != ' ' && value[len] != '\r' &&
           value[len] != '\n' && len + 1u < out_size) {
        out[len] = value[len];
        ++len;
    }
    if (value[len] != '\0' && value[len] != ' ' && value[len] != '\r' &&
        value[len] != '\n') {
        /* Truncated. */
        return false;
    }
    out[len] = '\0';
    return len > 0u;
}

const char *sc_auth_status_name(ScAuthStatus status)
{
    switch (status) {
    case SC_AUTH_OK:                     return "OK";
    case SC_AUTH_ERR_NULL_ARG:           return "NULL_ARG";
    case SC_AUTH_ERR_HELLO_FAILED:       return "HELLO_FAILED";
    case SC_AUTH_ERR_HELLO_PARSE:        return "HELLO_PARSE";
    case SC_AUTH_ERR_BEGIN_FAILED:       return "BEGIN_FAILED";
    case SC_AUTH_ERR_BAD_CHALLENGE:      return "BAD_CHALLENGE";
    case SC_AUTH_ERR_RESPONSE_COMPUTE:   return "RESPONSE_COMPUTE";
    case SC_AUTH_ERR_PROVE_FAILED:       return "PROVE_FAILED";
    case SC_AUTH_ERR_AUTH_REJECTED:      return "AUTH_REJECTED";
    }
    return "UNKNOWN";
}

const char *sc_reboot_status_name(ScRebootStatus status)
{
    switch (status) {
    case SC_REBOOT_OK:                   return "OK";
    case SC_REBOOT_ERR_NULL_ARG:         return "NULL_ARG";
    case SC_REBOOT_ERR_TRANSPORT:        return "TRANSPORT";
    case SC_REBOOT_ERR_NOT_AUTHORIZED:   return "NOT_AUTHORIZED";
    case SC_REBOOT_ERR_UNEXPECTED_REPLY: return "UNEXPECTED_REPLY";
    }
    return "UNKNOWN";
}

ScAuthStatus sc_core_authenticate(
    const ScTransport *transport,
    const char *device_path,
    char *error,
    size_t error_size)
{
    if (transport == NULL || transport->ops == NULL || device_path == NULL) {
        set_phase5_error(error, error_size, "null argument");
        return SC_AUTH_ERR_NULL_ARG;
    }

    char tx_error[256];
    tx_error[0] = '\0';

    /* 1. HELLO - refresh identity and start a fresh authenticated window. */
    char hello[SC_HELLO_RESPONSE_MAX];
    if (transport->ops->send_hello == NULL ||
        !transport->ops->send_hello(transport->context, device_path,
                                    hello, sizeof(hello),
                                    tx_error, sizeof(tx_error))) {
        set_phase5_error(error, error_size, "HELLO failed: %s", tx_error);
        return SC_AUTH_ERR_HELLO_FAILED;
    }

    /* 2. Parse uid_hex and session_id from the HELLO inner payload. */
    char uid_hex[SC_IDENTITY_FIELD_MAX];
    char session_str[SC_IDENTITY_FIELD_MAX];
    if (!extract_kv_value(hello, "uid", uid_hex, sizeof(uid_hex)) ||
        !extract_kv_value(hello, "session", session_str, sizeof(session_str))) {
        set_phase5_error(error, error_size,
                         "HELLO parse: missing uid/session in '%s'", hello);
        return SC_AUTH_ERR_HELLO_PARSE;
    }

    uint8_t uid_bytes[8];
    if (strlen(uid_hex) != sizeof(uid_bytes) * 2u ||
        !sc_auth_decode_hex(uid_hex, uid_bytes, sizeof(uid_bytes))) {
        set_phase5_error(error, error_size,
                         "HELLO parse: bad uid hex '%s'", uid_hex);
        return SC_AUTH_ERR_HELLO_PARSE;
    }

    char *end = NULL;
    const unsigned long session_ul = strtoul(session_str, &end, 10);
    if (end == session_str || *end != '\0' || session_ul == 0u ||
        session_ul > UINT32_MAX) {
        set_phase5_error(error, error_size,
                         "HELLO parse: bad session '%s'", session_str);
        return SC_AUTH_ERR_HELLO_PARSE;
    }
    const uint32_t session_id = (uint32_t)session_ul;

    /* 3. SC_AUTH_BEGIN -> SC_OK AUTH_CHALLENGE <hex>. */
    char begin_reply[SC_HELLO_RESPONSE_MAX];
    if (transport->ops->send_sc_command == NULL ||
        !transport->ops->send_sc_command(transport->context, device_path,
                                         SC_CMD_AUTH_BEGIN,
                                         begin_reply, sizeof(begin_reply),
                                         tx_error, sizeof(tx_error))) {
        set_phase5_error(error, error_size,
                         "AUTH_BEGIN transport failed: %s", tx_error);
        return SC_AUTH_ERR_BEGIN_FAILED;
    }

    static const char k_chal_prefix[] =
        SC_STATUS_OK " " SC_REPLY_TAG_AUTH_CHALLENGE " ";
    if (strncmp(begin_reply, k_chal_prefix, sizeof(k_chal_prefix) - 1u) != 0) {
        set_phase5_error(error, error_size,
                         "AUTH_BEGIN unexpected reply: %s", begin_reply);
        return SC_AUTH_ERR_BAD_CHALLENGE;
    }

    const char *challenge_hex = begin_reply + (sizeof(k_chal_prefix) - 1u);
    uint8_t challenge[SC_AUTH_CHALLENGE_BYTES];
    if (strlen(challenge_hex) != SC_AUTH_CHALLENGE_BYTES * 2u ||
        !sc_auth_decode_hex(challenge_hex, challenge, sizeof(challenge))) {
        set_phase5_error(error, error_size,
                         "AUTH_BEGIN bad challenge hex: %s", challenge_hex);
        return SC_AUTH_ERR_BAD_CHALLENGE;
    }

    /* 4. Compute response and send SC_AUTH_PROVE <hex>. */
    char response_hex[SC_AUTH_RESPONSE_HEX_BUF_SIZE];
    if (!sc_auth_compute_response_hex(uid_bytes, sizeof(uid_bytes),
                                      challenge, sizeof(challenge),
                                      session_id,
                                      response_hex, sizeof(response_hex))) {
        set_phase5_error(error, error_size, "compute_response_hex failed");
        return SC_AUTH_ERR_RESPONSE_COMPUTE;
    }

    char prove_cmd[SC_HELLO_RESPONSE_MAX];
    const int written = snprintf(prove_cmd, sizeof(prove_cmd),
                                 SC_CMD_AUTH_PROVE " %s", response_hex);
    if (written < 0 || (size_t)written >= sizeof(prove_cmd)) {
        set_phase5_error(error, error_size, "AUTH_PROVE command too long");
        return SC_AUTH_ERR_RESPONSE_COMPUTE;
    }

    char prove_reply[SC_HELLO_RESPONSE_MAX];
    if (!transport->ops->send_sc_command(transport->context, device_path,
                                         prove_cmd,
                                         prove_reply, sizeof(prove_reply),
                                         tx_error, sizeof(tx_error))) {
        set_phase5_error(error, error_size,
                         "AUTH_PROVE transport failed: %s", tx_error);
        return SC_AUTH_ERR_PROVE_FAILED;
    }

    if (strcmp(prove_reply, SC_REPLY_AUTH_OK) != 0) {
        set_phase5_error(error, error_size,
                         "AUTH_PROVE rejected: %s", prove_reply);
        return SC_AUTH_ERR_AUTH_REJECTED;
    }

    return SC_AUTH_OK;
}

ScRebootStatus sc_core_reboot_to_bootloader(
    const ScTransport *transport,
    const char *device_path,
    char *error,
    size_t error_size)
{
    if (transport == NULL || transport->ops == NULL ||
        transport->ops->send_sc_command == NULL || device_path == NULL) {
        set_phase5_error(error, error_size, "null argument");
        return SC_REBOOT_ERR_NULL_ARG;
    }

    char tx_error[256];
    tx_error[0] = '\0';
    char reply[SC_HELLO_RESPONSE_MAX];

    if (!transport->ops->send_sc_command(transport->context, device_path,
                                         SC_CMD_REBOOT_BOOTLOADER,
                                         reply, sizeof(reply),
                                         tx_error, sizeof(tx_error))) {
        set_phase5_error(error, error_size,
                         SC_CMD_REBOOT_BOOTLOADER " transport failed: %s",
                         tx_error);
        return SC_REBOOT_ERR_TRANSPORT;
    }

    if (strcmp(reply, SC_REPLY_REBOOT_OK) == 0) {
        return SC_REBOOT_OK;
    }
    if (strncmp(reply, SC_STATUS_NOT_AUTHORIZED,
                sizeof(SC_STATUS_NOT_AUTHORIZED) - 1u) == 0) {
        set_phase5_error(error, error_size,
                         "firmware refused: %s", reply);
        return SC_REBOOT_ERR_NOT_AUTHORIZED;
    }
    set_phase5_error(error, error_size, "unexpected reply: %s", reply);
    return SC_REBOOT_ERR_UNEXPECTED_REPLY;
}

/* ── Phase 8.5: parameter staging host orchestrator ───────────────── */

const char *sc_set_param_status_name(ScSetParamStatus status)
{
    switch (status) {
    case SC_SET_PARAM_OK:                   return "OK";
    case SC_SET_PARAM_ERR_NULL_ARG:         return "NULL_ARG";
    case SC_SET_PARAM_ERR_TRANSPORT:        return "TRANSPORT";
    case SC_SET_PARAM_ERR_NOT_AUTHORIZED:   return "NOT_AUTHORIZED";
    case SC_SET_PARAM_ERR_INVALID_ID:       return "INVALID_ID";
    case SC_SET_PARAM_ERR_READ_ONLY:        return "READ_ONLY";
    case SC_SET_PARAM_ERR_OUT_OF_RANGE:     return "OUT_OF_RANGE";
    case SC_SET_PARAM_ERR_UNEXPECTED_REPLY: return "UNEXPECTED_REPLY";
    }
    return "UNKNOWN";
}

const char *sc_commit_params_status_name(ScCommitParamsStatus status)
{
    switch (status) {
    case SC_COMMIT_PARAMS_OK:                   return "OK";
    case SC_COMMIT_PARAMS_ERR_NULL_ARG:         return "NULL_ARG";
    case SC_COMMIT_PARAMS_ERR_TRANSPORT:        return "TRANSPORT";
    case SC_COMMIT_PARAMS_ERR_NOT_AUTHORIZED:   return "NOT_AUTHORIZED";
    case SC_COMMIT_PARAMS_ERR_COMMIT_FAILED:    return "COMMIT_FAILED";
    case SC_COMMIT_PARAMS_ERR_UNEXPECTED_REPLY: return "UNEXPECTED_REPLY";
    }
    return "UNKNOWN";
}

const char *sc_revert_params_status_name(ScRevertParamsStatus status)
{
    switch (status) {
    case SC_REVERT_PARAMS_OK:                   return "OK";
    case SC_REVERT_PARAMS_ERR_NULL_ARG:         return "NULL_ARG";
    case SC_REVERT_PARAMS_ERR_TRANSPORT:        return "TRANSPORT";
    case SC_REVERT_PARAMS_ERR_NOT_AUTHORIZED:   return "NOT_AUTHORIZED";
    case SC_REVERT_PARAMS_ERR_UNEXPECTED_REPLY: return "UNEXPECTED_REPLY";
    }
    return "UNKNOWN";
}

ScSetParamStatus sc_core_set_param(
    const ScTransport *transport,
    const char *device_path,
    const char *param_id,
    int16_t value,
    char *error,
    size_t error_size)
{
    if (transport == NULL || transport->ops == NULL ||
        transport->ops->send_sc_command == NULL ||
        device_path == NULL || param_id == NULL) {
        set_phase5_error(error, error_size, "null argument");
        return SC_SET_PARAM_ERR_NULL_ARG;
    }

    char cmd[SC_HELLO_RESPONSE_MAX];
    const int n = snprintf(cmd, sizeof(cmd),
                           SC_CMD_SET_PARAM " %s %d",
                           param_id, (int)value);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        set_phase5_error(error, error_size,
                         SC_CMD_SET_PARAM " command too long");
        return SC_SET_PARAM_ERR_NULL_ARG;
    }

    char tx_error[256];
    tx_error[0] = '\0';
    char reply[SC_HELLO_RESPONSE_MAX];
    if (!transport->ops->send_sc_command(transport->context, device_path,
                                         cmd, reply, sizeof(reply),
                                         tx_error, sizeof(tx_error))) {
        set_phase5_error(error, error_size,
                         SC_CMD_SET_PARAM " transport failed: %s", tx_error);
        return SC_SET_PARAM_ERR_TRANSPORT;
    }

    /* Happy path: "SC_OK PARAM_SET id=<id> staged=<int> active=<int>". */
    static const char k_ok_prefix[] =
        SC_STATUS_OK " " SC_REPLY_TAG_PARAM_SET " ";
    if (strncmp(reply, k_ok_prefix, sizeof(k_ok_prefix) - 1u) == 0) {
        return SC_SET_PARAM_OK;
    }

    if (strncmp(reply, SC_STATUS_NOT_AUTHORIZED,
                sizeof(SC_STATUS_NOT_AUTHORIZED) - 1u) == 0) {
        set_phase5_error(error, error_size, "firmware: %s", reply);
        return SC_SET_PARAM_ERR_NOT_AUTHORIZED;
    }
    if (strncmp(reply, SC_STATUS_INVALID_PARAM_ID,
                sizeof(SC_STATUS_INVALID_PARAM_ID) - 1u) == 0) {
        set_phase5_error(error, error_size, "firmware: %s", reply);
        return SC_SET_PARAM_ERR_INVALID_ID;
    }

    /* SC_BAD_REQUEST splits into read_only / out_of_range / other. */
    static const char k_bad_prefix[] = SC_STATUS_BAD_REQUEST " ";
    if (strncmp(reply, k_bad_prefix, sizeof(k_bad_prefix) - 1u) == 0) {
        const char *tail = reply + (sizeof(k_bad_prefix) - 1u);
        if (strncmp(tail, "read_only", 9u) == 0 &&
            (tail[9] == ' ' || tail[9] == '\0')) {
            set_phase5_error(error, error_size, "firmware: %s", reply);
            return SC_SET_PARAM_ERR_READ_ONLY;
        }
        if (strncmp(tail, "out_of_range", 12u) == 0 &&
            (tail[12] == ' ' || tail[12] == '\0')) {
            set_phase5_error(error, error_size, "firmware: %s", reply);
            return SC_SET_PARAM_ERR_OUT_OF_RANGE;
        }
        /* Other SC_BAD_REQUEST variants (e.g. param_id_too_long,
         * value_not_int16) are treated as UNEXPECTED at this layer
         * because the caller already pre-validates argv before
         * sending. */
    }

    set_phase5_error(error, error_size, "unexpected reply: %s", reply);
    return SC_SET_PARAM_ERR_UNEXPECTED_REPLY;
}

ScCommitParamsStatus sc_core_commit_params(
    const ScTransport *transport,
    const char *device_path,
    char *error,
    size_t error_size)
{
    if (transport == NULL || transport->ops == NULL ||
        transport->ops->send_sc_command == NULL || device_path == NULL) {
        set_phase5_error(error, error_size, "null argument");
        return SC_COMMIT_PARAMS_ERR_NULL_ARG;
    }

    char tx_error[256];
    tx_error[0] = '\0';
    char reply[SC_HELLO_RESPONSE_MAX];
    if (!transport->ops->send_sc_command(transport->context, device_path,
                                         SC_CMD_COMMIT_PARAMS,
                                         reply, sizeof(reply),
                                         tx_error, sizeof(tx_error))) {
        set_phase5_error(error, error_size,
                         SC_CMD_COMMIT_PARAMS " transport failed: %s",
                         tx_error);
        return SC_COMMIT_PARAMS_ERR_TRANSPORT;
    }

    /* Happy path: "SC_OK PARAMS_COMMITTED count=<n>". */
    static const char k_ok_prefix[] =
        SC_STATUS_OK " " SC_REPLY_TAG_PARAMS_COMMITTED " ";
    if (strncmp(reply, k_ok_prefix, sizeof(k_ok_prefix) - 1u) == 0) {
        return SC_COMMIT_PARAMS_OK;
    }
    if (strncmp(reply, SC_STATUS_NOT_AUTHORIZED,
                sizeof(SC_STATUS_NOT_AUTHORIZED) - 1u) == 0) {
        set_phase5_error(error, error_size, "firmware: %s", reply);
        return SC_COMMIT_PARAMS_ERR_NOT_AUTHORIZED;
    }
    if (strncmp(reply, SC_STATUS_COMMIT_FAILED,
                sizeof(SC_STATUS_COMMIT_FAILED) - 1u) == 0) {
        /* Caller reads the precise reason from @p error - we keep the
         * firmware's verbatim "SC_COMMIT_FAILED reason=<token>" line. */
        set_phase5_error(error, error_size, "firmware: %s", reply);
        return SC_COMMIT_PARAMS_ERR_COMMIT_FAILED;
    }

    set_phase5_error(error, error_size, "unexpected reply: %s", reply);
    return SC_COMMIT_PARAMS_ERR_UNEXPECTED_REPLY;
}

ScRevertParamsStatus sc_core_revert_params(
    const ScTransport *transport,
    const char *device_path,
    char *error,
    size_t error_size)
{
    if (transport == NULL || transport->ops == NULL ||
        transport->ops->send_sc_command == NULL || device_path == NULL) {
        set_phase5_error(error, error_size, "null argument");
        return SC_REVERT_PARAMS_ERR_NULL_ARG;
    }

    char tx_error[256];
    tx_error[0] = '\0';
    char reply[SC_HELLO_RESPONSE_MAX];
    if (!transport->ops->send_sc_command(transport->context, device_path,
                                         SC_CMD_REVERT_PARAMS,
                                         reply, sizeof(reply),
                                         tx_error, sizeof(tx_error))) {
        set_phase5_error(error, error_size,
                         SC_CMD_REVERT_PARAMS " transport failed: %s",
                         tx_error);
        return SC_REVERT_PARAMS_ERR_TRANSPORT;
    }

    if (strcmp(reply, SC_REPLY_PARAMS_REVERTED) == 0) {
        return SC_REVERT_PARAMS_OK;
    }
    if (strncmp(reply, SC_STATUS_NOT_AUTHORIZED,
                sizeof(SC_STATUS_NOT_AUTHORIZED) - 1u) == 0) {
        set_phase5_error(error, error_size, "firmware: %s", reply);
        return SC_REVERT_PARAMS_ERR_NOT_AUTHORIZED;
    }

    set_phase5_error(error, error_size, "unexpected reply: %s", reply);
    return SC_REVERT_PARAMS_ERR_UNEXPECTED_REPLY;
}

/* ── Phase 6.5: end-to-end flashing orchestrator ──────────────────── */

const char *sc_flash_status_name(ScFlashStatus status)
{
    switch (status) {
    case SC_FLASH_STATUS_OK:                          return "OK";
    case SC_FLASH_STATUS_NULL_ARG:                    return "NULL_ARG";
    case SC_FLASH_STATUS_FORMAT_REJECTED:             return "FORMAT_REJECTED";
    case SC_FLASH_STATUS_MANIFEST_PARSE_FAILED:       return "MANIFEST_PARSE_FAILED";
    case SC_FLASH_STATUS_MANIFEST_MODULE_MISMATCH:    return "MANIFEST_MODULE_MISMATCH";
    case SC_FLASH_STATUS_MANIFEST_ARTIFACT_MISMATCH:  return "MANIFEST_ARTIFACT_MISMATCH";
    case SC_FLASH_STATUS_AUTH_FAILED:                 return "AUTH_FAILED";
    case SC_FLASH_STATUS_REBOOT_FAILED:               return "REBOOT_FAILED";
    case SC_FLASH_STATUS_BOOTSEL_TIMEOUT:             return "BOOTSEL_TIMEOUT";
    case SC_FLASH_STATUS_COPY_FAILED:                 return "COPY_FAILED";
    case SC_FLASH_STATUS_REENUM_TIMEOUT:              return "REENUM_TIMEOUT";
    case SC_FLASH_STATUS_POST_FLASH_HELLO_FAILED:     return "POST_FLASH_HELLO_FAILED";
    case SC_FLASH_STATUS_POST_FLASH_FW_MISMATCH:      return "POST_FLASH_FW_MISMATCH";
    }
    return "UNKNOWN";
}

const char *sc_flash_phase_name(ScFlashPhase phase)
{
    switch (phase) {
    case SC_FLASH_PHASE_FORMAT_CHECK:        return "FORMAT_CHECK";
    case SC_FLASH_PHASE_MANIFEST_VERIFY:     return "MANIFEST_VERIFY";
    case SC_FLASH_PHASE_AUTHENTICATE:        return "AUTHENTICATE";
    case SC_FLASH_PHASE_REBOOT_TO_BOOTLOADER: return "REBOOT_TO_BOOTLOADER";
    case SC_FLASH_PHASE_WAIT_BOOTSEL:        return "WAIT_BOOTSEL";
    case SC_FLASH_PHASE_COPY:                return "COPY";
    case SC_FLASH_PHASE_WAIT_REENUM:         return "WAIT_REENUM";
    case SC_FLASH_PHASE_POST_FLASH_HELLO:    return "POST_FLASH_HELLO";
    }
    return "UNKNOWN";
}

static void flash_set_error(char *buf, size_t size, const char *fmt, ...)
{
    if (buf == NULL || size == 0u) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buf, size, fmt, args);
    va_end(args);
}

static void flash_pulse(sc_core_flash_progress_cb cb, void *user,
                        ScFlashPhase phase)
{
    if (cb != NULL) {
        cb(phase, 0u, 0u, user);
    }
}

typedef struct {
    sc_core_flash_progress_cb cb;
    void *user;
} flash_copy_progress_state_t;

static void flash_copy_progress_adapter(uint64_t bytes_written,
                                        uint64_t total_bytes, void *user)
{
    flash_copy_progress_state_t *st = (flash_copy_progress_state_t *)user;
    if (st->cb != NULL) {
        st->cb(SC_FLASH_PHASE_COPY, bytes_written, total_bytes, st->user);
    }
}

static void flash_sleep_ms(uint32_t ms)
{
    if (ms == 0u) {
        return;
    }
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000u);
    req.tv_nsec = (long)((ms % 1000u) * 1000000L);
    (void)nanosleep(&req, NULL);
}

ScFlashStatus sc_core_flash(
    const ScTransport *transport,
    size_t module_index,
    const char *device_path,
    const char *uid_hex,
    const char *uf2_path,
    const char *manifest_path_or_null,
    const ScFlashOptions *options_or_null,
    sc_core_flash_progress_cb progress_cb, void *progress_user,
    char *error_buf, size_t error_size)
{
    if (transport == NULL || device_path == NULL || uid_hex == NULL ||
        uid_hex[0] == '\0' || uf2_path == NULL ||
        module_index >= SC_MODULE_COUNT) {
        flash_set_error(error_buf, error_size, "null/invalid input");
        return SC_FLASH_STATUS_NULL_ARG;
    }

    char helper_err[256];

    /* 1. UF2 format check. */
    flash_pulse(progress_cb, progress_user, SC_FLASH_PHASE_FORMAT_CHECK);
    helper_err[0] = '\0';
    const sc_flash_status_t fmt_st = sc_flash_uf2_format_check(
        uf2_path, helper_err, sizeof(helper_err));
    if (fmt_st != SC_FLASH_OK) {
        flash_set_error(error_buf, error_size,
                        "UF2 format rejected: %s - %s",
                        sc_flash_status_str(fmt_st), helper_err);
        return SC_FLASH_STATUS_FORMAT_REJECTED;
    }

    /* 2. Manifest (optional). */
    sc_manifest_t manifest;
    bool have_manifest = false;
    if (manifest_path_or_null != NULL && manifest_path_or_null[0] != '\0') {
        flash_pulse(progress_cb, progress_user, SC_FLASH_PHASE_MANIFEST_VERIFY);
        const sc_manifest_status_t parse_st = sc_manifest_load_file(
            manifest_path_or_null, &manifest);
        if (parse_st != SC_MANIFEST_OK) {
            flash_set_error(error_buf, error_size,
                            "manifest parse failed: %s",
                            sc_manifest_status_str(parse_st));
            return SC_FLASH_STATUS_MANIFEST_PARSE_FAILED;
        }
        have_manifest = true;

        const char *expected = k_module_defs[module_index].token;
        const sc_manifest_status_t mod_st = sc_manifest_check_module_match(
            &manifest, expected);
        if (mod_st != SC_MANIFEST_OK) {
            flash_set_error(error_buf, error_size,
                            "manifest module mismatch: declares '%s', "
                            "expected '%s'",
                            manifest.module_name, expected);
            return SC_FLASH_STATUS_MANIFEST_MODULE_MISMATCH;
        }

        const sc_manifest_status_t art_st = sc_manifest_verify_artifact(
            &manifest, uf2_path);
        if (art_st != SC_MANIFEST_OK) {
            flash_set_error(error_buf, error_size,
                            "manifest artifact mismatch: %s",
                            sc_manifest_status_str(art_st));
            return SC_FLASH_STATUS_MANIFEST_ARTIFACT_MISMATCH;
        }
    }

    /* 3. Authenticate. */
    flash_pulse(progress_cb, progress_user, SC_FLASH_PHASE_AUTHENTICATE);
    helper_err[0] = '\0';
    const ScAuthStatus auth_st = sc_core_authenticate(
        transport, device_path, helper_err, sizeof(helper_err));
    if (auth_st != SC_AUTH_OK) {
        flash_set_error(error_buf, error_size,
                        "auth failed: %s - %s",
                        sc_auth_status_name(auth_st), helper_err);
        return SC_FLASH_STATUS_AUTH_FAILED;
    }

    /* 4. Reboot to bootloader. */
    flash_pulse(progress_cb, progress_user, SC_FLASH_PHASE_REBOOT_TO_BOOTLOADER);
    helper_err[0] = '\0';
    const ScRebootStatus reboot_st = sc_core_reboot_to_bootloader(
        transport, device_path, helper_err, sizeof(helper_err));
    if (reboot_st != SC_REBOOT_OK) {
        flash_set_error(error_buf, error_size,
                        "reboot failed: %s - %s",
                        sc_reboot_status_name(reboot_st), helper_err);
        return SC_FLASH_STATUS_REBOOT_FAILED;
    }

    /* 5. Wait for BOOTSEL drive. */
    const ScFlashOptions defaults = {
        .bootsel_parents = { NULL, NULL },
        .by_id_parent = NULL,
        .bootsel_timeout_ms = SC_FLASH_DEFAULT_BOOTSEL_TIMEOUT_MS,
        .reenum_timeout_ms = SC_FLASH_DEFAULT_REENUM_TIMEOUT_MS,
        .grace_after_reenum_ms = SC_FLASH_DEFAULT_REENUM_GRACE_MS,
    };
    const ScFlashOptions *opts = (options_or_null != NULL)
                                     ? options_or_null : &defaults;
    const uint32_t bootsel_timeout = (opts->bootsel_timeout_ms != 0u)
                                         ? opts->bootsel_timeout_ms
                                         : defaults.bootsel_timeout_ms;
    const uint32_t reenum_timeout = (opts->reenum_timeout_ms != 0u)
                                        ? opts->reenum_timeout_ms
                                        : defaults.reenum_timeout_ms;
    const uint32_t grace_ms = (opts->grace_after_reenum_ms != 0u)
                                  ? opts->grace_after_reenum_ms
                                  : defaults.grace_after_reenum_ms;

    flash_pulse(progress_cb, progress_user, SC_FLASH_PHASE_WAIT_BOOTSEL);
    char bootsel_path[SC_TRANSPORT_PATH_MAX];
    helper_err[0] = '\0';
    sc_flash_status_t bs_st;
    if (opts->bootsel_parents[0] != NULL || opts->bootsel_parents[1] != NULL) {
        const char *parents[2] = {
            opts->bootsel_parents[0], opts->bootsel_parents[1]
        };
        const size_t parent_count =
            (parents[1] != NULL) ? 2u : (parents[0] != NULL) ? 1u : 0u;
        const char *first_non_null = parents[0];
        if (first_non_null == NULL) {
            first_non_null = parents[1];
        }
        const char *p[2] = { first_non_null, parents[1] };
        if (parent_count == 1u && parents[0] == NULL) {
            p[0] = parents[1];
        }
        bs_st = sc_flash__watch_for_bootsel_in(
            p, parent_count, bootsel_timeout,
            bootsel_path, sizeof(bootsel_path),
            helper_err, sizeof(helper_err));
    } else {
        bs_st = sc_flash_watch_for_bootsel(
            bootsel_timeout, bootsel_path, sizeof(bootsel_path),
            helper_err, sizeof(helper_err));
    }
    if (bs_st != SC_FLASH_OK) {
        flash_set_error(error_buf, error_size,
                        "BOOTSEL watcher: %s - %s",
                        sc_flash_status_str(bs_st), helper_err);
        return SC_FLASH_STATUS_BOOTSEL_TIMEOUT;
    }

    /* 6. Copy UF2. */
    flash_copy_progress_state_t copy_state = {
        .cb = progress_cb, .user = progress_user
    };
    helper_err[0] = '\0';
    const sc_flash_status_t cp_st = sc_flash_copy_uf2(
        uf2_path, bootsel_path, flash_copy_progress_adapter, &copy_state,
        helper_err, sizeof(helper_err));
    if (cp_st != SC_FLASH_OK) {
        flash_set_error(error_buf, error_size,
                        "copy failed: %s - %s",
                        sc_flash_status_str(cp_st), helper_err);
        return SC_FLASH_STATUS_COPY_FAILED;
    }

    /* 7. Wait for re-enumeration on the same UID. */
    flash_pulse(progress_cb, progress_user, SC_FLASH_PHASE_WAIT_REENUM);
    char new_device_path[SC_TRANSPORT_PATH_MAX];
    helper_err[0] = '\0';
    const sc_flash_status_t re_st = (opts->by_id_parent != NULL)
        ? sc_flash__wait_reenumeration_in(
              opts->by_id_parent, uid_hex, reenum_timeout,
              new_device_path, sizeof(new_device_path),
              helper_err, sizeof(helper_err))
        : sc_flash_wait_reenumeration(
              uid_hex, reenum_timeout,
              new_device_path, sizeof(new_device_path),
              helper_err, sizeof(helper_err));
    if (re_st != SC_FLASH_OK) {
        flash_set_error(error_buf, error_size,
                        "re-enumeration: %s - %s",
                        sc_flash_status_str(re_st), helper_err);
        return SC_FLASH_STATUS_REENUM_TIMEOUT;
    }

    /* 8. Grace pause. */
    flash_sleep_ms(grace_ms);

    /* 9. Post-flash HELLO. */
    flash_pulse(progress_cb, progress_user, SC_FLASH_PHASE_POST_FLASH_HELLO);
    char hello_response[SC_HELLO_RESPONSE_MAX];
    char hello_err[256];
    if (!sc_transport_send_hello(transport, new_device_path,
                                  hello_response, sizeof(hello_response),
                                  hello_err, sizeof(hello_err))) {
        flash_set_error(error_buf, error_size,
                        "post-flash HELLO transport: %s", hello_err);
        return SC_FLASH_STATUS_POST_FLASH_HELLO_FAILED;
    }
    ScIdentityData parsed;
    if (!parse_hello_identity(hello_response, &parsed)) {
        flash_set_error(error_buf, error_size,
                        "post-flash HELLO unparseable: %.180s",
                        hello_response);
        return SC_FLASH_STATUS_POST_FLASH_HELLO_FAILED;
    }

    if (have_manifest && manifest.fw_version[0] != '\0') {
        if (strcmp(parsed.fw_version, manifest.fw_version) != 0) {
            flash_set_error(error_buf, error_size,
                            "post-flash fw_version mismatch: device='%s' "
                            "manifest='%s'",
                            parsed.fw_version, manifest.fw_version);
            return SC_FLASH_STATUS_POST_FLASH_FW_MISMATCH;
        }
    }

    flash_set_error(error_buf, error_size,
                    "OK: %s flashed, fw=%s, build=%s",
                    parsed.module_name, parsed.fw_version, parsed.build_id);
    return SC_FLASH_STATUS_OK;
}

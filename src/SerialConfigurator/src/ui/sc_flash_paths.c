#include "sc_flash_paths.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_JSON_BYTES (64u * 1024u)

/* Module display names keyed by SC_MODULE_COUNT index. MUST stay in
 * sync with sc_core's k_module_defs[] — the entry order is part of
 * the on-disk schema. Adjustometer is intentionally absent (v1.32
 * policy lock). */
static const char *const k_module_names[SC_MODULE_COUNT] = {
    "ECU",
    "Clocks",
    "OilAndSpeed",
};

static const char *s_test_override = NULL;
static char s_default_buf[1024];

void sc_flash_paths_init(ScFlashPaths *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

void sc_flash_paths_set_test_override(const char *path_or_null)
{
    s_test_override = path_or_null;
}

const char *sc_flash_paths_default_file(void)
{
    if (s_test_override != NULL) {
        return s_test_override;
    }

#ifdef _WIN32
    /* Windows path resolver lands in a future packaging slice
     * (provider §4.1 implementation rule). Keep the API shape but
     * return NULL so save/load fail closed. */
    return NULL;
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        (void)snprintf(s_default_buf, sizeof(s_default_buf),
                       "%s/fiesta-configurator/flash-paths.json", xdg);
        return s_default_buf;
    }
    const char *home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        (void)snprintf(s_default_buf, sizeof(s_default_buf),
                       "%s/.config/fiesta-configurator/flash-paths.json", home);
        return s_default_buf;
    }
    /* No HOME — degrade to /tmp so the GUI stays usable. */
    (void)snprintf(s_default_buf, sizeof(s_default_buf),
                   "/tmp/fiesta-configurator-flash-paths.json");
    return s_default_buf;
#endif
}

static int find_index_by_name(const char *name)
{
    if (name == NULL) {
        return -1;
    }
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        if (strcmp(k_module_names[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

const char *sc_flash_paths_get_uf2(const ScFlashPaths *p,
                                   const char *module_display_name)
{
    if (p == NULL) return "";
    const int idx = find_index_by_name(module_display_name);
    if (idx < 0) return "";
    return p->entries[idx].uf2;
}

const char *sc_flash_paths_get_manifest(const ScFlashPaths *p,
                                        const char *module_display_name)
{
    if (p == NULL) return "";
    const int idx = find_index_by_name(module_display_name);
    if (idx < 0) return "";
    return p->entries[idx].manifest;
}

static void copy_path(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0u) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

void sc_flash_paths_set_uf2(ScFlashPaths *p,
                            const char *module_display_name,
                            const char *path)
{
    if (p == NULL) return;
    const int idx = find_index_by_name(module_display_name);
    if (idx < 0) return;
    copy_path(p->entries[idx].uf2,
              sizeof(p->entries[idx].uf2),
              path);
}

void sc_flash_paths_set_manifest(ScFlashPaths *p,
                                 const char *module_display_name,
                                 const char *path)
{
    if (p == NULL) return;
    const int idx = find_index_by_name(module_display_name);
    if (idx < 0) return;
    copy_path(p->entries[idx].manifest,
              sizeof(p->entries[idx].manifest),
              path);
}

/* ── Tiny JSON reader/writer (focused on flash-paths.json schema) ───── */

typedef struct {
    const char *p;
    const char *end;
} json_cursor_t;

static void skip_ws(json_cursor_t *c)
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

static bool consume_char(json_cursor_t *c, char expected)
{
    skip_ws(c);
    if (c->p >= c->end || *c->p != expected) {
        return false;
    }
    c->p++;
    return true;
}

/* Parse a JSON string starting at '"', write into out (NUL terminated).
 * Supports \" \\ escapes; rejects everything else. */
static bool parse_string(json_cursor_t *c, char *out, size_t out_size)
{
    if (out_size == 0u) return false;
    if (!consume_char(c, '"')) return false;

    size_t w = 0u;
    while (c->p < c->end) {
        char ch = *c->p++;
        if (ch == '"') {
            if (w + 1u > out_size) return false;
            out[w] = '\0';
            return true;
        }
        if (ch == '\\') {
            if (c->p >= c->end) return false;
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
            return false;
        }
        if (w + 1u >= out_size) {
            return false;
        }
        out[w++] = ch;
    }
    return false;
}

static bool parse_inner_object(json_cursor_t *c, ScFlashPathsEntry *entry)
{
    if (!consume_char(c, '{')) return false;
    skip_ws(c);

    /* Empty object is fine. */
    if (c->p < c->end && *c->p == '}') {
        c->p++;
        return true;
    }

    bool first = true;
    while (true) {
        skip_ws(c);
        if (!first) {
            if (!consume_char(c, ',')) {
                if (consume_char(c, '}')) return true;
                return false;
            }
            skip_ws(c);
        }
        first = false;

        char key[32];
        if (!parse_string(c, key, sizeof(key))) return false;
        if (!consume_char(c, ':')) return false;
        skip_ws(c);

        char value[SC_FLASH_PATHS_PATH_MAX];
        if (!parse_string(c, value, sizeof(value))) return false;

        if (strcmp(key, "uf2_path") == 0) {
            copy_path(entry->uf2, sizeof(entry->uf2), value);
        } else if (strcmp(key, "manifest_path") == 0) {
            copy_path(entry->manifest, sizeof(entry->manifest), value);
        }
        /* Unknown inner keys silently ignored — forward-compat hook. */

        skip_ws(c);
        if (c->p < c->end && *c->p == '}') {
            c->p++;
            return true;
        }
    }
}

static bool parse_top_object(const char *json, size_t json_len,
                             ScFlashPaths *out)
{
    json_cursor_t c = { json, json + json_len };

    if (!consume_char(&c, '{')) return false;
    skip_ws(&c);

    if (c.p < c.end && *c.p == '}') {
        c.p++;
        return true;
    }

    bool first = true;
    while (true) {
        skip_ws(&c);
        if (!first) {
            if (!consume_char(&c, ',')) {
                if (consume_char(&c, '}')) break;
                return false;
            }
            skip_ws(&c);
        }
        first = false;

        char module_name[64];
        if (!parse_string(&c, module_name, sizeof(module_name))) return false;
        if (!consume_char(&c, ':')) return false;

        ScFlashPathsEntry tmp;
        memset(&tmp, 0, sizeof(tmp));
        if (!parse_inner_object(&c, &tmp)) return false;

        const int idx = find_index_by_name(module_name);
        if (idx >= 0) {
            out->entries[idx] = tmp;
        }
        /* Unknown top-level module names (e.g. legacy "Adjustometer") are
         * silently dropped — forward-compat for schema additions and
         * back-compat for old configs that included Adjustometer. */

        skip_ws(&c);
        if (c.p < c.end && *c.p == '}') {
            c.p++;
            break;
        }
    }

    skip_ws(&c);
    if (c.p != c.end) return false;
    return true;
}

/* Append @p src into @p dst with " escaped to \" and \\ to \\\\.
 * Writes a NUL terminator. Returns false if dst would overflow. */
static bool append_json_string(char *dst, size_t dst_size,
                               size_t *pos, const char *src)
{
    if (dst == NULL || pos == NULL) return false;
    if (*pos + 1u >= dst_size) return false;
    dst[(*pos)++] = '"';
    if (src != NULL) {
        for (const char *p = src; *p != '\0'; ++p) {
            char ch = *p;
            if (ch == '"' || ch == '\\') {
                if (*pos + 2u >= dst_size) return false;
                dst[(*pos)++] = '\\';
                dst[(*pos)++] = ch;
            } else if ((unsigned char)ch < 0x20u) {
                /* Reject control chars in stored paths — would not
                 * round-trip cleanly. Real filesystem paths never
                 * contain these. */
                return false;
            } else {
                if (*pos + 1u >= dst_size) return false;
                dst[(*pos)++] = ch;
            }
        }
    }
    if (*pos + 1u >= dst_size) return false;
    dst[(*pos)++] = '"';
    dst[*pos] = '\0';
    return true;
}

static bool serialize(const ScFlashPaths *in, char *out, size_t out_size)
{
    if (out_size < 4u) return false;
    size_t pos = 0u;
    out[pos++] = '{';
    out[pos++] = '\n';

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        if (i > 0u) {
            if (pos + 2u >= out_size) return false;
            out[pos++] = ',';
            out[pos++] = '\n';
        }
        if (pos + 2u >= out_size) return false;
        out[pos++] = ' ';
        out[pos++] = ' ';
        if (!append_json_string(out, out_size, &pos, k_module_names[i])) return false;
        if (pos + 2u >= out_size) return false;
        out[pos++] = ':';
        out[pos++] = ' ';

        if (pos + 14u >= out_size) return false;
        out[pos++] = '{';
        out[pos++] = ' ';
        if (!append_json_string(out, out_size, &pos, "uf2_path")) return false;
        if (pos + 2u >= out_size) return false;
        out[pos++] = ':';
        out[pos++] = ' ';
        if (!append_json_string(out, out_size, &pos, in->entries[i].uf2)) return false;
        if (pos + 2u >= out_size) return false;
        out[pos++] = ',';
        out[pos++] = ' ';
        if (!append_json_string(out, out_size, &pos, "manifest_path")) return false;
        if (pos + 2u >= out_size) return false;
        out[pos++] = ':';
        out[pos++] = ' ';
        if (!append_json_string(out, out_size, &pos, in->entries[i].manifest)) return false;
        if (pos + 2u >= out_size) return false;
        out[pos++] = ' ';
        out[pos++] = '}';
    }

    if (pos + 3u >= out_size) return false;
    out[pos++] = '\n';
    out[pos++] = '}';
    out[pos++] = '\n';
    out[pos] = '\0';
    return true;
}

/* ── File IO ────────────────────────────────────────────────────────── */

bool sc_flash_paths_load(ScFlashPaths *out)
{
    if (out == NULL) return false;
    sc_flash_paths_init(out);

    const char *path = sc_flash_paths_default_file();
    if (path == NULL) return false;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        /* Missing file is the normal "no remembered paths yet" state. */
        return true;
    }

    if (fseek(f, 0, SEEK_END) != 0) { (void)fclose(f); return false; }
    const long len = ftell(f);
    if (len < 0 || (size_t)len > MAX_JSON_BYTES) {
        (void)fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) { (void)fclose(f); return false; }

    char *buf = (char *)malloc((size_t)len + 1u);
    if (buf == NULL) { (void)fclose(f); return false; }
    const size_t n = fread(buf, 1u, (size_t)len, f);
    (void)fclose(f);
    if (n != (size_t)len) {
        free(buf);
        return false;
    }
    buf[len] = '\0';

    const bool ok = parse_top_object(buf, (size_t)len, out);
    free(buf);
    if (!ok) {
        sc_flash_paths_init(out);
        return false;
    }
    return true;
}

#ifndef _WIN32
static bool ensure_parent_dir(const char *path)
{
    /* Walk @p path and `mkdir` every '/'-separated prefix. Best effort:
     * EEXIST is fine, anything else fails the save. */
    char tmp[1024];
    (void)snprintf(tmp, sizeof(tmp), "%s", path);
    const size_t n = strlen(tmp);
    for (size_t i = 1u; i < n; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            tmp[i] = '/';
        }
    }
    return true;
}
#endif

bool sc_flash_paths_save(const ScFlashPaths *in)
{
    if (in == NULL) return false;
    const char *path = sc_flash_paths_default_file();
    if (path == NULL) return false;

#ifndef _WIN32
    if (!ensure_parent_dir(path)) return false;
#endif

    char buf[8 * 1024];
    if (!serialize(in, buf, sizeof(buf))) return false;

    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    const size_t len = strlen(buf);
    const size_t n = fwrite(buf, 1u, len, f);
    (void)fclose(f);
    return n == len;
}

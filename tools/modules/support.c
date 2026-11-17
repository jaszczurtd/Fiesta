#include "modules.h"
#include <jb/literal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FM_BYTES = 16 * 1024 * 1024 };
static hal_status_t fm_json_sink(void *user, const char *data, size_t length);
void fm_string(jb_string_t *s) { (void)jb_string_init(s, jb_heap_allocator()); }
void fm_init(fm_context *c) {
  *c = (fm_context){.status = HAL_OK};
  fm_string(&c->error);
}
void fm_status(fm_context *c, hal_status_t status) {
  if (c->status == HAL_OK && status != HAL_OK)
    c->status = status;
}
static void formatted(fm_context *c, jb_string_t *out, const char *format,
                      va_list args) {
  if (c->status != HAL_OK)
    return;
  va_list measure;
  va_copy(measure, args);
  int length = vsnprintf(NULL, 0, format, measure);
  va_end(measure);
  if (length == 0)
    return;
  if (length < 0) {
    fm_status(c, HAL_EIO);
    return;
  }
  size_t old = jb_string_size(out);
  if ((size_t)length > FM_BYTES || old > FM_BYTES - (size_t)length) {
    fm_status(c, HAL_EOVERFLOW);
    return;
  }
  fm_status(c, jb_string_resize(out, old + (size_t)length, '\0'));
  if (c->status == HAL_OK &&
      vsnprintf(jb_string_mutable_data(out) + old, (size_t)length + 1, format,
                args) != length)
    fm_status(c, HAL_EIO);
}
void fm_printf(fm_context *c, jb_string_t *out, const char *format, ...) {
  va_list args;
  va_start(args, format);
  formatted(c, out, format, args);
  va_end(args);
}
void fm_error(fm_context *c, const char *format, ...) {
  if (c->status != HAL_OK)
    return;
  va_list args;
  va_start(args, format);
  formatted(c, &c->error, format, args);
  va_end(args);
  fm_status(c, HAL_ECONFIG);
}
void fm_append(fm_context *c, jb_string_t *out, const char *text) {
  if (c->status != HAL_OK)
    return;
  size_t n = strlen(text);
  if (n > FM_BYTES || jb_string_size(out) > FM_BYTES - n)
    fm_status(c, HAL_EOVERFLOW);
  else
    fm_status(c, jb_string_append(out, text, n));
}
/* JSON output uses the shared writer, including NUL and non-ASCII escaping. */
void fm_json(fm_context *c, jb_string_t *out, const char *text) {
  if (c->status != HAL_OK)
    return;
  if (text == NULL) {
    fm_append(c, out, "null");
    return;
  }
  jb_json_writer_t writer = {0};
  const jb_json_writer_limits_t limits = {FM_BYTES, 0, 0, 0, 0};
  size_t bytes = 0;
  fm_status(c, jb_json_writer_workspace_size(&limits, &bytes));
  void *work = c->status == HAL_OK ? malloc(bytes) : NULL;
  if (c->status == HAL_OK && work == NULL)
    fm_status(c, HAL_ENOMEM);
  if (c->status == HAL_OK) {
    fm_status(c, jb_json_writer_init(&writer, &limits,
                                     (jb_json_workspace_t){work, bytes},
                                     fm_json_sink, out));
    if (c->status == HAL_OK)
      fm_status(c, jb_json_writer_string(&writer,
                                         (jb_json_span_t){text, strlen(text)}));
    if (c->status == HAL_OK)
      fm_status(c, jb_json_writer_finish(&writer, &bytes));
  }
  free(work);
}
static hal_status_t fm_json_sink(void *user, const char *data, size_t length) {
  jb_string_t *out = user;
  if (length > FM_BYTES || jb_string_size(out) > FM_BYTES - length)
    return HAL_EOVERFLOW;
  return jb_string_append(out, data, length);
}
void fm_literal(fm_context *c, jb_string_t *out, const char *text) {
  jb_string_t json = {0};
  fm_string(&json);
  fm_json(c, &json, text);
  const jb_json_limits_t limits = {FM_BYTES, 1, 0, FM_BYTES, 0, 0, 0};
  size_t bytes = 0;
  if (c->status == HAL_OK)
    fm_status(c, jb_json_parse_workspace_size(&limits, &bytes));
  void *work = c->status == HAL_OK ? malloc(bytes) : NULL;
  if (c->status == HAL_OK && work == NULL)
    fm_status(c, HAL_ENOMEM);
  jb_json_token_t token = {0};
  jb_json_doc_t doc = {0};
  jb_json_node_t node = {0};
  if (c->status == HAL_OK)
    fm_status(c, jb_json_parse((jb_json_span_t){jb_string_data(&json),
                                                jb_string_size(&json)},
                               &limits, &token, 1,
                               (jb_json_workspace_t){work, bytes}, &doc, NULL));
  if (c->status == HAL_OK)
    fm_status(c, jb_json_root(&doc, &node));
  size_t needed = 0, old = jb_string_size(out);
  if (c->status == HAL_OK) {
    hal_status_t s = jb_c_literal(node, JB_C_STRING, (jb_json_workspace_t){0},
                                  FM_BYTES, NULL, 0, &needed);
    if (s != HAL_EOVERFLOW)
      fm_status(c, s);
    if (needed > FM_BYTES || old > FM_BYTES - needed)
      fm_status(c, HAL_EOVERFLOW);
  }
  if (c->status == HAL_OK)
    fm_status(c, jb_string_resize(out, old + needed, '\0'));
  if (c->status == HAL_OK)
    fm_status(c, jb_c_literal(node, JB_C_STRING, (jb_json_workspace_t){0},
                              FM_BYTES, jb_string_mutable_data(out) + old,
                              needed, &needed));
  free(work);
  jb_string_destroy(&json);
}
void fm_format(fm_context *c, jb_string_t *out, fm_value value) {
  const jb_json_tree_format_t format = {FM_BYTES, 0, false, false};
  size_t bytes = 0;
  if (c->status == HAL_OK)
    fm_status(c,
              jb_json_tree_format(value, &format, fm_json_sink, out, &bytes));
}
void fm_read(fm_context *c, const char *path, jb_json_tree_t **tree) {
  if (c->status != HAL_OK)
    return;
  jb_file_data_t data = {0};
  hal_status_t s = jb_file_read(path, FM_BYTES, &data);
  const jb_json_limits_t parse = {
      FM_BYTES, 100000, 128, FM_BYTES, FM_BYTES, 100000, 64 * 1024 * 1024};
  const jb_json_tree_limits_t limits = {100000, 128, FM_BYTES,
                                        64 * 1024 * 1024};
  if (s == HAL_OK)
    s = jb_json_tree_parse(
        (jb_json_span_t){(const char *)data.data, data.length}, &parse, &limits,
        JB_JSON_TREE_LAST_DUPLICATE | JB_JSON_TREE_NONFINITE, tree, NULL);
  if (s != HAL_OK)
    fm_error(c, "cannot read %s (status %d)", path, (int)s);
  jb_file_release(&data);
}
fm_value fm_root(const jb_json_tree_t *tree) {
  fm_value v = {0};
  if (tree != NULL)
    (void)jb_json_tree_root(tree, &v);
  return v;
}
fm_value fm_get(fm_value object, const char *key) {
  fm_value v = {0};
  (void)jb_json_tree_find(object, (jb_json_span_t){key, strlen(key)}, &v);
  return v;
}
fm_value fm_at(fm_value array, size_t index) {
  fm_value v = {0};
  (void)jb_json_tree_at(array, index, &v);
  return v;
}
jb_json_type_t fm_type(fm_value value) {
  jb_json_type_t type = UINT32_MAX;
  (void)jb_json_tree_type(value, &type);
  return type;
}
size_t fm_count(fm_value value) {
  size_t n = 0;
  (void)jb_json_tree_count(value, &n);
  return n;
}
bool fm_equal(jb_json_span_t value, const char *text) {
  return value.length == strlen(text) &&
         memcmp(value.data, text, value.length) == 0;
}
char *fm_text(fm_context *c, fm_value value, const char *where, bool ascii) {
  if (c->status != HAL_OK)
    return NULL;
  jb_json_span_t s = {0};
  bool valid = fm_type(value) == JB_JSON_STRING &&
               jb_json_tree_text(value, &s) == HAL_OK;
  if (ascii && s.length == 0)
    valid = false;
  for (size_t i = 0; valid && i < s.length; ++i)
    if (s.data[i] == '\0' || (ascii && ((unsigned char)s.data[i] < 32 ||
                                        (unsigned char)s.data[i] > 126)))
      valid = false;
  if (!valid) {
    fm_error(c, "%s: expected %sstring", where,
             ascii ? "a non-empty printable ASCII " : "a ");
    return NULL;
  }
  char *copy = malloc(s.length + 1);
  if (copy == NULL) {
    fm_status(c, HAL_ENOMEM);
    return NULL;
  }
  memcpy(copy, s.data, s.length);
  copy[s.length] = '\0';
  return copy;
}
bool fm_flag(fm_context *c, fm_value object, const char *key, bool fallback,
             const char *where) {
  fm_value v = fm_get(object, key);
  if (v.tree == NULL)
    return fallback;
  bool result = false;
  if (jb_json_tree_get_bool(v, &result) != HAL_OK)
    fm_error(c, "%s.%s: expected true or false", where, key);
  return result;
}

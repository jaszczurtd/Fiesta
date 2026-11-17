#ifndef FIESTA_MODULES_INTERNAL_H
#define FIESTA_MODULES_INTERNAL_H

/* Private model shared by the Fiesta registry command's C translation units. */
#include <jb/array.h>
#include <jb/file.h>
#include <jb/filesystem.h>
#include <jb/json_tree.h>
#include <jb/string.h>

typedef jb_json_tree_view_t fm_value;
typedef struct {
  hal_status_t status;
  jb_string_t error;
  size_t comparisons;
} fm_context;
typedef struct {
  char *name, *token, *macro, *display, *host_tests, *hint;
  bool firmware, inactive, sc, tests, tests_drive, ci, cppcheck;
  size_t index;
  fm_value definition, shared;
} fm_module;
typedef struct {
  jb_json_tree_t *document;
  fm_module *modules;
  fm_module **sc;
  size_t count, sc_count;
} fm_registry;

void fm_init(fm_context *c);
void fm_status(fm_context *c, hal_status_t status);
void fm_error(fm_context *c, const char *format, ...);
void fm_string(jb_string_t *s);
void fm_append(fm_context *c, jb_string_t *out, const char *text);
void fm_printf(fm_context *c, jb_string_t *out, const char *format, ...);
void fm_json(fm_context *c, jb_string_t *out, const char *text);
void fm_literal(fm_context *c, jb_string_t *out, const char *text);
void fm_format(fm_context *c, jb_string_t *out, fm_value value);
void fm_read(fm_context *c, const char *path, jb_json_tree_t **tree);
fm_value fm_root(const jb_json_tree_t *tree);
fm_value fm_get(fm_value object, const char *key);
fm_value fm_at(fm_value array, size_t index);
jb_json_type_t fm_type(fm_value value);
size_t fm_count(fm_value value);
bool fm_equal(jb_json_span_t value, const char *text);
char *fm_text(fm_context *c, fm_value value, const char *where, bool ascii);
bool fm_flag(fm_context *c, fm_value object, const char *key, bool fallback,
             const char *where);
void fm_load(fm_context *c, const char *root, fm_registry *registry);
void fm_destroy(fm_registry *registry);
void fm_render(fm_context *c, const fm_registry *registry, jb_string_t out[3]);
void fm_report(fm_context *c, const fm_registry *registry,
               const jb_string_t out[3], jb_string_t *json);
void fm_shared_files(fm_context *c, const char *root, const char *set,
                     jb_string_t *out);
void fm_extra_sources(fm_context *c, const char *root, const fm_module *module,
                      jb_string_t *out);
void fm_apply_sources(fm_context *c, const char *path, const fm_module *module,
                      const char *root, jb_string_t *out);
extern const char *const fm_outputs[3];
#endif

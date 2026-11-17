#include "fiesta_shared_sets.h"
#include "modules.h"
#include <jb/json_number.h>
#include <stdlib.h>
#include <string.h>

static int key_order(const void *a, const void *b) {
  const jb_json_span_t *left = a, *right = b;
  const size_t length =
      left->length < right->length ? left->length : right->length;
  const int order = memcmp(left->data, right->data, length);
  return order
             ? order
             : (left->length > right->length) - (left->length < right->length);
}
static void keys(fm_context *c, fm_value object, const char *const *allowed,
                 size_t count, const char *where) {
  if (fm_type(object) != JB_JSON_OBJECT) {
    fm_error(c, "%s: expected an object", where);
    return;
  }
  const size_t capacity = fm_count(object);
  jb_json_span_t *unknown = capacity ? calloc(capacity, sizeof *unknown) : NULL;
  if (capacity && !unknown) {
    fm_status(c, HAL_ENOMEM);
    return;
  }
  size_t used = 0;
  for (size_t i = 0; c->status == HAL_OK && i < capacity; ++i) {
    jb_json_span_t key = {0};
    fm_status(c, jb_json_tree_key(fm_at(object, i), &key));
    bool known = false;
    for (size_t j = 0; j < count; ++j)
      known |= fm_equal(key, allowed[j]);
    if (!known)
      unknown[used++] = key;
  }
  if (used) {
    qsort(unknown, used, sizeof *unknown, key_order);
    jb_string_t list = {0};
    fm_string(&list);
    for (size_t i = 0; i < used; ++i)
      fm_printf(c, &list, "%s'%.*s'", i ? ", " : "", (int)unknown[i].length,
                unknown[i].data);
    fm_error(c, "%s: unknown keys [%s]", where, jb_string_data(&list));
    jb_string_destroy(&list);
  }
  free(unknown);
}
static bool any_key(fm_value object, const char *const *names, size_t count) {
  for (size_t i = 0; i < count; ++i)
    if (fm_get(object, names[i]).tree != NULL)
      return true;
  return false;
}
static bool number(fm_context *c, fm_value value, bool integer_token,
                   uint64_t *result) {
  if (c->status != HAL_OK)
    return false;
  jb_json_span_t text = {0};
  if (fm_type(value) != JB_JSON_NUMBER ||
      jb_json_tree_text(value, &text) != HAL_OK)
    return false;
  if (integer_token && (memchr(text.data, '.', text.length) ||
                        memchr(text.data, 'e', text.length) ||
                        memchr(text.data, 'E', text.length)))
    return false;
  const jb_json_limits_t limits = {16 * 1024 * 1024, 1, 0, 0,
                                   16 * 1024 * 1024, 0, 0};
  size_t bytes = 0;
  if (jb_json_parse_workspace_size(&limits, &bytes) != HAL_OK)
    return false;
  void *work = malloc(bytes);
  if (work == NULL) {
    fm_status(c, HAL_ENOMEM);
    return false;
  }
  jb_json_token_t token = {0};
  jb_json_doc_t doc = {0};
  jb_json_node_t node = {0};
  bool ok =
      jb_json_parse(text, &limits, &token, 1,
                    (jb_json_workspace_t){work, bytes}, &doc, NULL) == HAL_OK;
  if (ok)
    ok = jb_json_root(&doc, &node) == HAL_OK &&
         jb_json_get_u64(node, result) == HAL_OK;
  free(work);
  return ok;
}
static char *module_text(fm_context *c, fm_module *m, fm_value raw,
                         const char *field, bool ascii) {
  jb_string_t where = {0};
  fm_string(&where);
  fm_printf(c, &where, "module %s.%s", m->name, field);
  char *result = fm_text(c, raw, jb_string_data(&where), ascii);
  jb_string_destroy(&where);
  return result;
}
static void serial(fm_context *c, fm_module *m, fm_value raw) {
  static const char *const allowed[] = {"index", "displayName", "tests",
                                        "testsDrive"};
  jb_string_t where = {0};
  fm_string(&where);
  fm_printf(c, &where, "module %s.serialConfigurator", m->name);
  bool valid = fm_type(raw) == JB_JSON_OBJECT;
  for (size_t i = 0; valid && i < fm_count(raw); ++i) {
    jb_json_span_t key = {0};
    fm_status(c, jb_json_tree_key(fm_at(raw, i), &key));
    bool known = false;
    for (size_t j = 0; j < COUNTOF(allowed); ++j)
      known |= fm_equal(key, allowed[j]);
    valid = known;
  }
  if (!valid)
    fm_error(
        c, "%s: expected keys ['displayName', 'index', 'tests', 'testsDrive']",
        jb_string_data(&where));
  uint64_t index = 0;
  if (!number(c, fm_get(raw, "index"), true, &index))
    fm_error(c, "module %s.serialConfigurator.index: expected an integer >= 0",
             m->name);
  if (index > SIZE_MAX)
    fm_error(c, "SerialConfigurator indices must be contiguous without gaps");
  m->index = (size_t)index;
  m->tests = fm_flag(c, raw, "tests", false, jb_string_data(&where));
  m->tests_drive = fm_flag(c, raw, "testsDrive", false, jb_string_data(&where));
  if (m->tests_drive && !m->tests)
    fm_error(c, "%s.testsDrive: needs tests", jb_string_data(&where));
  m->display = module_text(c, m, fm_get(raw, "displayName"),
                           "serialConfigurator.displayName", true);
  jb_string_destroy(&where);
  m->sc = true;
}
static void macro(fm_context *c, fm_module *m) {
  m->macro = module_text(c, m, fm_get(m->definition, "macro"), "macro", true);
  if (c->status != HAL_OK)
    return;
  bool valid = m->macro[0] >= 'A' && m->macro[0] <= 'Z';
  for (const char *p = m->macro + 1; *p; ++p)
    valid &= (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_';
  if (!valid)
    fm_error(c, "module %s.macro: expected an upper-case C identifier part",
             m->name);
}
static void identity(fm_context *c, const char *root, fm_module *m) {
  jb_string_t path = {0};
  fm_string(&path);
  fm_printf(c, &path, "%s/src/%s/.vscode/jaszczurhal.project.json", root,
            m->name);
  jb_json_tree_t *tree = NULL;
  if (c->status == HAL_OK)
    fm_read(c, jb_string_data(&path), &tree);
  fm_value id = fm_get(fm_root(tree), "identity");
  if (c->status == HAL_OK && fm_type(id) != JB_JSON_OBJECT)
    fm_error(c, "%s: identity in %s is not an object", m->name,
             jb_string_data(&path));
  if (fm_get(id, "usbVid").tree || fm_get(id, "usbPid").tree)
    fm_error(c, "%s: USB VID/PID must be inherited from HAL target metadata",
             m->name);
  /* Only strings can have the required Python str() prefixes. */
  jb_json_span_t product = {0}, hint = {0};
  (void)jb_json_tree_text(fm_get(id, "usbProduct"), &product);
  (void)jb_json_tree_text(fm_get(id, "byIdHint"), &hint);
  if (fm_type(fm_get(id, "usbProduct")) != JB_JSON_STRING ||
      product.length < 7 || memcmp(product.data, "Fiesta ", 7) != 0)
    fm_error(c, "%s: usbProduct must start with 'Fiesta '", m->name);
  if (fm_type(fm_get(id, "byIdHint")) != JB_JSON_STRING || hint.length < 7 ||
      memcmp(hint.data, "Fiesta_", 7) != 0)
    fm_error(c, "%s: byIdHint must start with 'Fiesta_'", m->name);
  m->hint = fm_text(c, fm_get(id, "byIdHint"), "byIdHint", false);
  jb_json_tree_destroy(tree);
  jb_string_destroy(&path);
}
static void choices(fm_context *c, const fm_module *m, fm_value value,
                    const char *const *allowed, size_t count,
                    const char *field) {
  if (value.tree == NULL)
    return;
  bool valid = fm_type(value) == JB_JSON_ARRAY;
  for (size_t i = 0; valid && c->status == HAL_OK && i < fm_count(value); ++i) {
    fm_value item = fm_at(value, i);
    jb_json_span_t text = {0};
    bool known = false;
    if (fm_type(item) == JB_JSON_STRING &&
        jb_json_tree_text(item, &text) == HAL_OK)
      for (size_t j = 0; j < count; ++j)
        known |= fm_equal(text, allowed[j]);
    valid = known;
  }
  if (!valid) {
    jb_string_t values = {0};
    fm_string(&values);
    for (size_t i = 0; i < count; ++i)
      fm_printf(c, &values, "%s'%s'", i ? ", " : "", allowed[i]);
    fm_error(c, "module %s.%s: allowed values [%s]", m->name, field,
             jb_string_data(&values));
    jb_string_destroy(&values);
  }
}
static void module(fm_context *c, const char *root, fm_module *m,
                   size_t index) {
  static const char *const allowed[] = {
      "name",      "kind",     "token", "macro",        "serialConfigurator",
      "hostTests", "analysis", "ci",    "sharedSources"};
  static const char *const inactive[] = {"token", "hostTests", "analysis",
                                         "sharedSources", "ci"};
  static const char *const firmware[] = {"token", "macro",
                                         "serialConfigurator"};
  static const char *const analyses[] = {"cppcheck"};
  fm_value raw = m->definition;
  jb_string_t location = {0};
  fm_string(&location);
  fm_printf(c, &location, "modules[%zu]", index);
  keys(c, raw, allowed, COUNTOF(allowed), jb_string_data(&location));
  fm_append(c, &location, ".name");
  m->name = fm_text(c, fm_get(raw, "name"), jb_string_data(&location), true);
  jb_string_destroy(&location);
  if (c->status != HAL_OK)
    return;
  jb_json_span_t kind = {0};
  (void)jb_json_tree_text(fm_get(raw, "kind"), &kind);
  m->firmware = fm_equal(kind, "firmware");
  m->inactive = fm_equal(kind, "inactive");
  jb_string_t path = {0};
  fm_string(&path);
  fm_printf(c, &path, "%s/src/%s", root, m->name);
  jb_fs_info_t info = {0};
  if (strchr(m->name, '/') || strchr(m->name, '\\') ||
      (!m->inactive &&
       (jb_fs_status(jb_string_data(&path), true, &info) != HAL_OK ||
        info.kind != JB_FS_DIRECTORY)))
    fm_error(c, "module %s: src/%s is not a module directory", m->name,
             m->name);
  jb_string_destroy(&path);
  if (!m->firmware && !m->inactive && !fm_equal(kind, "host-app"))
    fm_error(
        c,
        "module %s: kind must be one of ['firmware', 'host-app', 'inactive']",
        m->name);
  if (m->firmware) {
    m->token = module_text(c, m, fm_get(raw, "token"), "token", true);
    macro(c, m);
    if (fm_get(raw, "serialConfigurator").tree)
      serial(c, m, fm_get(raw, "serialConfigurator"));
    identity(c, root, m);
  } else if (m->inactive) {
    if (any_key(raw, inactive, COUNTOF(inactive)))
      fm_error(c, "module %s: inactive modules have no firmware, tests or CI",
               m->name);
    macro(c, m);
    serial(c, m, fm_get(raw, "serialConfigurator"));
    if (m->tests)
      fm_error(c, "module %s: inactive modules have no functional tests",
               m->name);
  } else if (any_key(raw, firmware, COUNTOF(firmware))) {
    fm_error(c,
             "module %s: only firmware modules have token, macro and "
             "serialConfigurator",
             m->name);
  }
  fm_value host = fm_get(raw, "hostTests");
  if (host.tree && fm_type(host) != JB_JSON_NULL) {
    m->host_tests = module_text(c, m, host, "hostTests", true);
    if (m->host_tests &&
        (strchr(m->host_tests, '/') || strchr(m->host_tests, '\\')))
      fm_error(c, "module %s.hostTests: expected a directory name", m->name);
  }
  fm_value analysis = fm_get(raw, "analysis");
  choices(c, m, analysis, analyses, COUNTOF(analyses), "analysis");
  m->cppcheck = fm_count(analysis) != 0;
  if (m->cppcheck && m->host_tests == NULL)
    fm_error(c, "module %s.analysis: needs hostTests", m->name);
  m->shared = fm_get(raw, "sharedSources");
  choices(c, m, m->shared, fm_shared_sets, COUNTOF(fm_shared_sets),
          "sharedSources");
  if (fm_count(m->shared) && !m->firmware)
    fm_error(c,
             "module %s.sharedSources: only firmware modules compile a shared "
             "source set",
             m->name);
  jb_string_t where = {0};
  fm_string(&where);
  fm_printf(c, &where, "module %s", m->name);
  m->ci = fm_flag(c, raw, "ci", !m->inactive, jb_string_data(&where));
  jb_string_destroy(&where);
}
static bool compare_budget(fm_context *c, const char *a, const char *b) {
  if (c->status != HAL_OK || !a || !b)
    return false;
  const size_t limit = 64 * 1024 * 1024, cost = 1 + strlen(a) + strlen(b);
  if (cost > limit - c->comparisons) {
    fm_status(c, HAL_EOVERFLOW);
    return false;
  }
  c->comparisons += cost;
  return true;
}
static void unique(fm_context *c, const char *a, const char *b,
                   const char *what) {
  if (compare_budget(c, a, b) && strcmp(a, b) == 0)
    fm_error(c, "duplicate %s: %s", what, a);
}
static int index_order(const void *a, const void *b) {
  const size_t left = *(const size_t *)a, right = *(const size_t *)b;
  return (left > right) - (left < right);
}
static void indices(fm_context *c, fm_registry *r) {
  if (c->status != HAL_OK || r->sc_count == 0)
    return;
  size_t *values = calloc(r->sc_count, sizeof *values);
  if (!values) {
    fm_status(c, HAL_ENOMEM);
    return;
  }
  size_t used = 0;
  for (size_t i = 0; i < r->count; ++i) {
    fm_module *m = &r->modules[i];
    if (m->sc) {
      values[used++] = m->index;
      if (m->index < r->sc_count)
        r->sc[m->index] = m;
    }
  }
  qsort(values, used, sizeof *values, index_order);
  bool valid = true;
  for (size_t i = 0; i < used; ++i)
    valid &= values[i] == i;
  if (!valid) {
    jb_string_t list = {0};
    fm_string(&list);
    for (size_t i = 0; i < used; ++i)
      fm_printf(c, &list, "%s%zu", i ? ", " : "", values[i]);
    fm_error(c,
             "SerialConfigurator indices must be 0..%zu without gaps, got [%s]",
             r->sc_count - 1, jb_string_data(&list));
    jb_string_destroy(&list);
  }
  free(values);
}
void fm_load(fm_context *c, const char *root, fm_registry *r) {
  jb_string_t path = {0};
  fm_string(&path);
  fm_printf(c, &path, "%s/modules.json", root);
  if (c->status == HAL_OK)
    fm_read(c, jb_string_data(&path), &r->document);
  fm_value doc = fm_root(r->document), schema = fm_get(doc, "schemaVersion");
  uint64_t version = 0;
  bool flag = false;
  bool valid = number(c, schema, false, &version) && version == 1;
  valid |= jb_json_tree_get_bool(schema, &flag) == HAL_OK && flag;
  if (!valid)
    fm_error(c, "%s: schemaVersion must be 1", jb_string_data(&path));
  fm_value list = fm_get(doc, "modules");
  if (fm_type(list) != JB_JSON_ARRAY || fm_count(list) == 0)
    fm_error(c, "%s: modules must be a non-empty list", jb_string_data(&path));
  jb_string_destroy(&path);
  if (c->status != HAL_OK)
    return;
  r->count = fm_count(list);
  r->modules = calloc(r->count, sizeof(*r->modules));
  r->sc = calloc(r->count, sizeof(*r->sc));
  if (!r->modules || !r->sc) {
    fm_status(c, HAL_ENOMEM);
    return;
  }
  for (size_t i = 0; c->status == HAL_OK && i < r->count; ++i) {
    r->modules[i].definition = fm_at(list, i);
    module(c, root, &r->modules[i], i);
    if (r->modules[i].sc)
      ++r->sc_count;
  }
  /* Match the former registry's ordering of independent duplicate checks. */
  for (size_t field = 0; c->status == HAL_OK && field < 4; ++field)
    for (size_t i = 0; c->status == HAL_OK && i < r->count; ++i)
      for (size_t j = 0; c->status == HAL_OK && j < i; ++j) {
        fm_module *a = &r->modules[i], *b = &r->modules[j];
        switch (field) {
        case 0:
          unique(c, a->name, b->name, "module name");
          break;
        case 1:
          unique(c, a->token, b->token, "token");
          break;
        case 2:
          unique(c, a->macro, b->macro, "macro");
          break;
        default:
          unique(c, a->hint, b->hint, "byIdHint");
          break;
        }
      }
  for (size_t i = 0; c->status == HAL_OK && i < r->count; ++i) {
    fm_module *m = &r->modules[i];
    if (m->hint)
      for (size_t j = 0; j < r->count; ++j) {
        fm_module *other = &r->modules[j];
        if (i != j && compare_budget(c, other->hint, m->hint) &&
            strstr(other->hint, m->hint))
          fm_error(c, "byIdHint %s also matches %s", m->hint, other->name);
      }
  }
  for (size_t i = 0; c->status == HAL_OK && i < r->count; ++i)
    for (size_t j = 0; c->status == HAL_OK && j < i; ++j)
      unique(c, r->modules[i].display, r->modules[j].display,
             "SerialConfigurator displayName");
  indices(c, r);
}
void fm_destroy(fm_registry *r) {
  if (r->modules)
    for (size_t i = 0; i < r->count; ++i) {
      fm_module *m = &r->modules[i];
      free(m->name);
      free(m->token);
      free(m->macro);
      free(m->display);
      free(m->host_tests);
      free(m->hint);
    }
  free(r->modules);
  free(r->sc);
  jb_json_tree_destroy(r->document);
  *r = (fm_registry){0};
}

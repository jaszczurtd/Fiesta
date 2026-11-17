/* Cases migrated from tests/test_module_registry.py. All writes stay under
 * the CTest work directory; production files are used only for byte parity. */
#include "modules.h"
#include <jb/process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

static size_t checks, failures;
#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      ++failures;                                                              \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);          \
    }                                                                          \
  } while (0)
static const jb_json_limits_t parse_limits = {1048576, 4096, 32,     1048576,
                                              4096,    4096, 4194304};
static const jb_json_tree_limits_t tree_limits = {4096, 32, 1048576, 4194304};
static const char baseline[] =
    "{\"schemaVersion\":1,\"modules\":["
    "{\"name\":\"ECU\",\"kind\":\"firmware\",\"token\":\"ECU\",\"macro\":"
    "\"ECU\","
    "\"serialConfigurator\":{\"index\":0,\"displayName\":\"ECU\",\"tests\":"
    "true},"
    "\"hostTests\":\"build_test\",\"analysis\":[\"cppcheck\"]},"
    "{\"name\":\"Fiesta_clock\",\"kind\":\"firmware\",\"token\":\"RTC_CLK\","
    "\"macro\":\"CLOCK\","
    "\"serialConfigurator\":{\"index\":1,\"displayName\":\"RTC_Clock\"}},"
    "{\"name\":\"Adjustometer\",\"kind\":\"firmware\",\"token\":\"ADJ\","
    "\"macro\":\"ADJUSTOMETER\",\"hostTests\":\"build_test\"},"
    "{\"name\":\"SerialConfigurator\",\"kind\":\"host-app\",\"hostTests\":"
    "\"build\"}]}";
static const char *const names[] = {"ECU", "Fiesta_clock", "Adjustometer",
                                    "SerialConfigurator"};
static jb_json_tree_t *parse(const char *text) {
  jb_json_tree_t *tree = NULL;
  CHECK(jb_json_tree_parse((jb_json_span_t){text, strlen(text)}, &parse_limits,
                           &tree_limits, 0, &tree, NULL) == HAL_OK);
  return tree;
}
static void write_file(const char *path, const char *text) {
  bool changed = false;
  CHECK(jb_file_write_atomic(path, text, strlen(text), false, &changed) ==
        HAL_OK);
}
static char *path_to(const char *root, const char *suffix) {
  jb_string_t result = {0};
  fm_string(&result);
  fm_context c;
  fm_init(&c);
  fm_printf(&c, &result, "%s/%s", root, suffix);
  CHECK(c.status == HAL_OK);
  char *copy = malloc(jb_string_size(&result) + 1);
  CHECK(copy != NULL);
  if (copy)
    memcpy(copy, jb_string_data(&result), jb_string_size(&result) + 1);
  jb_string_destroy(&c.error);
  jb_string_destroy(&result);
  return copy;
}
static void save(const char *root, const char *suffix,
                 const jb_json_tree_t *tree) {
  char *path = path_to(root, suffix);
  if (!path)
    return;
  jb_string_t text = {0};
  fm_string(&text);
  fm_context c;
  fm_init(&c);
  fm_format(&c, &text, fm_root(tree));
  CHECK(c.status == HAL_OK);
  if (c.status == HAL_OK)
    write_file(path, jb_string_data(&text));
  jb_string_destroy(&c.error);
  jb_string_destroy(&text);
  free(path);
}
static void reset(const char *root) {
  hal_status_t s = jb_fs_remove(root, true);
  CHECK(s == HAL_OK || s == HAL_ENOENT);
  CHECK(jb_fs_mkdir(root, true) == HAL_OK);
  char *registry = path_to(root, "modules.json");
  if (registry)
    write_file(registry, baseline);
  free(registry);
  for (size_t i = 0; i < COUNTOF(names); ++i) {
    jb_string_t suffix = {0}, text = {0};
    fm_string(&suffix);
    fm_string(&text);
    fm_context c;
    fm_init(&c);
    fm_printf(&c, &suffix, "src/%s/.vscode/jaszczurhal.project.json", names[i]);
    fm_printf(
        &c, &text,
        "{\"identity\":{\"enabled\":true,\"usbManufacturer\":\"Jaszczur\","
        "\"usbProduct\":\"Fiesta %s\",\"byIdHint\":\"Fiesta_%s\"}}",
        names[i], names[i]);
    CHECK(c.status == HAL_OK);
    char *path = path_to(root, jb_string_data(&suffix));
    if (path)
      write_file(path, jb_string_data(&text));
    free(path);
    jb_string_destroy(&suffix);
    jb_string_destroy(&text);
    jb_string_destroy(&c.error);
  }
}
/* Test-fixture overlay. A null member removes a field, as modified() did. */
static void overlay(jb_json_tree_t *tree, fm_value patch) {
  for (size_t i = 0; i < fm_count(patch); ++i) {
    fm_value v = fm_at(patch, i);
    jb_json_span_t key = {0};
    CHECK(jb_json_tree_key(v, &key) == HAL_OK);
    if (fm_type(v) == JB_JSON_NULL) {
      hal_status_t s = jb_json_tree_erase(tree, key);
      CHECK(s == HAL_OK || s == HAL_ENOENT);
    } else
      CHECK(jb_json_tree_set(tree, key, v) == HAL_OK);
  }
}
static void change_module(const char *root, size_t index,
                          const char *patch_text) {
  fm_context c;
  fm_init(&c);
  char *path = path_to(root, "modules.json");
  jb_json_tree_t *doc = NULL;
  if (path)
    fm_read(&c, path, &doc);
  CHECK(c.status == HAL_OK);
  jb_json_tree_t *patch = parse(patch_text), *array = parse("[]"), *copy = NULL;
  fm_value modules = fm_get(fm_root(doc), "modules");
  for (size_t i = 0; i < fm_count(modules); ++i) {
    CHECK(jb_json_tree_copy(fm_at(modules, i), &tree_limits, &copy) == HAL_OK);
    if (i == index)
      overlay(copy, fm_root(patch));
    CHECK(jb_json_tree_append(array, fm_root(copy)) == HAL_OK);
    jb_json_tree_destroy(copy);
    copy = NULL;
  }
  if (index == fm_count(modules))
    CHECK(jb_json_tree_append(array, fm_root(patch)) == HAL_OK);
  CHECK(jb_json_tree_set(doc, (jb_json_span_t){"modules", 7}, fm_root(array)) ==
        HAL_OK);
  save(root, "modules.json", doc);
  jb_json_tree_destroy(array);
  jb_json_tree_destroy(patch);
  jb_json_tree_destroy(doc);
  free(path);
  jb_string_destroy(&c.error);
}
static void rejected(const char *root, const char *expected) {
  fm_context c;
  fm_init(&c);
  fm_registry r = {0};
  fm_load(&c, root, &r);
  CHECK(c.status != HAL_OK);
  if (!strstr(jb_string_data(&c.error), expected))
    fprintf(stderr, "expected %s; got %s\n", expected,
            jb_string_data(&c.error));
  CHECK(strstr(jb_string_data(&c.error), expected) != NULL);
  fm_destroy(&r);
  jb_string_destroy(&c.error);
}
static void rules(const char *root) {
  static const struct {
    size_t index;
    const char *patch, *error;
  } cases[] = {
      {1, "{\"token\":\"ECU\"}", "duplicate token"},
      {1, "{\"macro\":\"ECU\"}", "duplicate macro"},
      {4, "{\"name\":\"SerialConfigurator\",\"kind\":\"host-app\"}",
       "duplicate module name"},
      {0, "{\"z\":1,\"board\":\"x\"}",
       "modules[0]: unknown keys ['board', 'z']"},
      {3, "{\"kind\":\"library\"}", "kind must be"},
      {3, "{\"token\":\"SC\"}", "only firmware modules"},
      {1,
       "{\"serialConfigurator\":{\"index\":2,\"displayName\":\"RTC_Clock\"}}",
       "without gaps, got [0, 2]"},
      {1,
       "{\"serialConfigurator\":{\"index\":0,\"displayName\":\"RTC_Clock\"}}",
       "without gaps, got [0, 0]"},
      {0, "{\"hostTests\":null}", "needs hostTests"},
      {0, "{\"analysis\":[\"lint\"]}",
       "module ECU.analysis: allowed values ['cppcheck']"},
      {0, "{\"analysis\":false}",
       "module ECU.analysis: allowed values ['cppcheck']"},
      {0, "{\"sharedSources\":[\"engine\"]}",
       "module ECU.sharedSources: allowed values ['vp37', 'test_runner', "
       "'obdDefinitions']"},
      {0, "{\"token\":false}", "module ECU.token: expected"},
      {0, "{\"macro\":false}", "module ECU.macro: expected"},
      {0, "{\"ci\":0}", "module ECU.ci: expected true or false"},
      {0, "{\"serialConfigurator\":false}",
       "module ECU.serialConfigurator: expected keys ['displayName', 'index', "
       "'tests', 'testsDrive']"},
      {3, "{\"sharedSources\":[\"vp37\"]}", "only firmware"},
      {1,
       "{\"serialConfigurator\":{\"index\":1,\"displayName\":"
       "\"RTC\u2013Clock\"}}",
       "ASCII"},
      {1,
       "{\"serialConfigurator\":{\"index\":1,\"displayName\":\"RTC_Clock\","
       "\"testsDrive\":true}}",
       "module Fiesta_clock.serialConfigurator.testsDrive: needs tests"},
      {1,
       "{\"serialConfigurator\":{\"index\":1,\"displayName\":\"RTC_Clock\","
       "\"tests\":true,\"testsDrive\":1}}",
       "module Fiesta_clock.serialConfigurator.testsDrive: expected true or "
       "false"},
      {4,
       "{\"name\":\"dpfmodule\",\"kind\":\"inactive\",\"macro\":\"ECU\","
       "\"serialConfigurator\":{\"index\":2,\"displayName\":\"DPF\"}}",
       "duplicate macro"}};
  for (size_t i = 0; i < COUNTOF(cases); ++i) {
    reset(root);
    change_module(root, cases[i].index, cases[i].patch);
    rejected(root, cases[i].error);
  }
  reset(root);
  char *path = path_to(root, "src/SerialConfigurator");
  if (path)
    CHECK(jb_fs_remove(path, true) == HAL_OK);
  free(path);
  rejected(root, "not a module directory");
  reset(root);
  jb_json_tree_t *doc = parse("{\"schemaVersion\":2,\"modules\":[]}");
  save(root, "modules.json", doc);
  jb_json_tree_destroy(doc);
  rejected(root, "schemaVersion");
}
static void identities(const char *root) {
  static const struct {
    const char *product, *hint, *extra, *error;
  } cases[] = {
      {"Fiesta ECU", "Fiesta_ECU", ",\"usbVid\":\"0x1234\"", "VID/PID"},
      {"Fiesta ECU", "Fiesta_ECU", ",\"usbVid\":\"0x2e8a\"",
       "inherited from HAL"},
      {"Fiesta ECU", "Fiesta_ECU", ",\"usbPid\":\"0x000a\"",
       "inherited from HAL"},
      {"ECU", "Fiesta_ECU", "", "usbProduct"},
      {"Fiesta ECU", "ECU", "", "byIdHint must start"},
      {"Fiesta ECU", "Fiesta_Fiesta_clock_suffix", "", "also matches"},
      {"Fiesta ECU", "Fiesta_Fiesta_clock", "", "duplicate byIdHint"}};
  for (size_t i = 0; i < COUNTOF(cases); ++i) {
    reset(root);
    fm_context c;
    fm_init(&c);
    jb_string_t text = {0};
    fm_string(&text);
    fm_printf(&c, &text,
              "{\"identity\":{\"usbProduct\":\"%s\",\"byIdHint\":\"%s\"%s}}",
              cases[i].product, cases[i].hint, cases[i].extra);
    char *path = path_to(root, "src/ECU/.vscode/jaszczurhal.project.json");
    if (path)
      write_file(path, jb_string_data(&text));
    free(path);
    CHECK(c.status == HAL_OK);
    jb_string_destroy(&text);
    jb_string_destroy(&c.error);
    rejected(root, cases[i].error);
  }
}
static void render_cases(const char *root) {
  reset(root);
  fm_context c;
  fm_init(&c);
  fm_registry r = {0};
  fm_load(&c, root, &r);
  CHECK(c.status == HAL_OK && r.count == 4 && r.sc_count == 2);
  jb_string_t out[3] = {{0}};
  for (size_t i = 0; i < COUNTOF(out); ++i)
    fm_string(&out[i]);
  fm_render(&c, &r, out);
  CHECK(c.status == HAL_OK);
  CHECK(strstr(jb_string_data(&out[0]), "#define SC_MODULE_COUNT 2u") != NULL);
  CHECK(strstr(jb_string_data(&out[0]),
               "#define SC_MODULE_TOKEN_ADJUSTOMETER \"ADJ\"") != NULL);
  CHECK(strstr(jb_string_data(&out[0]), "SC_MODULE_ADJUSTOMETER ") == NULL);
  CHECK(strstr(jb_string_data(&out[1]), "{\"Fiesta_Adjustometer\", NULL}") !=
        NULL);
  CHECK(strstr(jb_string_data(&out[1]),
               "X(SC_MODULE_TOKEN_ECU, SC_MODULE_ECU, \"ECU\", true, false)") !=
        NULL);
  fm_destroy(&r);
  jb_string_destroy(&c.error);
  for (size_t i = 0; i < COUNTOF(out); ++i)
    jb_string_destroy(&out[i]);
}
static void variants(const char *root) {
  static const char inactive[] =
      "{\"name\":\"dpfmodule\",\"kind\":\"inactive\",\"macro\":\"DPF\","
      "\"serialConfigurator\":{\"index\":2,\"displayName\":\"DPF\"}}";
  static const char *const forbidden[] = {
      "{\"token\":\"OIL&SPD\"}", "{\"hostTests\":\"build\"}", "{\"ci\":true}"};
  for (size_t variant = 0; variant < 3; ++variant) {
    reset(root);
    if (variant == 0)
      change_module(root, 4, inactive);
    else if (variant == 1) {
      change_module(
          root, 0,
          "{\"serialConfigurator\":{\"index\":1,\"displayName\":\"ECU\"}}");
      change_module(root, 1,
                    "{\"serialConfigurator\":{\"index\":0,\"displayName\":"
                    "\"RTC_Clock\"}}");
    } else {
      change_module(root, 0,
                    "{\"serialConfigurator\":{\"index\":0,\"displayName\":"
                    "\"ECU\",\"tests\":true,\"testsDrive\":true}}");
      change_module(root, 1,
                    "{\"serialConfigurator\":{\"index\":1,\"displayName\":"
                    "\"RTC_Clock\",\"tests\":true}}");
    }
    fm_context c;
    fm_init(&c);
    fm_registry r = {0};
    fm_load(&c, root, &r);
    jb_string_t out[3] = {{0}};
    for (size_t i = 0; i < COUNTOF(out); ++i)
      fm_string(&out[i]);
    fm_render(&c, &r, out);
    CHECK(c.status == HAL_OK);
    const char *tokens = jb_string_data(&out[0]),
               *table = jb_string_data(&out[1]);
    if (variant == 0) {
      CHECK(r.sc_count == 3 && strcmp(r.sc[2]->name, "dpfmodule") == 0 &&
            !r.sc[2]->firmware && !r.sc[2]->ci);
      CHECK(strstr(tokens, "SC_MODULE_TOKEN_DPF") == NULL);
      CHECK(strstr(table, "X(NULL, SC_MODULE_DPF, NULL, false, false)") !=
            NULL);
    } else if (variant == 1) {
      const char *a = strstr(table, "SC_MODULE_CLOCK,"),
                 *b = strstr(table, "SC_MODULE_ECU,");
      CHECK(a && b && a < b);
      a = strstr(tokens, "SC_MODULE_CLOCK \"RTC_Clock\"");
      b = strstr(tokens, "SC_MODULE_ECU \"ECU\"");
      CHECK(a && b && a < b);
    } else {
      CHECK(strstr(
                table,
                "X(SC_MODULE_TOKEN_ECU, SC_MODULE_ECU, \"ECU\", true, true)") !=
            NULL);
      CHECK(strstr(table, "X(SC_MODULE_TOKEN_CLOCK, SC_MODULE_CLOCK, "
                          "\"Fiesta_clock\", true, false)") != NULL);
    }
    for (size_t i = 0; i < COUNTOF(out); ++i)
      jb_string_destroy(&out[i]);
    fm_destroy(&r);
    jb_string_destroy(&c.error);
  }
  for (size_t i = 0; i < COUNTOF(forbidden); ++i) {
    reset(root);
    change_module(root, 4, inactive);
    change_module(root, 4, forbidden[i]);
    rejected(root, "inactive modules");
  }
}
static void sources(const char *root) {
  reset(root);
  change_module(root, 0, "{\"sharedSources\":[\"vp37\"]}");
  char *listing = path_to(root, "src/common/vp37/vp37_sources.txt");
  char *source = path_to(root, "src/common/vp37/vp37.c");
  char *extra = path_to(root, "src/common/vp37/vp37_extra.c");
  char *manifest = path_to(root, "manifest.json");
  if (!listing || !source || !extra || !manifest) {
    free(listing);
    free(source);
    free(extra);
    free(manifest);
    return;
  }
  write_file(listing, "# comment\nvp37.c\n");
  for (size_t which = 0; which < 3; ++which) {
    if (which == 1)
      write_file(source, "int a;\n");
    if (which == 2)
      write_file(extra, "int b;\n");
    fm_context c;
    fm_init(&c);
    jb_string_t files = {0};
    fm_string(&files);
    fm_shared_files(&c, root, "vp37", &files);
    if (which == 1)
      CHECK(c.status == HAL_OK &&
            strcmp(jb_string_data(&files), "vp37.c\n") == 0);
    else
      CHECK(c.status != HAL_OK && strstr(jb_string_data(&c.error),
                                         which == 0 ? "missing" : "unlisted"));
    jb_string_destroy(&files);
    jb_string_destroy(&c.error);
  }
  CHECK(jb_fs_remove(extra, false) == HAL_OK);
  fm_registry r = {0};
  fm_context c;
  fm_init(&c);
  fm_load(&c, root, &r);
  jb_string_t value = {0}, out = {0};
  fm_string(&value);
  fm_string(&out);
  if (c.status == HAL_OK)
    fm_extra_sources(&c, root, &r.modules[0], &value);
  CHECK(c.status == HAL_OK &&
        strcmp(jb_string_data(&value), "../common/vp37/vp37.c") == 0);
  CHECK(jb_fs_remove(source, false) == HAL_OK);
  write_file(listing, "# empty source set\n");
  jb_string_clear(&value);
  if (c.status == HAL_OK)
    fm_extra_sources(&c, root, &r.modules[0], &value);
  /* The former Python splitlines() preserved CMake's single empty line. */
  CHECK(c.status == HAL_OK &&
        strcmp(jb_string_data(&value), "../common/vp37/") == 0);
  write_file(listing, "vp37.c\n");
  write_file(source, "int a;\n");
  write_file(manifest, "{\"cmake\":{\"cache\":{\"JH_MODULE_NAME\":\"ECU\"}}}");
  if (c.status == HAL_OK)
    fm_apply_sources(&c, manifest, &r.modules[0], root, &out);
  CHECK(c.status == HAL_OK);
  jb_json_tree_t *updated = parse(jb_string_data(&out));
  fm_value cache = fm_get(fm_get(fm_root(updated), "cmake"), "cache");
  jb_json_span_t entry = {0};
  CHECK(fm_count(cache) == 2);
  CHECK(jb_json_tree_text(fm_get(cache, "JH_MODULE_NAME"), &entry) == HAL_OK);
  CHECK(fm_equal(entry, "ECU"));
  CHECK(jb_json_tree_text(fm_get(cache, "JH_EXTRA_SOURCES"), &entry) == HAL_OK);
  CHECK(fm_equal(entry, "../common/vp37/vp37.c"));
  jb_json_tree_destroy(updated);
  write_file(
      manifest,
      "{\"cmake\":{\"cache\":{\"JH_EXTRA_SOURCES\":\"../common/vp37/x.c\"}}}");
  jb_string_clear(&out);
  if (c.status == HAL_OK)
    fm_apply_sources(&c, manifest, &r.modules[1], root, &out);
  CHECK(c.status == HAL_OK &&
        strstr(jb_string_data(&out), "JH_EXTRA_SOURCES") == NULL);
  write_file(manifest, "{}");
  jb_string_clear(&out);
  if (c.status == HAL_OK)
    fm_apply_sources(&c, manifest, &r.modules[1], root, &out);
  CHECK(c.status == HAL_OK && strcmp(jb_string_data(&out), "{}") == 0);
  CHECK(jb_fs_remove(source, false) == HAL_OK);
  jb_string_clear(&out);
  if (c.status == HAL_OK)
    fm_apply_sources(&c, manifest, &r.modules[0], root, &out);
  CHECK(c.status != HAL_OK && strstr(jb_string_data(&c.error), "cmake.cache"));
  jb_string_destroy(&out);
  jb_string_destroy(&value);
  jb_string_destroy(&c.error);
  fm_destroy(&r);
  free(listing);
  free(source);
  free(extra);
  free(manifest);
}
static void cli_args(const char *root, const char *tool,
                     const char *const *args, uint32_t expected) {
  const jb_process_options_t options = {.executable = tool,
                                        .arguments = args,
                                        .working_directory = root,
                                        .timeout_ms = 30000};
  jb_process_result_t result = {0};
  CHECK(jb_process_run(&options, &result) == HAL_OK);
  CHECK(result.exited && result.term_signal == 0 &&
        result.exit_code == expected);
}
static void cli(const char *root, const char *tool, bool check,
                uint32_t expected) {
  const char *args[] = {"--repo-root", root, check ? "--check" : NULL, NULL};
  cli_args(root, tool, args, expected);
}
static void publication(const char *root, const char *tool, const char *repo) {
  reset(root);
  cli(root, tool, true, 1);
  char *path = path_to(root, fm_outputs[0]);
  if (!path)
    return;
  jb_fs_info_t info = {0};
  CHECK(jb_fs_status(path, true, &info) == HAL_ENOENT);
#ifndef _WIN32
  const mode_t mask = umask(0027);
#endif
  cli(root, tool, false, 0);
#ifndef _WIN32
  (void)umask(mask);
  struct stat permissions;
  CHECK(stat(path, &permissions) == 0 && (permissions.st_mode & 0777) == 0640);
  CHECK(chmod(path, 0604) == 0);
#endif
  cli(root, tool, true, 0);
  const char *empty_root[] = {"--repo-root=", "--check", NULL};
  const char *unknown[] = {"--unknown", NULL};
  const char *missing[] = {"--repo-root", "--check", NULL};
  cli_args(root, tool, empty_root, 0);
  cli_args(root, tool, unknown, 2);
  cli_args(root, tool, missing, 2);
  jb_file_data_t bytes = {0};
  CHECK(jb_file_read(path, 1048576, &bytes) == HAL_OK);
  jb_string_t crlf = {0};
  fm_string(&crlf);
  for (size_t i = 0; i < bytes.length; ++i) {
    if (bytes.data[i] == '\n')
      CHECK(jb_string_append(&crlf, "\r", 1) == HAL_OK);
    CHECK(jb_string_append(&crlf, (const char *)bytes.data + i, 1) == HAL_OK);
  }
  write_file(path, jb_string_data(&crlf));
  cli(root, tool, true, 0);
  jb_file_release(&bytes);
  CHECK(jb_file_read(path, 1048576, &bytes) == HAL_OK);
  CHECK(bytes.length == jb_string_size(&crlf) &&
        memcmp(bytes.data, jb_string_data(&crlf), bytes.length) == 0);
  jb_file_release(&bytes);
  jb_string_destroy(&crlf);
  write_file(path, "stale\n");
  cli(root, tool, true, 1);
  cli(root, tool, false, 0);
  cli(root, tool, true, 0);
#ifndef _WIN32
  CHECK(stat(path, &permissions) == 0 && (permissions.st_mode & 0777) == 0604);
#endif
  free(path);
  fm_context c;
  fm_init(&c);
  fm_registry r = {0};
  fm_load(&c, repo, &r);
  jb_string_t out[3] = {{0}};
  for (size_t i = 0; i < COUNTOF(out); ++i)
    fm_string(&out[i]);
  fm_render(&c, &r, out);
  CHECK(c.status == HAL_OK);
  for (size_t i = 0; i < COUNTOF(out); ++i) {
    path = path_to(repo, fm_outputs[i]);
    if (path)
      CHECK(jb_file_read(path, 1048576, &bytes) == HAL_OK);
    /* Git may check out the generated headers with CRLF on Windows. The
     * former read_text() comparison ignored that checkout conversion. */
    size_t length = 0;
    for (size_t j = 0; j < bytes.length; ++j)
      if (!(bytes.data[j] == '\r' && j + 1 < bytes.length &&
            bytes.data[j + 1] == '\n'))
        bytes.data[length++] = bytes.data[j];
    CHECK(length == jb_string_size(&out[i]) &&
          memcmp(bytes.data, jb_string_data(&out[i]), length) == 0);
    jb_file_release(&bytes);
    free(path);
    jb_string_destroy(&out[i]);
  }
  fm_destroy(&r);
  jb_string_destroy(&c.error);
}
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  rules(argv[1]);
  identities(argv[1]);
  render_cases(argv[1]);
  variants(argv[1]);
  sources(argv[1]);
  publication(argv[1], argv[2], argv[3]);
  CHECK(jb_fs_remove(argv[1], true) == HAL_OK);
  printf("%zu checks, %zu failures\n", checks, failures);
  return failures ? 1 : 0;
}

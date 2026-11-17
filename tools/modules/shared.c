#include "modules.h"
#include <jb/process.h>
#include <stdlib.h>
#include <string.h>

static hal_status_t capture(void *user, jb_process_stream_t stream,
                            const unsigned char *data, size_t length) {
  jb_string_t *outputs = user,
              *out = &outputs[stream == JB_PROCESS_STDERR ? 1 : 0];
  if (length > 16 * 1024 * 1024 ||
      jb_string_size(out) > 16 * 1024 * 1024 - length)
    return HAL_EOVERFLOW;
  return jb_string_append(out, (const char *)data, length);
}
void fm_shared_files(fm_context *c, const char *root, const char *set,
                     jb_string_t *out) {
  if (c->status != HAL_OK)
    return;
  size_t size = 0;
  jb_string_t cwd = {0}, directory = {0}, name = {0}, output[2] = {{0}};
  fm_string(&cwd);
  fm_string(&directory);
  fm_string(&name);
  for (size_t i = 0; i < COUNTOF(output); ++i)
    fm_string(&output[i]);
  fm_status(c, jb_fs_realpath(root, NULL, 0, &size));
  if (c->status == HAL_OK)
    fm_status(c, jb_string_resize(&cwd, size - 1, '\0'));
  if (c->status == HAL_OK)
    fm_status(c,
              jb_fs_realpath(root, jb_string_mutable_data(&cwd), size, &size));
  fm_printf(c, &directory, "-DFIESTA_QUERY_ROOT=%s", jb_string_data(&cwd));
  fm_printf(c, &name, "-DFIESTA_QUERY_SET=%s", set);
  const char *args[] = {
      "-DFIESTA_SHARED_QUERY=files", jb_string_data(&directory),
      jb_string_data(&name),         "-P",
      FIESTA_SHARED_READER,          NULL};
  const jb_process_options_t options = {.executable = FIESTA_CMAKE_COMMAND,
                                        .arguments = args,
                                        .working_directory =
                                            jb_string_data(&cwd),
                                        .timeout_ms = 60000,
                                        .output = capture,
                                        .context = output};
  jb_process_result_t result = {0};
  if (c->status == HAL_OK)
    fm_status(c, jb_process_run(&options, &result));
  if (c->status == HAL_OK &&
      (!result.exited || result.exit_code != 0 || result.term_signal != 0))
    fm_error(c, "%s", jb_string_data(&output[1]));
  /* CMake emits platform newlines; the tool's text protocol uses LF. */
  for (size_t i = 0; c->status == HAL_OK && i < jb_string_size(&output[0]);
       ++i) {
    char byte = jb_string_data(&output[0])[i];
    if (byte != '\r')
      fm_status(c, jb_string_append(out, &byte, 1));
  }
  for (size_t i = 0; i < COUNTOF(output); ++i)
    jb_string_destroy(&output[i]);
  jb_string_destroy(&cwd);
  jb_string_destroy(&directory);
  jb_string_destroy(&name);
}
void fm_extra_sources(fm_context *c, const char *root, const fm_module *module,
                      jb_string_t *out) {
  for (size_t i = 0; c->status == HAL_OK && i < fm_count(module->shared); ++i) {
    char *set = fm_text(c, fm_at(module->shared, i), "sharedSources", true);
    jb_string_t files = {0};
    fm_string(&files);
    if (c->status == HAL_OK)
      fm_shared_files(c, root, set, &files);
    const char *begin = jb_string_data(&files),
               *end = begin + jb_string_size(&files);
    while (c->status == HAL_OK && begin < end) {
      const char *line = memchr(begin, '\n', (size_t)(end - begin));
      if (!line)
        line = end;
      /* Keep the former splitlines() result, including the single empty
       * line CMake emits for an empty source set. */
      if (jb_string_size(out))
        fm_append(c, out, ";");
      fm_printf(c, out, "../common/%s/%.*s", set, (int)(line - begin), begin);
      begin = line == end ? end : line + 1;
    }
    free(set);
    jb_string_destroy(&files);
  }
}
void fm_apply_sources(fm_context *c, const char *path, const fm_module *module,
                      const char *root, jb_string_t *out) {
  jb_json_tree_t *doc = NULL, *cache = NULL, *cmake = NULL, *value = NULL;
  fm_read(c, path, &doc);
  const jb_json_tree_limits_t limits = {100000, 128, 16 * 1024 * 1024,
                                        64 * 1024 * 1024};
  fm_value config = fm_get(fm_root(doc), "cmake"),
           original = fm_get(config, "cache");
  bool have_cache = original.tree && fm_type(original) != JB_JSON_NULL;
  if (fm_count(module->shared) && !have_cache)
    fm_error(c,
             "module %s: sharedSources needs a cmake.cache in "
             "jaszczurhal.project.json",
             module->name);
  if (c->status == HAL_OK && have_cache) {
    if (fm_type(original) != JB_JSON_OBJECT)
      fm_error(c, "cmake.cache: expected an object");
    if (c->status == HAL_OK)
      fm_status(c, jb_json_tree_copy(original, &limits, &cache));
    jb_json_span_t key = {"JH_EXTRA_SOURCES", 16};
    if (c->status == HAL_OK && fm_count(module->shared)) {
      jb_string_t quoted = {0}, sources = {0};
      fm_string(&quoted);
      fm_string(&sources);
      fm_extra_sources(c, root, module, &sources);
      fm_json(c, &quoted, jb_string_data(&sources));
      const jb_json_limits_t parse = {
          16 * 1024 * 1024, 1, 0, 16 * 1024 * 1024, 0, 0, 0};
      if (c->status == HAL_OK)
        fm_status(c,
                  jb_json_tree_parse((jb_json_span_t){jb_string_data(&quoted),
                                                      jb_string_size(&quoted)},
                                     &parse, &limits, 0, &value, NULL));
      if (c->status == HAL_OK)
        fm_status(c, jb_json_tree_set(cache, key, fm_root(value)));
      jb_string_destroy(&quoted);
      jb_string_destroy(&sources);
    } else if (c->status == HAL_OK &&
               fm_get(original, "JH_EXTRA_SOURCES").tree) {
      fm_status(c, jb_json_tree_erase(cache, key));
    }
    if (c->status == HAL_OK)
      fm_status(c, jb_json_tree_copy(config, &limits, &cmake));
    if (c->status == HAL_OK)
      fm_status(c, jb_json_tree_set(cmake, (jb_json_span_t){"cache", 5},
                                    fm_root(cache)));
    if (c->status == HAL_OK)
      fm_status(c, jb_json_tree_set(doc, (jb_json_span_t){"cmake", 5},
                                    fm_root(cmake)));
  }
  if (c->status == HAL_OK)
    fm_format(c, out, fm_root(doc));
  jb_json_tree_destroy(value);
  jb_json_tree_destroy(cache);
  jb_json_tree_destroy(cmake);
  jb_json_tree_destroy(doc);
}

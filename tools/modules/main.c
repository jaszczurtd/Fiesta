#include "modules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static hal_status_t publish(const char *path, const jb_string_t *text,
                            bool *changed) {
#ifndef _WIN32
  jb_fs_info_t info;
  if (jb_fs_status(path, false, &info) == HAL_ENOENT) {
    /* This CLI is single-threaded; preserve the caller's creation policy. */
    const mode_t mask = umask(0);
    (void)umask(mask);
    return jb_file_write_atomic_mode(path, jb_string_data(text),
                                     jb_string_size(text), false,
                                     (uint32_t)(0666 & ~mask), changed);
  }
#endif
  return jb_file_write_atomic(path, jb_string_data(text), jb_string_size(text),
                              false, changed);
}

static bool current(fm_context *c, const char *path, const jb_string_t *text) {
  jb_file_data_t data = {0};
  hal_status_t s = jb_file_read(path, 16 * 1024 * 1024, &data);
  /* The former Path.read_text() used universal newlines, including CR-only
   * files. Preserve a current checkout's line endings until content changes. */
  bool equal = s == HAL_OK;
  size_t at = 0;
  for (size_t i = 0; equal && i < data.length; ++i) {
    unsigned char byte = data.data[i];
    if (byte == '\r') {
      byte = '\n';
      if (i + 1 < data.length && data.data[i + 1] == '\n')
        ++i;
    }
    equal = at < jb_string_size(text) &&
            byte == (unsigned char)jb_string_data(text)[at];
    ++at;
  }
  equal &= at == jb_string_size(text);
  if (s != HAL_OK && s != HAL_ENOENT)
    fm_error(c, "cannot read %s (status %d)", path, (int)s);
  jb_file_release(&data);
  return equal;
}
static const char *option_value(int argc, char **argv, int *index,
                                const char *name) {
  const char *arg = argv[*index];
  size_t length = strlen(name);
  if (strncmp(arg, name, length) != 0)
    return NULL;
  if (arg[length] == '=')
    return arg + length + 1;
  if (arg[length] != '\0' || *index + 1 == argc ||
      strncmp(argv[*index + 1], "--", 2) == 0)
    return NULL;
  return argv[++*index];
}
static int run(int argc, char **argv) {
  bool check = false, json = false;
  const char *root = ".";
  const char *shared = NULL, *extra = NULL, *manifest = NULL;
  for (int i = 1; i < argc; ++i) {
    const char *value = option_value(argc, argv, &i, "--repo-root");
    if (value)
      root = *value ? value : ".";
    else if (strcmp(argv[i], "--check") == 0)
      check = true;
    else if (strcmp(argv[i], "--json") == 0)
      json = true;
    else if ((value = option_value(argc, argv, &i, "--shared-files")) != NULL)
      shared = value;
    else if ((value = option_value(argc, argv, &i, "--extra-sources")) != NULL)
      extra = value;
    else if ((value = option_value(argc, argv, &i, "--manifest")) != NULL)
      manifest = value;
    else if (strcmp(argv[i], "--help") == 0) {
      puts("fiesta-modules [--repo-root PATH] [--check | --json | "
           "--shared-files SET | --extra-sources MODULE [--manifest FILE]]");
      return 0;
    } else {
      fprintf(stderr, "error: unknown or incomplete argument: %s\n", argv[i]);
      return 2;
    }
  }
  if ((manifest && !extra) || (shared && (extra || json || check)) ||
      (extra && (json || check)) || (json && check)) {
    fputs("error: incompatible command options\n", stderr);
    return 2;
  }
  fm_context c;
  fm_init(&c);
  fm_registry registry = {0};
  if (!shared && c.status == HAL_OK)
    fm_load(&c, root, &registry);
  jb_string_t outputs[3] = {{0}};
  for (size_t i = 0; i < COUNTOF(outputs); ++i)
    fm_string(&outputs[i]);
  if (!shared && !extra)
    fm_render(&c, &registry, outputs);
  bool stale = false;
  if ((shared || extra) && c.status == HAL_OK) {
    jb_string_t text = {0};
    fm_string(&text);
    if (shared)
      fm_shared_files(&c, root, shared, &text);
    else {
      const fm_module *m = NULL;
      for (size_t i = 0; i < registry.count; ++i)
        if (strcmp(registry.modules[i].name, extra) == 0)
          m = &registry.modules[i];
      if (!m)
        fm_error(&c, "unknown module: %s", extra);
      if (c.status == HAL_OK && manifest)
        fm_apply_sources(&c, manifest, m, root, &text);
      else if (c.status == HAL_OK)
        fm_extra_sources(&c, root, m, &text);
    }
    if (c.status == HAL_OK &&
        fwrite(jb_string_data(&text), 1, jb_string_size(&text), stdout) !=
            jb_string_size(&text))
      fm_status(&c, HAL_EIO);
    jb_string_destroy(&text);
  } else if (json && c.status == HAL_OK) {
    jb_string_t report = {0};
    fm_string(&report);
    fm_report(&c, &registry, outputs, &report);
    if (c.status == HAL_OK &&
        fwrite(jb_string_data(&report), 1, jb_string_size(&report), stdout) !=
            jb_string_size(&report))
      fm_status(&c, HAL_EIO);
    jb_string_destroy(&report);
  } else
    for (size_t i = 0; c.status == HAL_OK && i < COUNTOF(outputs); ++i) {
      jb_string_t path = {0};
      fm_string(&path);
      fm_printf(&c, &path, "%s/%s", root, fm_outputs[i]);
      bool same =
          c.status == HAL_OK && current(&c, jb_string_data(&path), &outputs[i]);
      if (c.status == HAL_OK && !same) {
        stale = true;
        if (check)
          fprintf(stderr,
                  "error: %s is out of date; run scripts/fiesta_modules.py\n",
                  fm_outputs[i]);
        else {
          bool changed = false;
          hal_status_t s =
              publish(jb_string_data(&path), &outputs[i], &changed);
          if (s != HAL_OK)
            fm_error(&c, "cannot write %s (status %d)", jb_string_data(&path),
                     (int)s);
          else if (changed)
            printf("generated %s\n", fm_outputs[i]);
        }
      }
      jb_string_destroy(&path);
    }
  if (fflush(stdout) != 0)
    fm_status(&c, HAL_EIO);
  if (c.status != HAL_OK)
    fprintf(stderr, "error: %s\n",
            jb_string_size(&c.error) ? jb_string_data(&c.error)
                                     : hal_status_to_string(c.status));
  int result = c.status != HAL_OK || (check && stale) ? 1 : 0;
  for (size_t i = 0; i < COUNTOF(outputs); ++i)
    jb_string_destroy(&outputs[i]);
  fm_destroy(&registry);
  jb_string_destroy(&c.error);
  return result;
}
#ifdef _WIN32
int wmain(int argc, wchar_t **wide) {
  char **args = calloc((size_t)argc + 1, sizeof(*args));
  if (!args)
    return 1;
  int result = 1, converted = 0;
  for (int i = 0; i < argc; ++i) {
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1,
                                     NULL, 0, NULL, NULL);
    if (length <= 0 || (args[i] = malloc((size_t)length)) == NULL)
      break;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, args[i],
                            length, NULL, NULL) != length)
      break;
    ++converted;
  }
  if (converted == argc && _setmode(_fileno(stdout), _O_BINARY) != -1)
    result = run(argc, args);
  else
    fputs("error: cannot initialize UTF-8 command arguments or output\n",
          stderr);
  for (int i = 0; i < argc; ++i)
    free(args[i]);
  free(args);
  return result;
}
#else
int main(int argc, char **argv) { return run(argc, argv); }
#endif

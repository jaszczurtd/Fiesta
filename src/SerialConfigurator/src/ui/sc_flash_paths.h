#ifndef SC_FLASH_PATHS_H
#define SC_FLASH_PATHS_H

/*
 * Persistence layer for the Flash tab's per-module file pickers.
 *
 * Saves a small JSON document at:
 *   Linux:   $XDG_CONFIG_HOME/fiesta-configurator/flash-paths.json
 *            (or ~/.config/fiesta-configurator/flash-paths.json)
 *   Windows: stub - sc_flash_paths_save / _load return false until a
 *            future Windows-packaging slice ports the path resolver
 *            (see provider §4.1 design rule).
 *
 * On-disk schema:
 *   {
 *     "ECU":         { "uf2_path": "...", "manifest_path": "..." },
 *     "Clocks":      { ... },
 *     "OilAndSpeed": { ... }
 *   }
 *
 * Adjustometer never appears here - out-of-scope by policy lock v1.32.
 *
 * In-memory layout is indexed by the same SC_MODULE_COUNT enum that
 * sc_core uses, so callers can do
 *     sc_flash_paths_get_uf2(&paths, status->display_name)
 * without owning a module-name -> index mapping.
 *
 * This module is GTK-free so future tests or CLI consumers can reuse
 * the loader without dragging in the GUI dependency tree.
 */

#include <stdbool.h>
#include <stddef.h>

#include "sc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum path length stored per slot, including NUL. */
#define SC_FLASH_PATHS_PATH_MAX 1024u

typedef struct ScFlashPathsEntry {
    char uf2[SC_FLASH_PATHS_PATH_MAX];
    char manifest[SC_FLASH_PATHS_PATH_MAX];
} ScFlashPathsEntry;

typedef struct ScFlashPaths {
    ScFlashPathsEntry entries[SC_MODULE_COUNT];
} ScFlashPaths;

/** @brief Zero-initialise a paths struct (all slots empty). */
void sc_flash_paths_init(ScFlashPaths *out);

/**
 * @brief Resolve the canonical config file location for the running
 *        platform. Returns a static string for the lifetime of the
 *        process.
 *
 * Linux: `$XDG_CONFIG_HOME/fiesta-configurator/flash-paths.json` if
 * the env var is set, otherwise `$HOME/.config/...`. Falls back to
 * `/tmp/...` only if HOME is unset (extremely unusual; keeps the GUI
 * usable even in a stripped-down container).
 *
 * Windows: returns NULL until the Windows packaging slice ports it.
 */
const char *sc_flash_paths_default_file(void);

/**
 * @brief Override the config-file path for tests. Pass NULL to reset
 *        to @ref sc_flash_paths_default_file. The override is process-
 *        local, not persisted anywhere.
 */
void sc_flash_paths_set_test_override(const char *path_or_null);

/**
 * @brief Load paths from @ref sc_flash_paths_default_file (or the
 *        test override). Missing file -> zero out @p out and return
 *        true (treated as "no remembered paths yet"). Malformed JSON
 *        -> zero out @p out and return false; the caller can decide
 *        whether to overwrite the bad file or surface a warning.
 */
bool sc_flash_paths_load(ScFlashPaths *out);

/**
 * @brief Save @p in to the default location. Creates the parent
 *        directory if needed. Returns false on IO failure or on
 *        Windows (see header note).
 */
bool sc_flash_paths_save(const ScFlashPaths *in);

/* Per-slot accessors keyed by module display name. Unknown name
 * (not one of ECU/Clocks/OilAndSpeed) is a no-op for setters and
 * returns "" for getters. */
const char *sc_flash_paths_get_uf2(const ScFlashPaths *p,
                                   const char *module_display_name);
const char *sc_flash_paths_get_manifest(const ScFlashPaths *p,
                                        const char *module_display_name);
void sc_flash_paths_set_uf2(ScFlashPaths *p,
                            const char *module_display_name,
                            const char *path);
void sc_flash_paths_set_manifest(ScFlashPaths *p,
                                 const char *module_display_name,
                                 const char *path);

#ifdef __cplusplus
}
#endif

#endif /* SC_FLASH_PATHS_H */

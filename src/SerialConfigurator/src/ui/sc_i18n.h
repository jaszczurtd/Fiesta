#ifndef SC_I18N_H
#define SC_I18N_H

/*
 * Compile-time string-table localization for the SerialConfigurator GUI.
 *
 * Strategy (see provider §4.6 for the full rationale):
 *   - All user-facing strings live in a single static table indexed
 *     by (locale, key). No runtime file IO, no .po/.mo/.json sidecars
 *     to package. Same binary works on Linux and Windows without
 *     bundling libintl or solving cross-platform path resolution.
 *   - The active locale is auto-detected on first call:
 *       1. SC_LOCALE env var (override for tests / explicit user choice),
 *       2. LANG / LC_ALL / LC_MESSAGES env vars (first two chars match
 *          against the supported-locale code list),
 *       3. fall back to English.
 *     Tests / runtime can override via @ref sc_i18n_set_locale.
 *   - Adding a language = adding a column to the table + a const
 *     locale code. Keys are an exhaustive enum, so a missing entry
 *     returns the @ref sc_i18n_missing_marker so the gap is visible
 *     in the UI rather than crashing.
 *
 * Why not gettext / ICU / runtime JSON?
 *   - gettext on Windows requires libintl as a runtime dependency
 *     plus a PO/MO build pipeline. Compiled-in tables side-step that.
 *   - ICU is overkill for a tool with ~50 strings.
 *   - Runtime JSON works but adds path-resolution edge cases that
 *     differ Linux vs Windows. The current backend can be swapped
 *     to JSON later WITHOUT changing callers — the public API only
 *     takes symbolic enum keys.
 *
 * Out of scope (not part of this layer):
 *   - protocol tokens (`SC_GET_META`, `SC_OK`, ...) stay verbatim
 *     because they are wire identifiers, not user-facing text;
 *   - CSS strings in sc_generic_gfx_helper — code, not text;
 *   - the GApplication ID `pl.jaszczur.fiesta.serialconfigurator`.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ScLocale {
    SC_LOCALE_EN = 0,
    SC_LOCALE_PL,
    SC_LOCALE_COUNT
} ScLocale;

typedef enum ScI18nKey {
    /* ── Window / tab structure ────────────────────────────────────── */
    SC_I18N_APP_TITLE,
    SC_I18N_TAB_MODULES,
    SC_I18N_TAB_FLASH,
    SC_I18N_FRAME_MODULES,

    /* ── Detect button (three states) ──────────────────────────────── */
    SC_I18N_BTN_DETECT,
    SC_I18N_BTN_DISCONNECT,
    SC_I18N_BTN_DETECTING,

    /* ── Log view messages ─────────────────────────────────────────── */
    SC_I18N_LOG_IDLE,
    SC_I18N_LOG_DETECTING,
    SC_I18N_LOG_DISCONNECTED,
    SC_I18N_LOG_DETECTION_FAILED,
    SC_I18N_LOG_AUTO_REFRESH_HEADER,    /* "[INFO] Starting automatic..." */
    SC_I18N_LOG_AUTO_REFRESH_FOR_FMT,   /* "[INFO] Automatic refresh for %s..." */
    SC_I18N_LOG_NO_TARGETS,             /* "[INFO] No detected module..." */
    SC_I18N_LOG_WARN_PREFIX,            /* "[WARN] " */

    /* ── Module-details placeholders ───────────────────────────────── */
    SC_I18N_PLACEHOLDER_INITIAL,
    SC_I18N_PLACEHOLDER_DETECTING,
    SC_I18N_PLACEHOLDER_FINISHED,
    SC_I18N_PLACEHOLDER_FAILED,
    SC_I18N_PLACEHOLDER_SELECT_MODULE,
    SC_I18N_PLACEHOLDER_UNKNOWN_ROW,

    /* ── Module-row label suffixes ─────────────────────────────────── */
    SC_I18N_LABEL_DETECTED_SUFFIX,      /* " (detected)" */
    SC_I18N_LABEL_AMBIGUOUS_FMT,        /* " (x%zu, ambiguous)" */

    /* ── Flash tab ─────────────────────────────────────────────────── */
    SC_I18N_FLASH_PLACEHOLDER,
    /* Phase 6.2 — per-module section widgets */
    SC_I18N_FLASH_SECTION_HEADER_FMT,    /* "Flash — %s" */
    SC_I18N_FLASH_LBL_UF2,               /* "UF2 artifact:" */
    SC_I18N_FLASH_LBL_MANIFEST,          /* "Manifest (optional):" */
    SC_I18N_FLASH_BTN_PICK_UF2,          /* "Choose UF2…" */
    SC_I18N_FLASH_BTN_PICK_MANIFEST,     /* "Choose manifest…" */
    SC_I18N_FLASH_BTN_CLEAR_UF2,         /* "Clear" (UF2 picker reset) */
    SC_I18N_FLASH_BTN_CLEAR_MANIFEST,    /* "Clear" (manifest picker reset) */
    SC_I18N_FLASH_BTN_FLASH,             /* "Flash" */
    SC_I18N_FLASH_NO_PATH,               /* "(none)" */
    SC_I18N_FLASH_DIALOG_UF2_TITLE,      /* "Select UF2 artifact" */
    SC_I18N_FLASH_DIALOG_MANIFEST_TITLE, /* "Select manifest JSON" */
    SC_I18N_FLASH_FILTER_UF2,            /* "UF2 firmware images" */
    SC_I18N_FLASH_FILTER_MANIFEST,       /* "Manifest JSON" */
    SC_I18N_FLASH_STATUS_INITIAL,        /* "Pick a UF2 artifact to start." */
    SC_I18N_FLASH_STATUS_UF2_OK_FMT,     /* "UF2 format OK: %s" */
    SC_I18N_FLASH_STATUS_UF2_FAIL_FMT,   /* "UF2 format check failed (%s): %s" */
    SC_I18N_FLASH_STATUS_MANIFEST_OK,    /* "Manifest parsed; sha256 matches the chosen UF2." */
    SC_I18N_FLASH_STATUS_MANIFEST_PARSE_FAIL_FMT, /* "Manifest parse failed: %s" */
    SC_I18N_FLASH_STATUS_MANIFEST_VERIFY_FAIL_FMT, /* "Manifest verification failed: %s" */
    SC_I18N_FLASH_STATUS_MANIFEST_NEEDS_UF2, /* "Manifest parsed; pick a UF2 to verify the hash." */
    SC_I18N_FLASH_STATUS_FLASH_TODO,     /* "Flash flow not implemented yet (Phase 6.5)." */
    SC_I18N_FLASH_STATUS_LOCK_RELEASED,  /* "Lock released. (Stub run finished.)" */

    /* ── Default per-module status strings (idle) ──────────────────── */
    SC_I18N_STATUS_NO_META,
    SC_I18N_STATUS_NO_CATALOG,
    SC_I18N_STATUS_NO_VALUES,
    SC_I18N_STATUS_NO_PROBE,
    SC_I18N_STATUS_WAITING,

    /* ── Worker-produced status strings ────────────────────────────── */
    SC_I18N_STATUS_MODULE_UNAVAILABLE,
    SC_I18N_STATUS_NOT_DETECTED,
    SC_I18N_STATUS_AMBIGUOUS_META,
    SC_I18N_STATUS_AMBIGUOUS_CATALOG,
    SC_I18N_STATUS_AMBIGUOUS_VALUES,
    SC_I18N_STATUS_AMBIGUOUS_PROBE,
    SC_I18N_STATUS_META_TRANSPORT_ERR,
    SC_I18N_STATUS_PROTOCOL_UNSUPPORTED,
    SC_I18N_STATUS_PROTOCOL_UNSUPPORTED_SHORT,
    SC_I18N_STATUS_META_FAILED_FMT,         /* "SC_GET_META failed: %s" */
    SC_I18N_STATUS_SKIP_META_TRANSPORT,
    SC_I18N_STATUS_SKIP_META_FAILED,
    SC_I18N_STATUS_META_REFRESHED,
    SC_I18N_STATUS_CATALOG_TRANSPORT_ERR,
    SC_I18N_STATUS_SKIP_CATALOG_TRANSPORT,
    SC_I18N_STATUS_CATALOG_PARSE_FAILED,
    SC_I18N_STATUS_SKIP_CATALOG_PARSE,
    SC_I18N_STATUS_CATALOG_READ_FMT,        /* "Catalog read: %zu id(s)%s" */
    SC_I18N_STATUS_TRUNCATED_SUFFIX,        /* " (truncated)" */
    SC_I18N_STATUS_VALUES_TRANSPORT_ERR,
    SC_I18N_STATUS_SKIP_VALUES_TRANSPORT,
    SC_I18N_STATUS_VALUES_PARSE_FAILED,
    SC_I18N_STATUS_SKIP_VALUES_PARSE,
    SC_I18N_STATUS_VALUES_READ_FMT,         /* "Values read: %zu entry(ies)%s" */
    SC_I18N_STATUS_NO_IDS_TO_PROBE,
    SC_I18N_STATUS_PROBE_PARTIAL_FMT,       /* "Probe partial: ok=%zu fail=%zu mismatch=%zu" */
    SC_I18N_STATUS_PROBE_OK_FMT,            /* "Probe ok: %zu id(s), mismatch=%zu" */
    SC_I18N_STATUS_PROBE_SKIPPED,

    SC_I18N_KEY_COUNT
} ScI18nKey;

/**
 * @brief Look up the localized text for @p key in the active locale.
 *
 * Returns a non-NULL static string. If the table happens to have a
 * NULL slot (which would be a build bug), falls back to the marker
 * returned by @ref sc_i18n_missing_marker so the gap is visible in
 * the UI rather than crashing.
 *
 * Stable for the program lifetime — the string is a static literal
 * baked into the binary.
 */
const char *sc_i18n_string_get(ScI18nKey key);

/** @brief Active locale for subsequent @ref sc_i18n_string_get calls. */
ScLocale sc_i18n_active_locale(void);

/** @brief Override the active locale (typically tests or explicit UI). */
void sc_i18n_set_locale(ScLocale locale);

/** @brief Two-letter ISO-639-1 code for a locale, e.g. "en" / "pl". */
const char *sc_i18n_locale_code(ScLocale locale);

/**
 * @brief Parse the first two letters of an `xx_YY.ENC`-style locale
 *        string into a supported @ref ScLocale. Returns
 *        @c SC_LOCALE_EN for unknown/NULL/empty input.
 */
ScLocale sc_i18n_parse_locale_string(const char *s);

/**
 * @brief Marker used when a key has no table entry. Public so tests
 *        can assert against it.
 */
const char *sc_i18n_missing_marker(void);

#ifdef __cplusplus
}
#endif

#endif /* SC_I18N_H */

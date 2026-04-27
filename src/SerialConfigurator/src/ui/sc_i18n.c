#include "sc_i18n.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ── Locale code table ───────────────────────────────────────────────── */

static const char *const k_locale_codes[SC_LOCALE_COUNT] = {
    [SC_LOCALE_EN] = "en",
    [SC_LOCALE_PL] = "pl",
};

/* ── String tables (designated initializers — missing slots are NULL) ── */

static const char *const k_strings_en[SC_I18N_KEY_COUNT] = {
    [SC_I18N_APP_TITLE]            = "Serial Configurator",
    [SC_I18N_TAB_MODULES]          = "Modules",
    [SC_I18N_TAB_FLASH]            = "Flash",
    [SC_I18N_FRAME_MODULES]        = "Modules",

    [SC_I18N_BTN_DETECT]           = "Detect Fiesta Modules",
    [SC_I18N_BTN_DISCONNECT]       = "Disconnect",
    [SC_I18N_BTN_DETECTING]        = "Detecting...",

    [SC_I18N_LOG_IDLE] =
        "Press \"Detect Fiesta Modules\" to send HELLO to connected modules.\n",
    [SC_I18N_LOG_DETECTING] =
        "Detection started... please wait.\n",
    [SC_I18N_LOG_DISCONNECTED] =
        "Disconnected. Application state has been reset.\n"
        "Press \"Detect Fiesta Modules\" to start detection again.\n",
    [SC_I18N_LOG_DETECTION_FAILED] =
        "Detection failed: internal task error.\n",
    [SC_I18N_LOG_AUTO_REFRESH_HEADER] =
        "\n[INFO] Starting automatic metadata refresh for detected modules...\n",
    [SC_I18N_LOG_AUTO_REFRESH_FOR_FMT] =
        "\n[INFO] Automatic metadata refresh for %s...\n",
    [SC_I18N_LOG_NO_TARGETS] =
        "[INFO] No detected module eligible for metadata refresh.\n",
    [SC_I18N_LOG_WARN_PREFIX]      = "[WARN] ",

    [SC_I18N_PLACEHOLDER_INITIAL]      = "Run detection to populate module details.",
    [SC_I18N_PLACEHOLDER_DETECTING]    = "Detecting modules...",
    [SC_I18N_PLACEHOLDER_FINISHED]     = "Detection + metadata refresh finished.",
    [SC_I18N_PLACEHOLDER_FAILED]       = "Detection/metadata workflow failed.",
    [SC_I18N_PLACEHOLDER_SELECT_MODULE] = "Select a module to view details.",
    [SC_I18N_PLACEHOLDER_UNKNOWN_ROW]   = "Unknown module row selected.",

    [SC_I18N_LABEL_DETECTED_SUFFIX] = " (detected)",
    [SC_I18N_LABEL_AMBIGUOUS_FMT]   = " (x%zu, ambiguous)",

    [SC_I18N_FLASH_PLACEHOLDER] =
        "Detect Fiesta modules first. The flash sections appear here "
        "once at least one module responds to HELLO.",

    [SC_I18N_STATUS_NO_META]    = "No metadata (module not detected).",
    [SC_I18N_STATUS_NO_CATALOG] = "No catalog read (module not detected).",
    [SC_I18N_STATUS_NO_VALUES]  = "No values read (module not detected).",
    [SC_I18N_STATUS_NO_PROBE]   = "No param probe (module not detected).",
    [SC_I18N_STATUS_WAITING]    = "Waiting for detection...",

    [SC_I18N_STATUS_MODULE_UNAVAILABLE] = "Module status unavailable.",
    [SC_I18N_STATUS_NOT_DETECTED]       = "Module not detected.",
    [SC_I18N_STATUS_AMBIGUOUS_META]     = "Ambiguous target: metadata skipped.",
    [SC_I18N_STATUS_AMBIGUOUS_CATALOG]  = "Ambiguous target: catalog skipped.",
    [SC_I18N_STATUS_AMBIGUOUS_VALUES]   = "Ambiguous target: values skipped.",
    [SC_I18N_STATUS_AMBIGUOUS_PROBE]    = "Ambiguous target: param probes skipped.",
    [SC_I18N_STATUS_META_TRANSPORT_ERR] = "SC_GET_META transport error.",
    [SC_I18N_STATUS_PROTOCOL_UNSUPPORTED] =
        "SC protocol not supported by this firmware yet.",
    [SC_I18N_STATUS_PROTOCOL_UNSUPPORTED_SHORT] = "SC protocol not supported.",
    [SC_I18N_STATUS_META_FAILED_FMT]    = "SC_GET_META failed: %s",
    [SC_I18N_STATUS_SKIP_META_TRANSPORT] = "Skipped: metadata transport failed.",
    [SC_I18N_STATUS_SKIP_META_FAILED]    = "Skipped: metadata failed.",
    [SC_I18N_STATUS_META_REFRESHED]      = "Metadata refreshed.",
    [SC_I18N_STATUS_CATALOG_TRANSPORT_ERR] = "SC_GET_PARAM_LIST transport error.",
    [SC_I18N_STATUS_SKIP_CATALOG_TRANSPORT] = "Skipped: catalog transport failed.",
    [SC_I18N_STATUS_CATALOG_PARSE_FAILED]   = "Catalog parse failed.",
    [SC_I18N_STATUS_SKIP_CATALOG_PARSE]     = "Skipped: catalog parse failed.",
    [SC_I18N_STATUS_CATALOG_READ_FMT]       = "Catalog read: %zu id(s)%s",
    [SC_I18N_STATUS_TRUNCATED_SUFFIX]       = " (truncated)",
    [SC_I18N_STATUS_VALUES_TRANSPORT_ERR]   = "SC_GET_VALUES transport error.",
    [SC_I18N_STATUS_SKIP_VALUES_TRANSPORT]  = "Skipped: values transport failed.",
    [SC_I18N_STATUS_VALUES_PARSE_FAILED]    = "Values parse failed.",
    [SC_I18N_STATUS_SKIP_VALUES_PARSE]      = "Skipped: values parse failed.",
    [SC_I18N_STATUS_VALUES_READ_FMT]        = "Values read: %zu entry(ies)%s",
    [SC_I18N_STATUS_NO_IDS_TO_PROBE]        = "No parameter ids to probe.",
    [SC_I18N_STATUS_PROBE_PARTIAL_FMT] =
        "Probe partial: ok=%zu fail=%zu mismatch=%zu",
    [SC_I18N_STATUS_PROBE_OK_FMT]           = "Probe ok: %zu id(s), mismatch=%zu",
    [SC_I18N_STATUS_PROBE_SKIPPED]          = "Probe skipped (fast auto-refresh).",
};

/*
 * Polish translations. Protocol tokens (SC_GET_*, HELLO) are kept verbatim
 * because they are wire identifiers, not natural-language text. Format
 * specifiers stay in the same positions so the C-side caller does not need
 * to know which locale is active.
 */
static const char *const k_strings_pl[SC_I18N_KEY_COUNT] = {
    [SC_I18N_APP_TITLE]            = "Serial Configurator",
    [SC_I18N_TAB_MODULES]          = "Moduły",
    [SC_I18N_TAB_FLASH]            = "Flashowanie",
    [SC_I18N_FRAME_MODULES]        = "Moduły",

    [SC_I18N_BTN_DETECT]           = "Wykryj moduły Fiesta",
    [SC_I18N_BTN_DISCONNECT]       = "Rozłącz",
    [SC_I18N_BTN_DETECTING]        = "Wykrywanie...",

    [SC_I18N_LOG_IDLE] =
        "Naciśnij \"Wykryj moduły Fiesta\", aby wysłać HELLO do podłączonych modułów.\n",
    [SC_I18N_LOG_DETECTING] =
        "Wykrywanie rozpoczęte... proszę czekać.\n",
    [SC_I18N_LOG_DISCONNECTED] =
        "Rozłączono. Stan aplikacji został zresetowany.\n"
        "Naciśnij \"Wykryj moduły Fiesta\", aby ponownie uruchomić wykrywanie.\n",
    [SC_I18N_LOG_DETECTION_FAILED] =
        "Wykrywanie nie powiodło się: wewnętrzny błąd zadania.\n",
    [SC_I18N_LOG_AUTO_REFRESH_HEADER] =
        "\n[INFO] Rozpoczynam automatyczne odświeżanie metadanych dla wykrytych modułów...\n",
    [SC_I18N_LOG_AUTO_REFRESH_FOR_FMT] =
        "\n[INFO] Automatyczne odświeżenie metadanych dla %s...\n",
    [SC_I18N_LOG_NO_TARGETS] =
        "[INFO] Brak wykrytych modułów kwalifikujących się do odświeżenia metadanych.\n",
    [SC_I18N_LOG_WARN_PREFIX]      = "[WARN] ",

    [SC_I18N_PLACEHOLDER_INITIAL]      = "Uruchom wykrywanie, aby wypełnić szczegóły modułu.",
    [SC_I18N_PLACEHOLDER_DETECTING]    = "Wykrywanie modułów...",
    [SC_I18N_PLACEHOLDER_FINISHED]     = "Wykrywanie i odświeżanie metadanych zakończone.",
    [SC_I18N_PLACEHOLDER_FAILED]       = "Wykrywanie/odświeżanie metadanych nie powiodło się.",
    [SC_I18N_PLACEHOLDER_SELECT_MODULE] = "Wybierz moduł, aby zobaczyć szczegóły.",
    [SC_I18N_PLACEHOLDER_UNKNOWN_ROW]   = "Wybrano nieznany wiersz modułu.",

    [SC_I18N_LABEL_DETECTED_SUFFIX] = " (wykryty)",
    [SC_I18N_LABEL_AMBIGUOUS_FMT]   = " (x%zu, niejednoznaczny)",

    [SC_I18N_FLASH_PLACEHOLDER] =
        "Najpierw wykryj moduły Fiesta. Sekcje flashowania pojawią się tutaj, "
        "gdy przynajmniej jeden moduł odpowie na HELLO.",

    [SC_I18N_STATUS_NO_META]    = "Brak metadanych (moduł niewykryty).",
    [SC_I18N_STATUS_NO_CATALOG] = "Brak odczytu katalogu (moduł niewykryty).",
    [SC_I18N_STATUS_NO_VALUES]  = "Brak odczytu wartości (moduł niewykryty).",
    [SC_I18N_STATUS_NO_PROBE]   = "Brak sondy parametrów (moduł niewykryty).",
    [SC_I18N_STATUS_WAITING]    = "Oczekiwanie na wykrywanie...",

    [SC_I18N_STATUS_MODULE_UNAVAILABLE] = "Status modułu niedostępny.",
    [SC_I18N_STATUS_NOT_DETECTED]       = "Moduł niewykryty.",
    [SC_I18N_STATUS_AMBIGUOUS_META]     = "Niejednoznaczny cel: metadane pominięte.",
    [SC_I18N_STATUS_AMBIGUOUS_CATALOG]  = "Niejednoznaczny cel: katalog pominięty.",
    [SC_I18N_STATUS_AMBIGUOUS_VALUES]   = "Niejednoznaczny cel: wartości pominięte.",
    [SC_I18N_STATUS_AMBIGUOUS_PROBE]    = "Niejednoznaczny cel: sondy parametrów pominięte.",
    [SC_I18N_STATUS_META_TRANSPORT_ERR] = "Błąd transportu SC_GET_META.",
    [SC_I18N_STATUS_PROTOCOL_UNSUPPORTED] =
        "Protokół SC nie jest jeszcze obsługiwany przez to firmware.",
    [SC_I18N_STATUS_PROTOCOL_UNSUPPORTED_SHORT] = "Protokół SC nieobsługiwany.",
    [SC_I18N_STATUS_META_FAILED_FMT]    = "SC_GET_META nieudane: %s",
    [SC_I18N_STATUS_SKIP_META_TRANSPORT] = "Pominięto: błąd transportu metadanych.",
    [SC_I18N_STATUS_SKIP_META_FAILED]    = "Pominięto: metadane nieudane.",
    [SC_I18N_STATUS_META_REFRESHED]      = "Metadane odświeżone.",
    [SC_I18N_STATUS_CATALOG_TRANSPORT_ERR] = "Błąd transportu SC_GET_PARAM_LIST.",
    [SC_I18N_STATUS_SKIP_CATALOG_TRANSPORT] = "Pominięto: błąd transportu katalogu.",
    [SC_I18N_STATUS_CATALOG_PARSE_FAILED]   = "Niepowodzenie parsowania katalogu.",
    [SC_I18N_STATUS_SKIP_CATALOG_PARSE]     = "Pominięto: niepowodzenie parsowania katalogu.",
    [SC_I18N_STATUS_CATALOG_READ_FMT]       = "Katalog wczytany: %zu identyfikator(ów)%s",
    [SC_I18N_STATUS_TRUNCATED_SUFFIX]       = " (przycięty)",
    [SC_I18N_STATUS_VALUES_TRANSPORT_ERR]   = "Błąd transportu SC_GET_VALUES.",
    [SC_I18N_STATUS_SKIP_VALUES_TRANSPORT]  = "Pominięto: błąd transportu wartości.",
    [SC_I18N_STATUS_VALUES_PARSE_FAILED]    = "Niepowodzenie parsowania wartości.",
    [SC_I18N_STATUS_SKIP_VALUES_PARSE]      = "Pominięto: niepowodzenie parsowania wartości.",
    [SC_I18N_STATUS_VALUES_READ_FMT]        = "Wartości wczytane: %zu wpis(ów)%s",
    [SC_I18N_STATUS_NO_IDS_TO_PROBE]        = "Brak identyfikatorów parametrów do sondowania.",
    [SC_I18N_STATUS_PROBE_PARTIAL_FMT] =
        "Sonda częściowa: ok=%zu blad=%zu niezgodnosc=%zu",
    [SC_I18N_STATUS_PROBE_OK_FMT]           = "Sonda ok: %zu id, niezgodnosc=%zu",
    [SC_I18N_STATUS_PROBE_SKIPPED]          = "Sonda pominięta (szybkie auto-odświeżanie).",
};

static const char *const *const k_locale_tables[SC_LOCALE_COUNT] = {
    [SC_LOCALE_EN] = k_strings_en,
    [SC_LOCALE_PL] = k_strings_pl,
};

/* ── Active locale state ─────────────────────────────────────────────── */

static ScLocale s_locale = SC_LOCALE_EN;
static bool s_locale_initialized = false;

static const char *k_missing = "??";

/* ── Locale resolution ───────────────────────────────────────────────── */

ScLocale sc_i18n_parse_locale_string(const char *s)
{
    if (s == NULL || s[0] == '\0' || s[1] == '\0') {
        return SC_LOCALE_EN;
    }
    for (int i = 0; i < SC_LOCALE_COUNT; ++i) {
        const char *code = k_locale_codes[i];
        if (code != NULL && code[0] == s[0] && code[1] == s[1]) {
            return (ScLocale)i;
        }
    }
    return SC_LOCALE_EN;
}

static ScLocale resolve_locale_from_env(void)
{
    /* SC_LOCALE wins — explicit override for tests / power users. */
    const char *override = getenv("SC_LOCALE");
    if (override != NULL && override[0] != '\0') {
        return sc_i18n_parse_locale_string(override);
    }

    /* POSIX-flavour env vars — same semantics on Linux and on
     * Windows when launched from MSYS2 or after `setlocale("")`. */
    static const char *const k_lang_vars[] = { "LC_ALL", "LC_MESSAGES", "LANG" };
    for (size_t i = 0; i < sizeof(k_lang_vars) / sizeof(k_lang_vars[0]); ++i) {
        const char *v = getenv(k_lang_vars[i]);
        if (v != NULL && v[0] != '\0') {
            return sc_i18n_parse_locale_string(v);
        }
    }

    return SC_LOCALE_EN;
}

static void ensure_initialized(void)
{
    if (s_locale_initialized) {
        return;
    }
    s_locale = resolve_locale_from_env();
    s_locale_initialized = true;
}

/* ── Public API ──────────────────────────────────────────────────────── */

ScLocale sc_i18n_active_locale(void)
{
    ensure_initialized();
    return s_locale;
}

void sc_i18n_set_locale(ScLocale locale)
{
    if ((int)locale < 0 || (int)locale >= SC_LOCALE_COUNT) {
        locale = SC_LOCALE_EN;
    }
    s_locale = locale;
    s_locale_initialized = true;
}

const char *sc_i18n_locale_code(ScLocale locale)
{
    if ((int)locale < 0 || (int)locale >= SC_LOCALE_COUNT) {
        return k_locale_codes[SC_LOCALE_EN];
    }
    return k_locale_codes[locale];
}

const char *sc_i18n_missing_marker(void)
{
    return k_missing;
}

const char *sc_i18n_string_get(ScI18nKey key)
{
    ensure_initialized();
    if ((int)key < 0 || (int)key >= SC_I18N_KEY_COUNT) {
        return k_missing;
    }
    const char *const *table = k_locale_tables[s_locale];
    if (table == NULL || table[key] == NULL) {
        /* Fall back to English if the active locale has a gap. This
         * keeps the UI usable when a translator forgets a key, rather
         * than rendering "??" everywhere. */
        const char *const *fallback = k_locale_tables[SC_LOCALE_EN];
        if (fallback != NULL && fallback[key] != NULL) {
            return fallback[key];
        }
        return k_missing;
    }
    return table[key];
}

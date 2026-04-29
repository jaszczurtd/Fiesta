#include "sc_i18n.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ── Locale code table ───────────────────────────────────────────────── */

static const char *const k_locale_codes[SC_LOCALE_COUNT] = {
    [SC_LOCALE_EN] = "en",
    [SC_LOCALE_PL] = "pl",
};

/* ── String tables (designated initializers - missing slots are NULL) ── */

static const char *const k_strings_en[SC_I18N_KEY_COUNT] = {
    [SC_I18N_APP_TITLE]            = "Fiesta USB Configurator",
    [SC_I18N_TAB_MODULES]          = "Modules",
    [SC_I18N_TAB_FLASH]            = "Flash",
    [SC_I18N_FRAME_MODULES]        = "Modules",

    [SC_I18N_BTN_DETECT]           = "Detect Fiesta Modules",
    [SC_I18N_BTN_DISCONNECT]       = "Disconnect",
    [SC_I18N_BTN_DETECTING]        = "Detecting...",

    [SC_I18N_LOG_IDLE] =
        "Press \"Detect Fiesta Modules\" to discover connected Fiesta modules.\n",
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
    [SC_I18N_FLASH_SECTION_HEADER_FMT]   = "Flash - %s",
    [SC_I18N_FLASH_LBL_UF2]              = "UF2 artifact (auto):",
    [SC_I18N_FLASH_LBL_MANIFEST]         = "Manifest:",
    [SC_I18N_FLASH_BTN_PICK_MANIFEST]    = "Choose manifest...",
    [SC_I18N_FLASH_BTN_CLEAR_MANIFEST]   = "Clear",
    [SC_I18N_FLASH_BTN_FLASH]            = "Flash",
    [SC_I18N_FLASH_NO_PATH]              = "(none)",
    [SC_I18N_FLASH_DIALOG_MANIFEST_TITLE] = "Select manifest JSON",
    [SC_I18N_FLASH_FILTER_MANIFEST]      = "Manifest JSON",
    [SC_I18N_FLASH_STATUS_INITIAL]       =
        "Pick a manifest to start. The matching UF2 fills in automatically.",
    [SC_I18N_FLASH_STATUS_UF2_FAIL_FMT]  = "UF2 format check failed (%s): %s",
    [SC_I18N_FLASH_STATUS_MANIFEST_OK]   =
        "Manifest parsed; sha256 matches; UF2 format OK.",
    [SC_I18N_FLASH_STATUS_MANIFEST_PARSE_FAIL_FMT] =
        "Manifest parse failed: %s",
    [SC_I18N_FLASH_STATUS_MANIFEST_VERIFY_FAIL_FMT] =
        "Manifest verification failed: %s",
    [SC_I18N_FLASH_STATUS_MANIFEST_NEEDS_UF2] =
        "Manifest parsed; add the uf2_file field so the UF2 sidecar can be resolved.",
    [SC_I18N_FLASH_STATUS_FLASH_TODO]    =
        "Flash flow not implemented yet (Phase 6.5). Lock plumbing is exercised.",
    [SC_I18N_FLASH_STATUS_LOCK_RELEASED] =
        "Lock released. Pick paths and click Flash again once Phase 6.5 lands.",
    [SC_I18N_FLASH_STATUS_NEED_DETECTION] =
        "Detect the module before flashing.",
    [SC_I18N_FLASH_STATUS_NEED_UF2] =
        "Pick a manifest with uf2_file before flashing.",
    [SC_I18N_FLASH_STATUS_RUNNING_FMT] =
        "Flashing: %s",
    [SC_I18N_FLASH_STATUS_COPY_FRACTION_FMT] =
        "Flashing: COPY (%u%%)",
    [SC_I18N_FLASH_STATUS_DONE_FMT] =
        "Flash OK: %s",
    [SC_I18N_FLASH_STATUS_FAILED_FMT] =
        "Flash failed: %s - %s",
    [SC_I18N_FLASH_RESULT_OK]            = "OK",
    [SC_I18N_FLASH_RESULT_FAIL]          = "FAIL",

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

    [SC_I18N_TAB_VALUES]                    = "Values",
    [SC_I18N_VALUES_PLACEHOLDER]            = "Run detection to populate the Values tab.",
    [SC_I18N_VALUES_NO_DETECTED_MODULES]    = "No modules detected.",
    [SC_I18N_VALUES_NO_PARAMS]              = "No parameters exposed by this module.",
    [SC_I18N_VALUES_LOADING]                = "Loading parameters...",
    [SC_I18N_VALUES_LOAD_FAILED_FMT]        = "Failed to load %s: %s",
    [SC_I18N_VALUES_BTN_APPLY_STAGED]       = "Use",
    [SC_I18N_VALUES_BTN_COMMIT]             = "Save",
    [SC_I18N_VALUES_BTN_REVERT]             = "Revert",
    [SC_I18N_VALUES_AUTH_FAILED_FMT]        = "Authentication failed: %s",
    [SC_I18N_VALUES_APPLY_OK_FMT]           = "Applied %u parameter(s) to staging.",
    [SC_I18N_VALUES_APPLY_FAILED_FMT]       = "Apply failed for %s: %s",
    [SC_I18N_VALUES_NOTHING_TO_APPLY]       = "Nothing to apply - no edited parameters.",
    [SC_I18N_VALUES_COMMIT_OK]              = "Saved.",
    [SC_I18N_VALUES_COMMIT_FAILED_FMT]      = "Save failed: %s",
    [SC_I18N_VALUES_REVERT_OK]              = "Reverted.",
    [SC_I18N_VALUES_REVERT_FAILED_FMT]      = "Revert failed: %s",
};

/*
 * Polish translations. Protocol tokens (SC_GET_*, HELLO) are kept verbatim
 * because they are wire identifiers, not natural-language text. Format
 * specifiers stay in the same positions so the C-side caller does not need
 * to know which locale is active.
 */
static const char *const k_strings_pl[SC_I18N_KEY_COUNT] = {
    [SC_I18N_APP_TITLE]            = "Fiesta USB Configurator",
    [SC_I18N_TAB_MODULES]          = "Moduły",
    [SC_I18N_TAB_FLASH]            = "Flashowanie",
    [SC_I18N_FRAME_MODULES]        = "Moduły",

    [SC_I18N_BTN_DETECT]           = "Wykryj moduły Fiesta",
    [SC_I18N_BTN_DISCONNECT]       = "Rozłącz",
    [SC_I18N_BTN_DETECTING]        = "Wykrywanie...",

    [SC_I18N_LOG_IDLE] =
        "Naciśnij \"Wykryj moduły Fiesta\", aby wykryć podłączone moduły.\n",
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
    [SC_I18N_FLASH_SECTION_HEADER_FMT]   = "Flashowanie - %s",
    [SC_I18N_FLASH_LBL_UF2]              = "Plik UF2 (auto):",
    [SC_I18N_FLASH_LBL_MANIFEST]         = "Manifest:",
    [SC_I18N_FLASH_BTN_PICK_MANIFEST]    = "Wybierz manifest...",
    [SC_I18N_FLASH_BTN_CLEAR_MANIFEST]   = "Wyczyść",
    [SC_I18N_FLASH_BTN_FLASH]            = "Flashuj",
    [SC_I18N_FLASH_NO_PATH]              = "(brak)",
    [SC_I18N_FLASH_DIALOG_MANIFEST_TITLE] = "Wybierz manifest JSON",
    [SC_I18N_FLASH_FILTER_MANIFEST]      = "Manifest JSON",
    [SC_I18N_FLASH_STATUS_INITIAL]       =
        "Wybierz manifest, aby rozpocząć. Pasujący plik UF2 wypełni się automatycznie.",
    [SC_I18N_FLASH_STATUS_UF2_FAIL_FMT]  = "Niepowodzenie sprawdzania formatu UF2 (%s): %s",
    [SC_I18N_FLASH_STATUS_MANIFEST_OK]   =
        "Manifest sparsowany; sha256 zgadza się; format UF2 OK.",
    [SC_I18N_FLASH_STATUS_MANIFEST_PARSE_FAIL_FMT] =
        "Niepowodzenie parsowania manifestu: %s",
    [SC_I18N_FLASH_STATUS_MANIFEST_VERIFY_FAIL_FMT] =
        "Niepowodzenie weryfikacji manifestu: %s",
    [SC_I18N_FLASH_STATUS_MANIFEST_NEEDS_UF2] =
        "Manifest sparsowany; dodaj pole uf2_file, aby rozwiązać sąsiadujący plik UF2.",
    [SC_I18N_FLASH_STATUS_FLASH_TODO]    =
        "Przepływ flashowania jeszcze niezaimplementowany (Faza 6.5). "
        "Mechanizm blokady działa.",
    [SC_I18N_FLASH_STATUS_LOCK_RELEASED] =
        "Blokada zwolniona. Wybierz ścieżki i kliknij Flashuj ponownie, "
        "gdy Faza 6.5 będzie gotowa.",
    [SC_I18N_FLASH_STATUS_NEED_DETECTION] =
        "Wykryj moduł zanim zaczniesz flashowanie.",
    [SC_I18N_FLASH_STATUS_NEED_UF2] =
        "Wybierz manifest z polem uf2_file przed flashowaniem.",
    [SC_I18N_FLASH_STATUS_RUNNING_FMT] =
        "Flashowanie: %s",
    [SC_I18N_FLASH_STATUS_COPY_FRACTION_FMT] =
        "Flashowanie: COPY (%u%%)",
    [SC_I18N_FLASH_STATUS_DONE_FMT] =
        "Flash OK: %s",
    [SC_I18N_FLASH_STATUS_FAILED_FMT] =
        "Flashowanie nieudane: %s - %s",
    [SC_I18N_FLASH_RESULT_OK]            = "OK",
    [SC_I18N_FLASH_RESULT_FAIL]          = "BŁĄD",

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

    [SC_I18N_TAB_VALUES]                    = "Wartości",
    [SC_I18N_VALUES_PLACEHOLDER]            = "Uruchom detekcję, aby wypełnić zakładkę Wartości.",
    [SC_I18N_VALUES_NO_DETECTED_MODULES]    = "Nie wykryto żadnego modułu.",
    [SC_I18N_VALUES_NO_PARAMS]              = "Moduł nie udostępnia żadnych parametrów.",
    [SC_I18N_VALUES_LOADING]                = "Wczytywanie parametrów...",
    [SC_I18N_VALUES_LOAD_FAILED_FMT]        = "Nie udało się wczytać %s: %s",
    [SC_I18N_VALUES_BTN_APPLY_STAGED]       = "Użyj",
    [SC_I18N_VALUES_BTN_COMMIT]             = "Zapisz",
    [SC_I18N_VALUES_BTN_REVERT]             = "Cofnij",
    [SC_I18N_VALUES_AUTH_FAILED_FMT]        = "Uwierzytelnianie nie powiodło się: %s",
    [SC_I18N_VALUES_APPLY_OK_FMT]           = "Zastosowano %u parametr(ów) w buforze staging.",
    [SC_I18N_VALUES_APPLY_FAILED_FMT]       = "Nie udało się zastosować %s: %s",
    [SC_I18N_VALUES_NOTHING_TO_APPLY]       = "Brak zmian do zastosowania.",
    [SC_I18N_VALUES_COMMIT_OK]              = "Zapisano.",
    [SC_I18N_VALUES_COMMIT_FAILED_FMT]      = "Zapis nie powiódł się: %s",
    [SC_I18N_VALUES_REVERT_OK]              = "Cofnięto.",
    [SC_I18N_VALUES_REVERT_FAILED_FMT]      = "Cofnięcie nie powiodło się: %s",
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
    /* SC_LOCALE wins - explicit override for tests / power users. */
    const char *override = getenv("SC_LOCALE");
    if (override != NULL && override[0] != '\0') {
        return sc_i18n_parse_locale_string(override);
    }

    /* POSIX-flavour env vars - same semantics on Linux and on
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

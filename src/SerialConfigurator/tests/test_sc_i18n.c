/*
 * sc_i18n: locale resolution + table integrity tests.
 *
 * No GTK dependency - sc_i18n.c is pure C, so the test compiles and
 * runs identically on Linux and Windows hosts. The test source is
 * included directly because the file lives in the GUI source list
 * (src/ui/sc_i18n.c); embedding it here keeps the CTest target
 * independent of the GUI executable that needs GTK-4 to link.
 */

#include "../src/ui/sc_i18n.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s - %s (line %d)\n", __func__, (msg), __LINE__); \
            return 1; \
        } \
    } while (0)

#define TEST_ASSERT_STR_EQ(actual, expected, msg) \
    do { \
        if (strcmp((actual), (expected)) != 0) { \
            fprintf(stderr, \
                    "FAIL: %s - %s (line %d): got '%s', want '%s'\n", \
                    __func__, (msg), __LINE__, (actual), (expected)); \
            return 1; \
        } \
    } while (0)

static int test_every_key_has_an_english_string(void)
{
    sc_i18n_set_locale(SC_LOCALE_EN);
    for (int k = 0; k < SC_I18N_KEY_COUNT; ++k) {
        const char *s = sc_i18n_string_get((ScI18nKey)k);
        if (s == NULL || s[0] == '\0') {
            fprintf(stderr,
                    "FAIL: %s - key %d returned %s\n",
                    __func__, k, s == NULL ? "NULL" : "empty string");
            return 1;
        }
        if (strcmp(s, sc_i18n_missing_marker()) == 0) {
            fprintf(stderr,
                    "FAIL: %s - key %d returned the missing marker\n",
                    __func__, k);
            return 1;
        }
    }
    return 0;
}

static int test_every_key_has_a_polish_string(void)
{
    /* Polish translations are allowed to be identical to English when
     * the source string is a protocol token or a marker like '[WARN] '
     * - but the slot must NOT be NULL or the missing marker. */
    sc_i18n_set_locale(SC_LOCALE_PL);
    for (int k = 0; k < SC_I18N_KEY_COUNT; ++k) {
        const char *s = sc_i18n_string_get((ScI18nKey)k);
        if (s == NULL || s[0] == '\0') {
            fprintf(stderr,
                    "FAIL: %s - key %d returned %s under PL\n",
                    __func__, k, s == NULL ? "NULL" : "empty string");
            return 1;
        }
        if (strcmp(s, sc_i18n_missing_marker()) == 0) {
            fprintf(stderr,
                    "FAIL: %s - key %d returned the missing marker under PL\n",
                    __func__, k);
            return 1;
        }
    }
    return 0;
}

static int test_set_locale_changes_active_value(void)
{
    sc_i18n_set_locale(SC_LOCALE_EN);
    const char *en_btn = sc_i18n_string_get(SC_I18N_BTN_DETECT);
    sc_i18n_set_locale(SC_LOCALE_PL);
    const char *pl_btn = sc_i18n_string_get(SC_I18N_BTN_DETECT);
    TEST_ASSERT(strcmp(en_btn, pl_btn) != 0,
                "EN and PL detect-button labels must differ");
    TEST_ASSERT_STR_EQ(en_btn, "Detect Fiesta Modules", "EN detect label");
    TEST_ASSERT_STR_EQ(pl_btn, "Wykryj moduły Fiesta", "PL detect label");
    return 0;
}

static int test_locale_codes_are_iso639_two_letter(void)
{
    TEST_ASSERT_STR_EQ(sc_i18n_locale_code(SC_LOCALE_EN), "en", "en code");
    TEST_ASSERT_STR_EQ(sc_i18n_locale_code(SC_LOCALE_PL), "pl", "pl code");
    /* Out-of-range falls back to EN. */
    TEST_ASSERT_STR_EQ(sc_i18n_locale_code((ScLocale)42), "en", "OOR -> en");
    return 0;
}

static int test_parse_locale_from_string(void)
{
    /* POSIX-style xx_YY.ENC */
    TEST_ASSERT(sc_i18n_parse_locale_string("pl_PL.UTF-8") == SC_LOCALE_PL,
                "pl_PL.UTF-8 -> PL");
    TEST_ASSERT(sc_i18n_parse_locale_string("en_US") == SC_LOCALE_EN,
                "en_US -> EN");
    /* BCP-47 dash form */
    TEST_ASSERT(sc_i18n_parse_locale_string("pl-PL") == SC_LOCALE_PL,
                "pl-PL -> PL");
    /* Two-letter only */
    TEST_ASSERT(sc_i18n_parse_locale_string("pl") == SC_LOCALE_PL,
                "pl -> PL");
    /* Unknown locale falls back to EN */
    TEST_ASSERT(sc_i18n_parse_locale_string("de_DE") == SC_LOCALE_EN,
                "de_DE -> EN (no German support yet)");
    /* Edge cases */
    TEST_ASSERT(sc_i18n_parse_locale_string("") == SC_LOCALE_EN, "empty -> EN");
    TEST_ASSERT(sc_i18n_parse_locale_string(NULL) == SC_LOCALE_EN, "NULL -> EN");
    TEST_ASSERT(sc_i18n_parse_locale_string("z") == SC_LOCALE_EN,
                "single char -> EN");
    return 0;
}

static int test_format_keys_round_trip_through_snprintf(void)
{
    sc_i18n_set_locale(SC_LOCALE_EN);
    char buf[128];
    (void)snprintf(buf, sizeof(buf),
                   sc_i18n_string_get(SC_I18N_STATUS_CATALOG_READ_FMT),
                   (size_t)5,
                   sc_i18n_string_get(SC_I18N_STATUS_TRUNCATED_SUFFIX));
    TEST_ASSERT_STR_EQ(buf, "Catalog read: 5 id(s) (truncated)",
                       "EN catalog format with truncated suffix");

    sc_i18n_set_locale(SC_LOCALE_PL);
    (void)snprintf(buf, sizeof(buf),
                   sc_i18n_string_get(SC_I18N_STATUS_CATALOG_READ_FMT),
                   (size_t)5,
                   sc_i18n_string_get(SC_I18N_STATUS_TRUNCATED_SUFFIX));
    TEST_ASSERT(strstr(buf, "5") != NULL, "PL format embeds count");
    TEST_ASSERT(strstr(buf, "(przycięty)") != NULL,
                "PL format embeds the PL truncated suffix");
    return 0;
}

static int test_active_locale_getter(void)
{
    sc_i18n_set_locale(SC_LOCALE_PL);
    TEST_ASSERT(sc_i18n_active_locale() == SC_LOCALE_PL, "active is PL");
    sc_i18n_set_locale(SC_LOCALE_EN);
    TEST_ASSERT(sc_i18n_active_locale() == SC_LOCALE_EN, "active is EN");
    return 0;
}

static int test_out_of_range_key_returns_marker(void)
{
    const char *marker = sc_i18n_missing_marker();
    TEST_ASSERT_STR_EQ(sc_i18n_string_get((ScI18nKey)-1), marker, "negative key");
    TEST_ASSERT_STR_EQ(sc_i18n_string_get((ScI18nKey)SC_I18N_KEY_COUNT), marker,
                       "off-end key");
    return 0;
}

static int test_env_resolver_prefers_sc_locale_override(void)
{
    /* Reset the cached locale state so resolve_locale_from_env runs. */
    s_locale_initialized = false;
    s_locale = SC_LOCALE_EN;

    setenv("SC_LOCALE", "pl", 1);
    setenv("LANG", "en_US.UTF-8", 1);
    /* Force re-resolution by reading via the public API. */
    TEST_ASSERT(sc_i18n_active_locale() == SC_LOCALE_PL,
                "SC_LOCALE=pl wins over LANG=en_US");

    s_locale_initialized = false;
    unsetenv("SC_LOCALE");
    setenv("LANG", "pl_PL.UTF-8", 1);
    TEST_ASSERT(sc_i18n_active_locale() == SC_LOCALE_PL,
                "LANG=pl_PL.UTF-8 -> PL when SC_LOCALE absent");

    s_locale_initialized = false;
    unsetenv("SC_LOCALE");
    unsetenv("LANG");
    unsetenv("LC_ALL");
    unsetenv("LC_MESSAGES");
    TEST_ASSERT(sc_i18n_active_locale() == SC_LOCALE_EN,
                "no env -> EN");

    return 0;
}

int main(void)
{
    int failures = 0;
    failures += test_every_key_has_an_english_string();
    failures += test_every_key_has_a_polish_string();
    failures += test_set_locale_changes_active_value();
    failures += test_locale_codes_are_iso639_two_letter();
    failures += test_parse_locale_from_string();
    failures += test_format_keys_round_trip_through_snprintf();
    failures += test_active_locale_getter();
    failures += test_out_of_range_key_returns_marker();
    failures += test_env_resolver_prefers_sc_locale_override();

    if (failures != 0) {
        fprintf(stderr, "test_sc_i18n: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    fprintf(stdout, "test_sc_i18n: all 9 tests passed\n");
    return EXIT_SUCCESS;
}

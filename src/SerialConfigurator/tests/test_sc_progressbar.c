/*
 * sc_progressbar: state round-trip + clamping + NULL safety.
 *
 * The widget is built on GtkDrawingArea so the test needs an
 * initialized GTK runtime. We call gtk_init() once and then create
 * widgets without ever realising them - the state we exercise lives
 * in GObject user-data, not in the rendered surface, so an offscreen
 * widget is enough.
 *
 * The test source embeds sc_progressbar.c by #include, mirroring how
 * test_sc_i18n.c and test_sc_flash_paths.c integrate their UI sources
 * - no separate library extraction needed.
 */

#include "../src/ui/sc_progressbar.c"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg)                                                \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL: %s - %s (line %d)\n",                      \
                    __func__, (msg), __LINE__);                               \
            return 1;                                                         \
        }                                                                     \
    } while (0)

#define TEST_ASSERT_NEAR(a, b, eps, msg)                                      \
    do {                                                                      \
        if (fabs((a) - (b)) > (eps)) {                                        \
            fprintf(stderr,                                                   \
                    "FAIL: %s - %s (line %d): got %.6f, want %.6f\n",         \
                    __func__, (msg), __LINE__, (double)(a), (double)(b));    \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_default_fraction_is_zero(void)
{
    GtkWidget *bar = sc_progressbar_new(20);
    TEST_ASSERT(bar != NULL, "constructor returned non-NULL");
    TEST_ASSERT_NEAR(sc_progressbar_get_fraction(bar), 0.0, 1e-9,
                     "fresh widget reports 0.0");
    g_object_ref_sink(bar);
    g_object_unref(bar);
    return 0;
}

static int test_set_fraction_round_trip(void)
{
    GtkWidget *bar = sc_progressbar_new(20);
    g_object_ref_sink(bar);

    sc_progressbar_set_fraction(bar, 0.42);
    TEST_ASSERT_NEAR(sc_progressbar_get_fraction(bar), 0.42, 1e-9,
                     "0.42 round-trips");

    sc_progressbar_set_fraction(bar, 0.0);
    TEST_ASSERT_NEAR(sc_progressbar_get_fraction(bar), 0.0, 1e-9,
                     "0.0 round-trips");

    sc_progressbar_set_fraction(bar, 1.0);
    TEST_ASSERT_NEAR(sc_progressbar_get_fraction(bar), 1.0, 1e-9,
                     "1.0 round-trips");

    g_object_unref(bar);
    return 0;
}

static int test_set_fraction_clamps_out_of_range(void)
{
    GtkWidget *bar = sc_progressbar_new(20);
    g_object_ref_sink(bar);

    sc_progressbar_set_fraction(bar, 1.5);
    TEST_ASSERT_NEAR(sc_progressbar_get_fraction(bar), 1.0, 1e-9,
                     "1.5 clamps to 1.0");

    sc_progressbar_set_fraction(bar, -0.3);
    TEST_ASSERT_NEAR(sc_progressbar_get_fraction(bar), 0.0, 1e-9,
                     "-0.3 clamps to 0.0");

    sc_progressbar_set_fraction(bar, 1000.0);
    TEST_ASSERT_NEAR(sc_progressbar_get_fraction(bar), 1.0, 1e-9,
                     "huge values clamp to 1.0");

    g_object_unref(bar);
    return 0;
}

static int test_null_widget_is_safe(void)
{
    /* Neither call should crash or write through a NULL pointer. */
    sc_progressbar_set_fraction(NULL, 0.5);
    TEST_ASSERT_NEAR(sc_progressbar_get_fraction(NULL), 0.0, 1e-9,
                     "get on NULL returns 0.0");
    return 0;
}

static int test_get_on_non_progressbar_widget(void)
{
    /* If a caller hands us a foreign widget that has no state attached,
     * get must return 0.0 rather than dereferencing g_object_data junk. */
    GtkWidget *foreign = gtk_label_new("not a progress bar");
    g_object_ref_sink(foreign);
    TEST_ASSERT_NEAR(sc_progressbar_get_fraction(foreign), 0.0, 1e-9,
                     "foreign widget returns 0.0");
    /* set on foreign widget should also be a no-op (no state -> no
     * write). It must not crash. */
    sc_progressbar_set_fraction(foreign, 0.5);
    TEST_ASSERT_NEAR(sc_progressbar_get_fraction(foreign), 0.0, 1e-9,
                     "set on foreign widget stays 0.0");
    g_object_unref(foreign);
    return 0;
}

static int test_height_fallback_for_invalid_input(void)
{
    /* Negative or zero heights should fall back to the default 18 px;
     * we cannot read it back via the public API, but the constructor
     * must still produce a usable widget rather than asserting. */
    GtkWidget *zero = sc_progressbar_new(0);
    TEST_ASSERT(zero != NULL, "height=0 produces a widget");
    g_object_ref_sink(zero);
    g_object_unref(zero);

    GtkWidget *neg = sc_progressbar_new(-5);
    TEST_ASSERT(neg != NULL, "height=-5 produces a widget");
    g_object_ref_sink(neg);
    g_object_unref(neg);
    return 0;
}

int main(int argc, char **argv)
{
    /* gtk_init() requires a usable display. Headless CI machines
     * often lack one - skip-passing the suite there beats a hard
     * failure that masks real bugs. The DISPLAY / WAYLAND_DISPLAY
     * env vars are the canonical signal. */
    (void)argc;
    (void)argv;
    const char *display = g_getenv("DISPLAY");
    const char *wayland = g_getenv("WAYLAND_DISPLAY");
    if ((display == NULL || display[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0')) {
        fprintf(stdout, "test_sc_progressbar: skipped "
                "(no DISPLAY / WAYLAND_DISPLAY in env)\n");
        return EXIT_SUCCESS;
    }

    if (!gtk_init_check()) {
        fprintf(stdout, "test_sc_progressbar: skipped "
                "(gtk_init_check failed - no usable display)\n");
        return EXIT_SUCCESS;
    }

    int failures = 0;
    failures += test_default_fraction_is_zero();
    failures += test_set_fraction_round_trip();
    failures += test_set_fraction_clamps_out_of_range();
    failures += test_null_widget_is_safe();
    failures += test_get_on_non_progressbar_widget();
    failures += test_height_fallback_for_invalid_input();

    if (failures != 0) {
        fprintf(stderr, "test_sc_progressbar: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    fprintf(stdout, "test_sc_progressbar: all 6 tests passed\n");
    return EXIT_SUCCESS;
}

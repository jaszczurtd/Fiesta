#include "sc_core.h"

#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "[FAIL] %s\n", msg); \
    return 1; \
} } while (0)

static int test_null_safety(void)
{
    sc_core_init(0);
    sc_core_reset_detection(0);
    sc_core_detect_modules(0, 0, 0u);

    char log[128] = {0};
    sc_core_detect_modules(0, log, sizeof(log));
    TEST_ASSERT(strstr(log, "Core is not initialized") != 0, "missing NULL-core error log");
    return 0;
}

static int test_status_contract(void)
{
    ScCore core;
    sc_core_init(&core);

    TEST_ASSERT(sc_core_module_status(&core, SC_MODULE_COUNT) == 0, "status should be NULL for out-of-range index");
    TEST_ASSERT(sc_core_module_status(&core, SC_MODULE_COUNT + 10u) == 0, "status should be NULL for far out-of-range index");

    for (size_t i = 0u; i < sc_core_module_count(); ++i) {
        const ScModuleStatus *status = sc_core_module_status(&core, i);
        TEST_ASSERT(status != 0, "status should not be NULL");
        TEST_ASSERT(status->display_name != 0, "display_name should not be NULL");
        TEST_ASSERT(status->display_name[0] != '\0', "display_name should not be empty");
    }

    return 0;
}

static int test_detect_modules_preserves_status_layout(void)
{
    ScCore core;
    sc_core_init(&core);

    char log[2048];
    sc_core_detect_modules(&core, log, sizeof(log));

    for (size_t i = 0u; i < sc_core_module_count(); ++i) {
        const ScModuleStatus *status = sc_core_module_status(&core, i);
        TEST_ASSERT(status != 0, "status should not be NULL after detect");
        TEST_ASSERT(status->display_name != 0, "display_name should not be NULL after detect");
        TEST_ASSERT(status->display_name[0] != '\0', "display_name should not be empty after detect");
    }

    return 0;
}

int main(void)
{
    if (test_null_safety() != 0) {
        return 1;
    }

    if (test_status_contract() != 0) {
        return 1;
    }

    if (test_detect_modules_preserves_status_layout() != 0) {
        return 1;
    }

    printf("[OK] serial_configurator_core API contract tests passed\n");
    return 0;
}

#include "sc_core.h"

#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "[FAIL] %s\n", msg); \
    return 1; \
} } while (0)

static int test_init_defaults(void)
{
    ScCore core;
    sc_core_init(&core);

    const char *expected_names[SC_MODULE_COUNT] = { "ECU", "Clocks", "OilAndSpeed" };

    TEST_ASSERT(sc_core_module_count() == SC_MODULE_COUNT, "module count mismatch");

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *status = sc_core_module_status(&core, i);
        TEST_ASSERT(status != 0, "status pointer is NULL");
        TEST_ASSERT(strcmp(status->display_name, expected_names[i]) == 0, "display name mismatch");
        TEST_ASSERT(!status->detected, "module should not be detected after init");
        TEST_ASSERT(status->detected_instances == 0u, "detected_instances should be 0 after init");
        TEST_ASSERT(!status->target_ambiguous, "target_ambiguous should be false after init");
        TEST_ASSERT(status->port_path[0] == '\0', "port_path should be empty after init");
        TEST_ASSERT(status->hello_response[0] == '\0', "hello_response should be empty after init");
        TEST_ASSERT(!status->hello_identity.valid, "hello identity should be invalid after init");
        TEST_ASSERT(!status->meta_identity.valid, "meta identity should be invalid after init");
    }

    TEST_ASSERT(sc_core_module_status(&core, SC_MODULE_COUNT) == 0, "out-of-range status should be NULL");
    TEST_ASSERT(sc_core_module_status(0, 0u) == 0, "NULL core should return NULL status");

    return 0;
}

static int test_reset_clears_detection_fields(void)
{
    ScCore core;
    sc_core_init(&core);

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        core.modules[i].detected = true;
        core.modules[i].detected_instances = 2u;
        core.modules[i].target_ambiguous = true;
        (void)snprintf(core.modules[i].port_path, sizeof(core.modules[i].port_path), "mock-port-%zu", i);
        (void)snprintf(
            core.modules[i].hello_response,
            sizeof(core.modules[i].hello_response),
            "OK HELLO module=%s",
            core.modules[i].display_name
        );
        core.modules[i].hello_identity.valid = true;
        core.modules[i].meta_identity.valid = true;
    }

    sc_core_reset_detection(&core);

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *status = sc_core_module_status(&core, i);
        TEST_ASSERT(status != 0, "status pointer is NULL after reset");
        TEST_ASSERT(!status->detected, "detected flag should be false after reset");
        TEST_ASSERT(status->detected_instances == 0u, "detected_instances should be 0 after reset");
        TEST_ASSERT(!status->target_ambiguous, "target_ambiguous should be false after reset");
        TEST_ASSERT(status->port_path[0] == '\0', "port_path should be cleared by reset");
        TEST_ASSERT(status->hello_response[0] == '\0', "hello_response should be cleared by reset");
        TEST_ASSERT(!status->hello_identity.valid, "hello identity should be cleared by reset");
        TEST_ASSERT(!status->meta_identity.valid, "meta identity should be cleared by reset");
    }

    return 0;
}

int main(void)
{
    if (test_init_defaults() != 0) {
        return 1;
    }

    if (test_reset_clears_detection_fields() != 0) {
        return 1;
    }

    printf("[OK] serial_configurator_core smoke tests passed\n");
    return 0;
}

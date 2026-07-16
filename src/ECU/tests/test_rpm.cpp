#include "hal/hal_gpio.h"
#include "hal/hal_soft_timer.h"
#include "hal/impl/.mock/hal_mock.h"
#include "rpm.h"
#include "unity.h"

static void cleanupRpmResources(void) {
    uint8_t owner = HAL_GPIO_IRQ_CORE_NONE;
    if (hal_gpio_get_interrupt_owner_ex(PIO_INTERRUPT_HALL, &owner) == HAL_OK) {
        hal_mock_gpio_set_current_core(owner);
        TEST_ASSERT_EQUAL_INT(HAL_OK,
                              hal_gpio_detach_interrupt_ex(PIO_INTERRUPT_HALL));
    }

#ifndef VP37
    RPM *rpm = getRPMInstance();
    if (rpm->rpmCycleTimer != NULL) {
        hal_soft_timer_destroy(rpm->rpmCycleTimer);
        rpm->rpmCycleTimer = NULL;
    }
#endif
}

void setUp(void) {
    cleanupRpmResources();
    hal_mock_gpio_set_current_core(0u);
}

void tearDown(void) {
    cleanupRpmResources();
    hal_mock_gpio_set_current_core(0u);
}

void test_rpm_create_on_core1_registers_owned_irq(void) {
    hal_mock_gpio_set_current_core(RPM_IRQ_OWNER_CORE);

    TEST_ASSERT_EQUAL_INT(HAL_OK, RPM_create());

    uint8_t owner = HAL_GPIO_IRQ_CORE_NONE;
    TEST_ASSERT_EQUAL_INT(
        HAL_OK, hal_gpio_get_interrupt_owner_ex(PIO_INTERRUPT_HALL, &owner));
    TEST_ASSERT_EQUAL_UINT8(RPM_IRQ_OWNER_CORE, owner);
}

void test_rpm_create_on_core0_propagates_core_affinity_error(void) {
    hal_mock_gpio_set_current_core(0u);

    TEST_ASSERT_EQUAL_INT(HAL_ESTATE, RPM_create());

    uint8_t owner = 0u;
    TEST_ASSERT_EQUAL_INT(
        HAL_ENOENT, hal_gpio_get_interrupt_owner_ex(PIO_INTERRUPT_HALL, &owner));
    TEST_ASSERT_EQUAL_UINT8(HAL_GPIO_IRQ_CORE_NONE, owner);
#ifndef VP37
    TEST_ASSERT_NULL(getRPMInstance()->rpmCycleTimer);
#endif
}

void test_rpm_init_rejects_null_instance(void) {
    hal_mock_gpio_set_current_core(RPM_IRQ_OWNER_CORE);

    TEST_ASSERT_EQUAL_INT(HAL_EINVAL, RPM_init(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rpm_create_on_core1_registers_owned_irq);
    RUN_TEST(test_rpm_create_on_core0_propagates_core_affinity_error);
    RUN_TEST(test_rpm_init_rejects_null_instance);
    return UNITY_END();
}

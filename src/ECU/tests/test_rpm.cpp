#include "hal/gpio/hal_gpio.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/timers/hal_soft_timer.h"
#include "rpm.h"
#include "unity.h"
#include "vp37_adapter.h"

static void cleanupRpmResources(void) {
  uint8_t owner = HAL_GPIO_IRQ_CORE_NONE;
  if (hal_gpio_get_interrupt_owner_ex(JH_HW_NODE_HALL_PIN_IN, &owner) ==
      HAL_OK) {
    hal_mock_gpio_set_current_core(owner);
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_gpio_detach_interrupt_ex(JH_HW_NODE_HALL_PIN_IN));
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
  hal_mock_set_micros64(0u);
  hal_mock_set_micros_step(0u);
}

void tearDown(void) {
  cleanupRpmResources();
  hal_mock_gpio_set_current_core(0u);
}

void test_rpm_create_on_mock_core0_registers_owned_irq(void) {
  hal_mock_gpio_set_current_core(0u);

  TEST_ASSERT_EQUAL_INT(HAL_OK, RPM_create());

  uint8_t owner = HAL_GPIO_IRQ_CORE_NONE;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_gpio_get_interrupt_owner_ex(JH_HW_NODE_HALL_PIN_IN, &owner));
  TEST_ASSERT_EQUAL_UINT8(0u, owner);
}

void test_rpm_create_on_mock_core1_propagates_core_affinity_error(void) {
  hal_mock_gpio_set_current_core(1u);

  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, RPM_create());

  uint8_t owner = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_gpio_get_interrupt_owner_ex(
                                        JH_HW_NODE_HALL_PIN_IN, &owner));
  TEST_ASSERT_EQUAL_UINT8(HAL_GPIO_IRQ_CORE_NONE, owner);
#ifndef VP37
  TEST_ASSERT_NULL(getRPMInstance()->rpmCycleTimer);
#endif
}

void test_rpm_init_rejects_null_instance(void) {
  hal_mock_gpio_set_current_core(0u);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, RPM_init(NULL));
}

static void edgeAt(uint64_t timeUs) {
  hal_mock_set_micros64(timeUs);
  RPM_interrupt(getRPMInstance());
  RPM_process(getRPMInstance());
}

static void sampleAt(uint64_t timeUs) {
  hal_mock_set_micros64(timeUs);
  RPM_process(getRPMInstance());
}

static void startCapture(uint64_t timeUs) {
  hal_mock_set_micros64(timeUs);
  TEST_ASSERT_EQUAL_INT(HAL_OK, RPM_create());
}

void test_rpm_requires_two_edges_and_uses_actual_period(void) {
  startCapture(1000000u);
  sampleAt(2000000u);
  TEST_ASSERT_EQUAL_INT32(0, RPM_getCurrentRPM(getRPMInstance()));
  edgeAt(3000000u);
  TEST_ASSERT_EQUAL_INT32(0, RPM_getCurrentRPM(getRPMInstance()));
  edgeAt(3009375u); // 200 rpm, with no additive calibration offset.
  TEST_ASSERT_EQUAL_INT32(200, RPM_getCurrentRPM(getRPMInstance()));
}

void test_rpm_rising_edges_are_independent_of_duty_cycle(void) {
  const uint32_t highTimes[] = {400u, 1000u, 1600u};
  for (size_t duty = 0u; duty < COUNTOF(highTimes); ++duty) {
    cleanupRpmResources();
    startCapture(0u);
    hal_mock_gpio_inject_level(JH_HW_NODE_HALL_PIN_IN, false);
    for (uint64_t i = 0u; i < 20u; ++i) {
      const uint64_t rising = 10000u + i * 2000u;
      hal_mock_set_micros64(rising);
      hal_mock_gpio_inject_level(JH_HW_NODE_HALL_PIN_IN, true);
      // The mock injects levels and invokes registered callbacks separately.
      hal_mock_gpio_fire_interrupt(JH_HW_NODE_HALL_PIN_IN);
      hal_mock_set_micros64(rising + highTimes[duty]);
      hal_mock_gpio_inject_level(JH_HW_NODE_HALL_PIN_IN, false);
      RPM_process(getRPMInstance());
      if (i > 0u) {
        TEST_ASSERT_EQUAL_INT32(938, RPM_getCurrentRPM(getRPMInstance()));
      }
    }
  }
}

void test_rpm_averages_eight_periods_and_follows_a_speed_step(void) {
  startCapture(0u);
  uint64_t time = 10000u;
  edgeAt(time);
  for (unsigned i = 0u; i < 16u; ++i) {
    time += 2500u;
    edgeAt(time);
  }
  TEST_ASSERT_EQUAL_INT32(750, RPM_getCurrentRPM(getRPMInstance()));
  time += 1250u;
  edgeAt(time);
  TEST_ASSERT_EQUAL_INT32(800, RPM_getCurrentRPM(getRPMInstance()));
  for (unsigned i = 1u; i < 8u; ++i) {
    time += 1250u;
    edgeAt(time);
  }
  TEST_ASSERT_EQUAL_INT32(1500, RPM_getCurrentRPM(getRPMInstance()));
  for (unsigned i = 0u; i < 8u; ++i) {
    time += 2500u;
    edgeAt(time);
  }
  TEST_ASSERT_EQUAL_INT32(750, RPM_getCurrentRPM(getRPMInstance()));
}

void test_rpm_limits_average_to_25ms_and_handles_one_long_period(void) {
  startCapture(0u);
  edgeAt(0u);
  edgeAt(10000u);
  edgeAt(20000u);
  edgeAt(30000u);
  edgeAt(35000u);
  // Last 25 ms contain three periods. An older fourth would take 35 ms.
  TEST_ASSERT_EQUAL_INT32(225, RPM_getCurrentRPM(getRPMInstance()));
  // At 50 rpm a single period is longer than the averaging window.
  edgeAt(1000000u);
  edgeAt(1037500u);
  TEST_ASSERT_EQUAL_INT32(50, RPM_getCurrentRPM(getRPMInstance()));
}

void test_rpm_glitch_does_not_shift_timestamp_or_extend_signal_lifetime(void) {
  startCapture(0u);
  edgeAt(10000u);
  edgeAt(10050u); // Too short to accept, not a new time origin.
  edgeAt(12000u);
  TEST_ASSERT_EQUAL_INT32(938, RPM_getCurrentRPM(getRPMInstance()));
  edgeAt(12050u);
  sampleAt(32000u);
  TEST_ASSERT_EQUAL_INT32(0, RPM_getCurrentRPM(getRPMInstance()));
  edgeAt(33000u);
  TEST_ASSERT_EQUAL_INT32(0, RPM_getCurrentRPM(getRPMInstance()));
  edgeAt(35000u);
  TEST_ASSERT_EQUAL_INT32(938, RPM_getCurrentRPM(getRPMInstance()));
}

void test_rpm_timeout_is_adaptive_and_restart_drops_old_samples(void) {
  startCapture(0u);
  edgeAt(0u);
  edgeAt(10000u);
  edgeAt(20000u);
  sampleAt(49999u);
  TEST_ASSERT_EQUAL_INT32(188, RPM_getCurrentRPM(getRPMInstance()));
  sampleAt(50000u);
  TEST_ASSERT_EQUAL_INT32(0, RPM_getCurrentRPM(getRPMInstance()));
  edgeAt(1000000u);
  TEST_ASSERT_EQUAL_INT32(0, RPM_getCurrentRPM(getRPMInstance()));
  edgeAt(1001250u);
  TEST_ASSERT_EQUAL_INT32(1500, RPM_getCurrentRPM(getRPMInstance()));
}

void test_rpm_isr_restarts_after_loss_without_a_process_call(void) {
  startCapture(0u);
  edgeAt(10000u);
  edgeAt(12000u);
  edgeAt(1000000u);
  TEST_ASSERT_EQUAL_INT32(0, RPM_getCurrentRPM(getRPMInstance()));
  edgeAt(1009375u);
  TEST_ASSERT_EQUAL_INT32(200, RPM_getCurrentRPM(getRPMInstance()));
}

void test_rpm_slow_cranking_has_bounded_loss_timeout(void) {
  startCapture(0u);
  edgeAt(0u);
  edgeAt(187500u);
  TEST_ASSERT_EQUAL_INT32(10, RPM_getCurrentRPM(getRPMInstance()));
  sampleAt(687499u);
  TEST_ASSERT_EQUAL_INT32(10, RPM_getCurrentRPM(getRPMInstance()));
  sampleAt(687500u);
  TEST_ASSERT_EQUAL_INT32(0, RPM_getCurrentRPM(getRPMInstance()));
}

void test_rpm_capture_and_timeout_cross_microsecond_wrap(void) {
  const uint64_t start = UINT32_MAX - UINT64_C(1000);
  startCapture(start);
  edgeAt(start);
  edgeAt(start + 2000u);
  TEST_ASSERT_EQUAL_INT32(938, RPM_getCurrentRPM(getRPMInstance()));
  sampleAt(start + 21999u);
  TEST_ASSERT_EQUAL_INT32(938, RPM_getCurrentRPM(getRPMInstance()));
  sampleAt(start + 22000u);
  TEST_ASSERT_EQUAL_INT32(0, RPM_getCurrentRPM(getRPMInstance()));
}

void test_rpm_measurement_crosses_millisecond_sign_boundary(void) {
  const uint64_t start = (UINT64_C(1) << 31u) * 1000u - 1000u;
  startCapture(start);
  edgeAt(start);
  edgeAt(start + 2000u);
  TEST_ASSERT_EQUAL_INT32(938, RPM_getCurrentRPM(getRPMInstance()));
}

void test_rpm_overspeed_reaches_the_real_adapter_interlock(void) {
  initSensors();
  startCapture(0u);
  edgeAt(10000u);
  edgeAt(10300u); // 6250 rpm, above the engine limit but below capture ceiling.
  TEST_ASSERT_EQUAL_INT32(6250, RPM_getCurrentRPM(getRPMInstance()));
  setGlobalValue(F_RPM, (float)RPM_getCurrentRPM(getRPMInstance()));
  TEST_ASSERT_FALSE(vp37AdapterCallbacks()->driveAllowed());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_rpm_create_on_mock_core0_registers_owned_irq);
  RUN_TEST(test_rpm_create_on_mock_core1_propagates_core_affinity_error);
  RUN_TEST(test_rpm_init_rejects_null_instance);
  RUN_TEST(test_rpm_requires_two_edges_and_uses_actual_period);
  RUN_TEST(test_rpm_rising_edges_are_independent_of_duty_cycle);
  RUN_TEST(test_rpm_averages_eight_periods_and_follows_a_speed_step);
  RUN_TEST(test_rpm_limits_average_to_25ms_and_handles_one_long_period);
  RUN_TEST(test_rpm_glitch_does_not_shift_timestamp_or_extend_signal_lifetime);
  RUN_TEST(test_rpm_timeout_is_adaptive_and_restart_drops_old_samples);
  RUN_TEST(test_rpm_isr_restarts_after_loss_without_a_process_call);
  RUN_TEST(test_rpm_slow_cranking_has_bounded_loss_timeout);
  RUN_TEST(test_rpm_capture_and_timeout_cross_microsecond_wrap);
  RUN_TEST(test_rpm_measurement_crosses_millisecond_sign_boundary);
  RUN_TEST(test_rpm_overspeed_reaches_the_real_adapter_interlock);
  return UNITY_END();
}

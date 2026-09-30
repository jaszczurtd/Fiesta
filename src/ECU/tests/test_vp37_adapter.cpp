// The ECU side of the VP37 module: the board services of vp37_adapter.c, the
// values published into the ECU's global table, and the bench test layer
// driving the pump. The module's own suite lives in
// src/common/vp37/tests/test_vp37.cpp.
#include "dtcManager.h"
#include "ecuContext.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
#include "testable/adjustometer_test_helpers.h"
#include "testable/vp37_test_fixture.h"
#include "tests_workers.h"
#include "unity.h"
#include "vp37_adapter.h"
#include <math.h>

// ── Lifecycle ────────────────────────────────────────────────────────────────

void setUp(void) { setUpVp37Fixture(); }

void tearDown(void) {
  (void)VP37_currentScanStop();
  hal_mock_i2c_set_busy(false);
  VP37Pump *pump = &getECUContext()->injectionPump;
  if (pump->pid.controller != NULL) {
    hal_pid_controller_destroy(pump->pid.controller);
    pump->pid.controller = NULL;
  }
}

void test_vp37_process_disables_when_rpm_above_max(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  setupPumpForProcessTests(pump);

  setGlobalValue(F_RPM, (float)(RPM_MAX_EVER + 1));
  injectAdjRegisterData(300, 136, 42, ADJ_STATUS_OK);
  VP37_process(pump);

  TEST_ASSERT_FALSE(pump->vp37Initialized);
}

void test_vp37_process_updates_globals_from_adjustometer_reading(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  setupPumpForProcessTests(pump);

  // The pump keeps the frame's temperature and supply; the ECU copies them
  // from the published status into its global values.
  injectAdjRegisterData(321, 137, 44, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 14.0f, getGlobalValue(F_VOLTS));
  vp37AdapterPublish(pump);

  TEST_ASSERT_EQUAL_INT32(321, pump->feedback.position);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 13.7f, pump->feedback.supplyVolts);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 44.0f, pump->feedback.fuelTempC);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 13.7f, getGlobalValue(F_VOLTS));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 44.0f, getGlobalValue(F_FUEL_TEMP));
}

static bool pwmDtcActive(void) {
  uint16_t codes[32] = {0};
  const uint8_t count = dtcManagerGetCodes(DTC_KIND_ACTIVE, codes, 32);
  for (uint8_t i = 0U; i < count; i++) {
    if (codes[i] == DTC_PWM_CHANNEL_NOT_INIT) {
      return true;
    }
  }
  return false;
}

void test_vp37_adapter_pwm_writes_leave_the_dtc_store_alone(void) {
  // The control step must not wait on the DTC mutex: the adapter writes the
  // channels directly, while valToPWM() still reports the DTC on each write.
  dtcManagerSetActive(DTC_PWM_CHANNEL_NOT_INIT, true);
  vp37AdapterCallbacks()->writeQuantityPwm(500);
  vp37AdapterCallbacks()->writeTimingPwm(300);
  TEST_ASSERT_TRUE(pwmDtcActive());
  valToPWM(PIO_VP37_RPM, 500);
  TEST_ASSERT_FALSE(pwmDtcActive());
}

void test_vp37_invalid_timing_or_pid_step_disables_output(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50);
  VP37_enableVP37(pump, true);
  hal_mock_i2c_reset_write_log();
  pump->pidTimeUpdate = 0.0f;
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  uint8_t latch = 0xff;
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame(0, &latch, 1));
  TEST_ASSERT_EQUAL_UINT8(0, latch & (1U << PCF8574_O_VP37_ENABLE));

  pump->vp37Initialized = true;
  pump->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  VP37_enableVP37(pump, true);
  injectAdjRegisterData(4600, 144, 26, ADJ_STATUS_OK);
  hal_mock_i2c_reset_write_log();
  pump->output.finalPWM = 700;
  hal_pid_controller_set_tf(pump->pid.controller, -1.0f);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.lastPWMval);
  const int lastFrame = hal_mock_i2c_get_write_frame_count() - 1;
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame(lastFrame, &latch, 1));
  TEST_ASSERT_EQUAL_UINT8(0, latch & (1U << PCF8574_O_VP37_ENABLE));
}

#if ECU_FUNCTIONAL_TESTS_ENABLED
void test_vp37_cyclic_counts_full_cycles_and_restarts_deterministically(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  TEST_ASSERT_TRUE(initTests());

  // Nothing runs until a test is started, and the demand stays with whoever
  // owns it outside the test layer.
  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_EQUAL_UINT32(0U, testsCyclicDelayMs());

  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_CYCLIC));
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_A, testsCyclicDelayMs());

  for (uint32_t step = 1U; step <= 200U * CYCLIC_FULL_CYCLES; ++step) {
    hal_mock_set_millis(step * CYCLIC_DELAYTIME_A);
    TEST_ASSERT_TRUE(tickTests());
    if (step == 100U) {
      TEST_ASSERT_FLOAT_WITHIN(.001f, 100.0f, pump->demand.requestedPercent);
    }
    if (step == 200U) {
      TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.requestedPercent);
      TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_A, testsCyclicDelayMs());
    }
  }
  TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_B, testsCyclicDelayMs());

  // Restarting the same test rewinds the generator to the first profile.
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_CYCLIC));
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_A, testsCyclicDelayMs());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.requestedPercent);
  hal_mock_set_millis(hal_millis() + CYCLIC_DELAYTIME_A);
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 1.0f, pump->demand.requestedPercent);

  // Stopping hands the demand back and commands zero on the way out.
  TEST_ASSERT_EQUAL_INT(HAL_OK, stopTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.requestedPercent);
  TEST_ASSERT_FALSE(tickTests());
}

#endif

#if ECU_FUNCTIONAL_TESTS_ENABLED
void test_vp37_serial_demand_remains_until_the_next_command(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  TEST_ASSERT_TRUE(initTests());

  // A demand command starts the manual test by itself.
  tickTestsHandleSerialLine("S73");
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 73.0f, pump->demand.requestedPercent);

  hal_mock_set_millis(60000U);
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 73.0f, pump->demand.requestedPercent);

  tickTestsHandleSerialLine("S25.5");
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 25.5f, pump->demand.requestedPercent);

  // An auto-zero deadline releases the demand without ending the test.
  tickTestsHandleSerialLine("G2000");
  TEST_ASSERT_TRUE(tickTests());
  tickTestsHandleSerialLine("S40");
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 40.0f, pump->demand.requestedPercent);
  hal_mock_set_millis(hal_millis() + 2001U);
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.requestedPercent);
}
#endif

#if ECU_FUNCTIONAL_TESTS_ENABLED
void test_vp37_current_observation_command_preserves_control_state(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->currentControl.enabled = true;
  TEST_ASSERT_TRUE(initTests());
  tickTestsHandleSerialLine("S73");
  tickTests();
  tickTests();
  const int32_t pwm = pump->output.finalPWM;
  const float integral = pump->pid.terms.integral;
  const int32_t target = pump->demand.target;
  const struct {
    const char *line;
    bool observation;
    bool feedback;
  } commands[] = {
      {"Q1", true, true},       {"Q0", false, true},      {"Q0.5", false, true},
      {"Q1extra", false, true}, {"Q1", true, true},       {"C0", true, false},
      {"C0.5", true, false},    {"C1extra", true, false}, {"C1", true, true},
      {"C0.5", true, true},     {"C1extra", true, true}};
  pump->currentControl.correctionPwm = 12.0f;
  for (size_t i = 0U; i < COUNTOF(commands); i++) {
    tickTestsHandleSerialLine(commands[i].line);
    tickTests();
    tickTests();
    TEST_ASSERT_EQUAL(commands[i].observation,
                      pump->thermal.observationEnabled);
    TEST_ASSERT_EQUAL(commands[i].feedback, pump->currentControl.enabled);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, pump->currentControl.correctionPwm);
    TEST_ASSERT_EQUAL_INT32(pwm, pump->output.finalPWM);
    TEST_ASSERT_EQUAL_INT32(target, pump->demand.target);
    TEST_ASSERT_FLOAT_WITHIN(.001f, integral, pump->pid.terms.integral);
  }
}
#endif

void test_vp37_sensor_demand_uses_shared_ramp_and_motion_feedforward(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->pid.topKd = .004f;
  pump->feedforward.motionBoostUp = VP37_PWM_FF_MOTION_BOOST_DEFAULT;
  int32_t previousTarget = -1;
  float peakMotion = 0.0f;
  // Raw ADC fixtures for 90..95% with the 1795..3605 calibration. The
  // RP2040 transfer-gap compensation adds 16 before the sensor maps them.
  const int rawCodes[] = {1960, 1941, 1923, 1905, 1887, 1869};
  for (uint32_t ms = 0U; ms <= 800U; ms += 5U) {
    const uint32_t inputStep = ms < 310U ? (ms / 62U) : 5U;
    const uint32_t motionMs = ms < 310U ? ms : 310U;
    hal_mock_set_millis(ms);
    if ((ms % 10U) == 0U) {
      hal_mock_adc_inject(ADC_SENSORS_PIN, rawCodes[inputStep]);
      readThrottleValues();
    }
    TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(
                                  pump, getDriverDemandPercent()));
    if (previousTarget < 0) {
      previousTarget = pump->demand.target;
    }
    injectAdjRegisterData((int16_t)(8200U + ((90U * motionMs) / 62U)), 144U,
                          49U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(previousTarget, pump->demand.target);
    // Less than 0.4% of this 9000 Hz stroke per 5 ms control step.
    TEST_ASSERT_LESS_OR_EQUAL_INT32(36, pump->demand.target - previousTarget);
    TEST_ASSERT_LESS_THAN_FLOAT(20.0f, pump->feedforward.motion);
    peakMotion = fmaxf(peakMotion, pump->feedforward.motion);
    previousTarget = pump->demand.target;
  }
  // The shared upper profile suppresses acceleration for this source too.
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, peakMotion);
  TEST_ASSERT_FLOAT_WITHIN(.11f, 95.0f, pump->demand.requestedPercent);
  TEST_ASSERT_INT32_WITHIN(10, 8650, pump->demand.target);
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
}

// ── The Adjustometer seen from the ECU ──────────────────────────────────────

// ── DTC array expansion test ─────────────────────────────────────────────────

void test_dtc_array_covers_adjustometer_codes(void) {
  // Verify all 4 new DTC codes are recognized by dtcManager
  dtcManagerSetActive(DTC_ADJ_COMM_LOST, true);
  dtcManagerSetActive(DTC_ADJ_SIGNAL_LOST, true);
  dtcManagerSetActive(DTC_ADJ_FUEL_TEMP_BROKEN, true);
  dtcManagerSetActive(DTC_ADJ_VOLTAGE_BAD, true);

  uint16_t codes[16];
  uint8_t count = dtcManagerGetCodes(DTC_KIND_ACTIVE, codes, 16);
  TEST_ASSERT_GREATER_OR_EQUAL(4, count);

  bool foundComm = false, foundSig = false, foundFt = false, foundVolt = false;
  for (uint8_t i = 0; i < count; i++) {
    if (codes[i] == DTC_ADJ_COMM_LOST)
      foundComm = true;
    if (codes[i] == DTC_ADJ_SIGNAL_LOST)
      foundSig = true;
    if (codes[i] == DTC_ADJ_FUEL_TEMP_BROKEN)
      foundFt = true;
    if (codes[i] == DTC_ADJ_VOLTAGE_BAD)
      foundVolt = true;
  }
  TEST_ASSERT_TRUE(foundComm);
  TEST_ASSERT_TRUE(foundSig);
  TEST_ASSERT_TRUE(foundFt);
  TEST_ASSERT_TRUE(foundVolt);
}

void test_supply_voltage_reads_the_local_divider_regardless_of_the_bus(void) {
  injectLocalSupplyVoltage(12.4f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 12.4f, getSystemSupplyVoltage());
  // A busy Adjustometer bus does not touch the ECU's supply reading.
  hal_mock_i2c_set_busy(true);
  injectLocalSupplyVoltage(12.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 12.0f, getSystemSupplyVoltage());
}

void test_adjustometer_status_flags_do_not_auto_set_dtc(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  const uint8_t statuses[] = {ADJ_STATUS_SIGNAL_LOST,
                              ADJ_STATUS_FUEL_TEMP_BROKEN,
                              ADJ_STATUS_VOLTAGE_BAD};
  for (size_t i = 0U; i < COUNTOF(statuses); i++) {
    injectAdjRegisterData(100, 120, 40, statuses[i]);
    (void)VP37_readAdjustometer(pump);
  }
  TEST_ASSERT_EQUAL_UINT8(0, dtcManagerCount(DTC_KIND_ACTIVE));
}
void test_vp37_reinitialization_does_not_exhaust_pwm_channels(void) {
  for (unsigned int i = 0; i <= HAL_PWM_FREQ_MAX_CHANNELS; ++i) {
    pwm_init();
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_vp37_sensor_demand_uses_shared_ramp_and_motion_feedforward);

  RUN_TEST(test_vp37_process_disables_when_rpm_above_max);
  RUN_TEST(test_vp37_process_updates_globals_from_adjustometer_reading);
  RUN_TEST(test_vp37_adapter_pwm_writes_leave_the_dtc_store_alone);

  RUN_TEST(test_vp37_invalid_timing_or_pid_step_disables_output);
#if ECU_FUNCTIONAL_TESTS_ENABLED
  RUN_TEST(test_vp37_current_observation_command_preserves_control_state);
#endif
#if ECU_FUNCTIONAL_TESTS_ENABLED
  RUN_TEST(test_vp37_cyclic_counts_full_cycles_and_restarts_deterministically);
  RUN_TEST(test_vp37_serial_demand_remains_until_the_next_command);
#endif
  RUN_TEST(test_dtc_array_covers_adjustometer_codes);
  RUN_TEST(test_supply_voltage_reads_the_local_divider_regardless_of_the_bus);
  RUN_TEST(test_adjustometer_status_flags_do_not_auto_set_dtc);
  RUN_TEST(test_vp37_reinitialization_does_not_exhaust_pwm_channels);
  return UNITY_END();
}

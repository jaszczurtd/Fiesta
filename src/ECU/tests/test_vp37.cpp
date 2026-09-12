#include "dtcManager.h"
#include "ecuContext.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
#include "testable/adjustometer_test_helpers.h"
#include "unity.h"
#include "vp37.h"
#include <math.h>

// ── Helpers ──────────────────────────────────────────────────────────────────

static void injectAdjRegisterData(int16_t pulseHz, uint8_t voltage,
                                  uint8_t fuelTemp, uint8_t status) {
  uint8_t buf[5];
  buf[0] = (uint8_t)((uint16_t)pulseHz >> 8);
  buf[1] = (uint8_t)((uint16_t)pulseHz & 0xFF);
  buf[2] = voltage;
  buf[3] = fuelTemp;
  buf[4] = status;
  hal_mock_i2c_inject_rx(buf, 5);
}

typedef struct {
  uint8_t bytes[255];
  int length;
} AdjustometerScript;

static void appendAdjRegisterData(AdjustometerScript *script, int16_t pulseHz,
                                  uint8_t voltage, uint8_t fuelTemp,
                                  uint8_t status) {
  if (script->length + 5 > (int)sizeof(script->bytes)) {
    return;
  }

  script->bytes[script->length++] = (uint8_t)((uint16_t)pulseHz >> 8);
  script->bytes[script->length++] = (uint8_t)((uint16_t)pulseHz & 0xFF);
  script->bytes[script->length++] = voltage;
  script->bytes[script->length++] = fuelTemp;
  script->bytes[script->length++] = status;
}

static void appendAdjRegisterDataRepeated(AdjustometerScript *script, int count,
                                          int16_t pulseHz, uint8_t voltage,
                                          uint8_t fuelTemp, uint8_t status) {
  for (int i = 0; i < count; i++) {
    appendAdjRegisterData(script, pulseHz, voltage, fuelTemp, status);
  }
}

static void injectAdjustometerScript(const AdjustometerScript *script) {
  hal_mock_i2c_inject_rx(script->bytes, script->length);
}

static void setupPumpForProcessTests(VP37Pump *pump) {
  memset(pump, 0, sizeof(*pump));
  pump->adjustController = hal_pid_controller_create();
  pump->vp37Initialized = true;
  pump->calibrationDone = true;
  pump->VP37_ADJUST_MIN = 100;
  pump->VP37_ADJUST_MAX = 9100;
  pump->VP37_ADJUST_MIDDLE =
      (pump->VP37_ADJUST_MAX + pump->VP37_ADJUST_MIN) / 2;
  pump->desiredAdjustometerTarget = -1;
  pump->desiredAdjustometer = -1;
  pump->lastThrottle = -1.0f;
  pump->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  pump->temperatureCorrection = 1.0f;
  pump->temperatureCompensationWeight = 1.0f;
  pump->compensationVolts = NOMINAL_VOLTAGE;
  pump->voltageFilterTimeConstant = VP37_VOLTAGE_FILTER_S;
  pump->voltageReady = false;
  pump->throttleRampLastMs = hal_millis();
  pump->lastAdjustometerStatus = ADJ_STATUS_OK;
  VP37_setVP37PID(pump, VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, false);
  hal_pid_controller_set_tf(pump->adjustController, VP37_PID_TF);
  hal_pid_controller_set_max_integral(pump->adjustController,
                                      VP37_PID_MAX_INTEGRAL);
  setGlobalValue(F_RPM, 1000.0f);
  setGlobalValue(F_VOLTS, 14.0f);
  setGlobalValue(F_THROTTLE_POS, 0.0f);
  hal_mock_i2c_set_busy(false);
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void setUp(void) {
  hal_mock_set_millis(0);
  hal_i2c_init(4, 5, 400000);
  initSensors();
  initI2C();
  dtcManagerInit();
  dtcManagerClearAll();
}

void tearDown(void) {
  hal_mock_i2c_set_busy(false);
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  if (pump->adjustController != NULL) {
    hal_pid_controller_destroy(pump->adjustController);
    pump->adjustController = NULL;
  }
}

// ── VP37 PID API tests ───────────────────────────────────────────────────────

void test_vp37_pid_setter_updates_controller_gains(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->adjustController = hal_pid_controller_create();

  VP37_setVP37PID(pump, 0.55f, 0.12f, 0.025f, false);

  float kp, ki, kd;
  VP37_getVP37PIDValues(pump, &kp, &ki, &kd);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.55f, kp);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.12f, ki);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.025f, kd);
}

void test_vp37_pid_reset_restores_pwm_tracking_state(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->adjustController = hal_pid_controller_create();
  pump->lastPWMval = 777;
  pump->finalPWM = 888;

  VP37_setVP37PID(pump, 0.20f, 0.10f, 0.05f, true);

  TEST_ASSERT_EQUAL_INT32(-1, pump->lastPWMval);
  TEST_ASSERT_EQUAL_INT32(VP37_PWM_MIN, pump->finalPWM);
}

void test_vp37_throttle_caps_target_to_configured_range(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->calibrationDone = true;
  pump->VP37_ADJUST_MIN = 100;
  pump->VP37_ADJUST_MAX = 9100;

  VP37_setVP37Throttle(pump, 100.0f);

  int32_t expectedTarget = (int32_t)hal_math_map_f32(
      (float)VP37_ACCELERATION_MAX, VP37_PERCENT_MIN, VP37_PERCENT_MAX,
      (float)pump->VP37_ADJUST_MIN, (float)pump->VP37_ADJUST_MAX);
  TEST_ASSERT_EQUAL_INT32(expectedTarget, pump->desiredAdjustometerTarget);
}

void test_vp37_throttle_caps_target_to_min_for_negative_input(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->calibrationDone = true;
  pump->VP37_ADJUST_MIN = 200;
  pump->VP37_ADJUST_MAX = 9200;

  VP37_setVP37Throttle(pump, -10.0f);

  TEST_ASSERT_EQUAL_INT32(pump->VP37_ADJUST_MIN,
                          pump->desiredAdjustometerTarget);
}

void test_vp37_pid_time_update_setter(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  pump->pidTimeUpdate = 45.0f;

  pump->pidTimeUpdate = 60.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, VP37_getVP37PIDTimeUpdate(pump));
}

void test_vp37_percentage_error_constant(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, PERCENTAGE_ERROR);
}

void test_vp37_init_returns_already_initialized(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->vp37Initialized = true;

  VP37InitStatus status = VP37_init(pump);

  TEST_ASSERT_EQUAL_INT(VP37_INIT_ALREADY_INITIALIZED, status);
}

void test_vp37_init_returns_ok_when_baseline_ready(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));

  // The I2C mock accepts one complete byte script. Baseline consumes the
  // first frame, then calibration observes stable MIN before stable MAX.
  AdjustometerScript script = {};
  appendAdjRegisterData(&script, 100, 132, 40, ADJ_STATUS_OK);
  appendAdjRegisterDataRepeated(&script, 10, 100, 132, 40, ADJ_STATUS_OK);
  appendAdjRegisterDataRepeated(&script, 10, 8200, 132, 40, ADJ_STATUS_OK);
  appendAdjRegisterData(&script, 8200, 132, 40, ADJ_STATUS_OK);
  injectAdjustometerScript(&script);

  VP37InitStatus status = VP37_init(pump);

  TEST_ASSERT_EQUAL_INT(VP37_INIT_OK, status);
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_TRUE(pump->calibrationDone);
  TEST_ASSERT_EQUAL_INT32(100, pump->VP37_ADJUST_MIN);
  TEST_ASSERT_EQUAL_INT32(8200, pump->VP37_ADJUST_MAX);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2U * VP37_CALIBRATION_MIN_SETTLE_MS,
                                      hal_millis());
}

void test_vp37_init_rejects_insufficient_calibration_travel(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));

  AdjustometerScript script = {};
  appendAdjRegisterData(&script, 100, 132, 40, ADJ_STATUS_OK);
  appendAdjRegisterDataRepeated(&script, 10, 100, 132, 40, ADJ_STATUS_OK);
  appendAdjRegisterDataRepeated(&script, 10, 3000, 132, 40, ADJ_STATUS_OK);
  appendAdjRegisterData(&script, 3000, 132, 40, ADJ_STATUS_OK);
  injectAdjustometerScript(&script);

  VP37InitStatus status = VP37_init(pump);

  TEST_ASSERT_EQUAL_INT(VP37_INIT_CALIBRATION_FAILED, status);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  TEST_ASSERT_FALSE(pump->calibrationDone);
}

void test_vp37_positive_limit_matches_hot_actuator_compensation(void) {
  TEST_ASSERT_FLOAT_WITHIN(
      0.01f, VP37_PID_CORR_LIMIT_POSITIVE_COLD,
      VP37_computePositiveCorrectionLimit(22.0f, ADJ_STATUS_OK, 690.0f));
  TEST_ASSERT_FLOAT_WITHIN(
      0.05f, 323.71f,
      VP37_computePositiveCorrectionLimit(51.0f, ADJ_STATUS_OK, 690.0f));
  TEST_ASSERT_FLOAT_WITHIN(
      0.05f, 338.02f,
      VP37_computePositiveCorrectionLimit(55.0f, ADJ_STATUS_OK, 690.0f));
  TEST_ASSERT_FLOAT_WITHIN(
      0.05f, 310.78f,
      VP37_computePositiveCorrectionLimit(55.0f, ADJ_STATUS_OK, 480.0f));
  TEST_ASSERT_FLOAT_WITHIN(
      0.01f, VP37_PID_CORR_LIMIT_POSITIVE_MAX,
      VP37_computePositiveCorrectionLimit(80.0f, ADJ_STATUS_OK, 690.0f));
}

void test_vp37_positive_limit_falls_back_for_bad_temperature(void) {
  TEST_ASSERT_FLOAT_WITHIN(
      0.01f, VP37_PID_CORR_LIMIT_POSITIVE_COLD,
      VP37_computePositiveCorrectionLimit(55.0f, ADJ_STATUS_FUEL_TEMP_BROKEN,
                                          VP37_PWM_FF_AT_MAX));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, VP37_PID_CORR_LIMIT_POSITIVE_COLD,
                           VP37_computePositiveCorrectionLimit(
                               NAN, ADJ_STATUS_OK, VP37_PWM_FF_AT_MAX));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, VP37_PID_CORR_LIMIT_POSITIVE_COLD,
                           VP37_computePositiveCorrectionLimit(
                               121.0f, ADJ_STATUS_OK, VP37_PWM_FF_AT_MAX));
}

void test_vp37_hot_positive_error_uses_expanded_range(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 1, 0, 0, false);

  VP37_setVP37Throttle(pump, 100.0f);
  injectAdjRegisterData(100, 144, 55, ADJ_STATUS_OK);
  VP37_process(pump);

  TEST_ASSERT_FLOAT_WITHIN(0.05f, 330.354f, pump->pidPositiveLimit);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 330.354f, pump->pidCorrection);
  TEST_ASSERT_TRUE(pump->pidSaturatedHigh);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1150.354f, pump->pwmValue);
}

void test_vp37_hot_negative_error_keeps_original_range(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 1, 0, 0, false);

  VP37_setVP37Throttle(pump, 100.0f);
  injectAdjRegisterData(14100, 144, 55, ADJ_STATUS_OK);
  VP37_process(pump);

  TEST_ASSERT_FLOAT_WITHIN(0.05f, 330.354f, pump->pidPositiveLimit);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -220.0f, pump->pidCorrection);
  TEST_ASSERT_FALSE(pump->pidSaturatedHigh);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 600.0f, pump->pwmValue);
}

void test_vp37_pwm_limit_matches_physical_resolution(void) {
  TEST_ASSERT_EQUAL_INT32(PWM_RESOLUTION, VP37_PWM_MAX);
}

void test_vp37_process_disables_after_adj_comm_cutoff_timeout(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  setupPumpForProcessTests(pump);

  injectAdjRegisterData(4000, 144, 50, ADJ_STATUS_OK);
  hal_mock_set_millis(95);
  VP37_process(pump);
  hal_mock_i2c_set_busy(true);

  hal_mock_set_millis(100);
  VP37_process(pump); // comm error #1 (commOk still true)
  hal_mock_set_millis(105);
  VP37_process(pump); // comm error #2 (commOk still true)
  hal_mock_set_millis(110);
  VP37_process(pump); // comm error #3 (commOk false, timestamp starts)
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_UINT32(110, pump->adjCommLostSince);

  hal_mock_set_millis(110 + VP37_ADJ_COMM_CUTOFF_MS);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
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

  injectAdjRegisterData(321, 137, 44, ADJ_STATUS_OK);
  VP37_process(pump);

  TEST_ASSERT_EQUAL_INT32(321, pump->currentAdjustometerPosition);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 13.7f, getGlobalValue(F_VOLTS));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 44.0f, getGlobalValue(F_FUEL_TEMP));
}

void test_vp37_voltage_filter_rejects_quantized_steady_ripple(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 73.0f);

  injectAdjRegisterData(6670, 145, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 14.5f, pump->compensationVolts);

  float minimum = pump->compensationVolts;
  float maximum = pump->compensationVolts;
  for (uint32_t ms = 5U; ms <= 2000U; ms += 5U) {
    hal_mock_set_millis(ms);
    const uint8_t voltage = (ms % 10U) == 0U ? 143U : 147U;
    injectAdjRegisterData(6670, voltage, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    if (ms >= 1000U) {
      minimum = fminf(minimum, pump->compensationVolts);
      maximum = fmaxf(maximum, pump->compensationVolts);
    }
  }

  TEST_ASSERT_LESS_THAN_FLOAT(.03f, maximum - minimum);
  TEST_ASSERT_FLOAT_WITHIN(.03f, 14.5f, pump->compensationVolts);
}

void test_vp37_voltage_filter_handles_cranking_drop_and_recovery(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50.0f);

  injectAdjRegisterData(4600, 150, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 15.0f, pump->compensationVolts);

  for (uint32_t ms = 5U; ms <= 60U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 80, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.5f, 8.0f, pump->compensationVolts);
  TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(8.0f, pump->compensationVolts);

  hal_mock_set_millis(65U);
  injectAdjRegisterData(4600, 150, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(14.9f, pump->compensationVolts);
  TEST_ASSERT_LESS_OR_EQUAL_FLOAT(15.0f, pump->compensationVolts);
}

void test_vp37_voltage_filter_bypass_and_validation(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        VP37_setVoltageFilterTimeConstant(NULL, .25f));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        VP37_setVoltageFilterTimeConstant(pump, NAN));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        VP37_setVoltageFilterTimeConstant(pump, -.01f));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        VP37_setVoltageFilterTimeConstant(
                            pump, VP37_VOLTAGE_FILTER_MAX_S + .01f));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setVoltageFilterTimeConstant(pump, 0.0f));

  VP37_setVP37Throttle(pump, 50.0f);
  injectAdjRegisterData(4600, 80, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 8.0f, pump->compensationVolts);
  hal_mock_set_millis(5U);
  injectAdjRegisterData(4600, 150, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 15.0f, pump->compensationVolts);
}

void test_vp37_period_skips_duplicate_updates_and_handles_wrap(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50.0f);
  hal_mock_set_micros(UINT32_MAX - 1000U);
  injectAdjRegisterData(4600, 144, 26, ADJ_STATUS_OK);
  VP37_process(pump);
  const uint32_t firstSequence = pump->controlSequence;
  const float firstIntegral = pump->pidTerms.integral;
  for (unsigned i = 0; i < 10; ++i) {
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_UINT32(firstSequence, pump->controlSequence);
  TEST_ASSERT_FLOAT_WITHIN(.001f, firstIntegral, pump->pidTerms.integral);
  hal_mock_set_micros(3999U);
  injectAdjRegisterData(4600, 144, 26, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_UINT32(firstSequence + 1U, pump->controlSequence);
  TEST_ASSERT_EQUAL_UINT32(5000U, pump->controlDtUs);
}

void test_vp37_pwm_floor_is_visible_to_integrator(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 1, .4f, 0, false);
  // The running floor applies to positive demand; zero must release the drive.
  VP37_setVP37Throttle(pump, 1);
  for (uint32_t ms = 5; ms <= 500; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(9100, 144, 26, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(VP37_PWM_MIN, pump->finalPWM);
  TEST_ASSERT_TRUE(pump->pidTerms.saturated_low);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pidTerms.integral);
  TEST_ASSERT_FLOAT_WITHIN(
      .1f,
      VP37_PWM_MIN / (pump->voltageCorrection * pump->temperatureCorrection) -
          pump->pwmFeedForward,
      pump->pidCorrection);
}

void test_vp37_zero_demand_releases_drive_despite_feedback_offset(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 0);
  const int16_t feedback[] = {1000, 100, 0};
  const uint8_t temperatures[] = {29, 49, 120};
  for (size_t i = 0; i < COUNTOF(feedback); ++i) {
    hal_mock_set_millis((uint32_t)(i + 1U) * 5U);
    injectAdjRegisterData(feedback[i], 144, temperatures[i], ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->vp37Initialized);
    TEST_ASSERT_TRUE(pump->quantityAtRest);
    TEST_ASSERT_EQUAL_INT32(pump->VP37_ADJUST_MIN, pump->desiredAdjustometer);
    TEST_ASSERT_EQUAL_INT32(0, pump->finalPWM);
    TEST_ASSERT_EQUAL_INT32(0, pump->lastPWMval);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pwmFeedForward);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pidTerms.output);
  }
}

void test_vp37_zero_demand_finishes_slew_and_resumes_without_stored_pid(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  for (uint32_t ms = 5; ms <= 500; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4000, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pidTerms.integral);
  VP37_setVP37Throttle(pump, 0);
  for (uint32_t ms = 505; ms <= 1000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(1000, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    if (ms == 505) {
      TEST_ASSERT_FALSE(pump->quantityAtRest);
      TEST_ASSERT_GREATER_THAN_INT32(0, pump->finalPWM);
      TEST_ASSERT_INT32_WITHIN(1, 4465, pump->desiredAdjustometer);
    }
  }
  TEST_ASSERT_TRUE(pump->quantityAtRest);
  TEST_ASSERT_EQUAL_INT32(0, pump->finalPWM);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pidTerms.integral);

  // The new measurement must seed D; it must not reuse the pre-release value.
  VP37_setVP37Throttle(pump, 5);
  hal_mock_set_millis(1005);
  injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_FALSE(pump->quantityAtRest);
  TEST_ASSERT_GREATER_THAN_INT32(0, pump->finalPWM);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pidTerms.derivative);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pidTerms.integral);
}

void test_vp37_positive_demand_rounded_to_min_still_regulates(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, .001f);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT32(pump->VP37_ADJUST_MIN, pump->desiredAdjustometer);
  TEST_ASSERT_FALSE(pump->quantityAtRest);
  TEST_ASSERT_GREATER_THAN_INT32(0, pump->finalPWM);
}

void test_vp37_feedback_fault_at_rest_still_latches_drive_off(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 0);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->quantityAtRest);
  hal_mock_set_millis(5);
  injectAdjRegisterData(0, 144, 29, ADJ_STATUS_SIGNAL_LOST);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  VP37_setVP37Throttle(pump, 50);
  hal_mock_set_millis(10);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT32(0, pump->finalPWM);
}

void test_vp37_target_ramp_uses_elapsed_time_and_preserves_overshoot(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 0);
  injectAdjRegisterData(100, 144, 26, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setVP37Throttle(pump, 100);
  hal_mock_set_millis(10);
  injectAdjRegisterData(9500, 144, 26, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_INT32_WITHIN(1, 370, pump->desiredAdjustometer);
  TEST_ASSERT_EQUAL_INT32(pump->desiredAdjustometer - 9500, pump->pidErr);
}

void test_vp37_invalid_timing_or_pid_step_disables_output(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
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
  pump->finalPWM = 700;
  hal_pid_controller_set_tf(pump->adjustController, -1.0f);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_INT32(0, pump->finalPWM);
  TEST_ASSERT_EQUAL_INT32(0, pump->lastPWMval);
  const int lastFrame = hal_mock_i2c_get_write_frame_count() - 1;
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame(lastFrame, &latch, 1));
  TEST_ASSERT_EQUAL_UINT8(0, latch & (1U << PCF8574_O_VP37_ENABLE));
}

#ifdef START_TEST_ENABLE_VP37_CYCLIC
void test_vp37_trace_preserves_consecutive_steps_until_drained(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  VP37TraceSample sample = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_startTrace(pump));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, VP37_startTrace(pump));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_readTrace(pump, &sample));
  for (uint32_t i = 1U; i <= VP37_TRACE_SAMPLES + 4U; i++) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData((int16_t)(4000U + i), 144, 26, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FALSE(VP37_traceCapturing());
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, VP37_startTrace(pump));
  for (uint32_t i = 1U; i <= VP37_TRACE_SAMPLES; i++) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(pump, &sample));
    TEST_ASSERT_EQUAL_UINT32(i, sample.sequence);
    TEST_ASSERT_EQUAL_UINT32(i * 5000U, sample.us);
    TEST_ASSERT_EQUAL_UINT32(5000U, sample.dt);
    TEST_ASSERT_EQUAL_INT32(4000U + i, sample.measured);
  }
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readTrace(pump, &sample));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_startTrace(pump));
  hal_mock_set_millis((VP37_TRACE_SAMPLES + 5U) * 5U);
  injectAdjRegisterData(4700, 144, 26, ADJ_STATUS_OK);
  VP37_process(pump);
  pump->vp37Initialized = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(pump, &sample));
  TEST_ASSERT_EQUAL_INT32(4700, sample.measured);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readTrace(pump, &sample));
}

void test_vp37_cyclic_counts_full_cycles_and_restarts_deterministically(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  TEST_ASSERT_TRUE(initTests());

  tickTests();
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->lastThrottle);
  tickTestsHandleSerialLine("C");
  tickTests();
  TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_A, getCurrentVP37CyclicDelayMs());

  for (uint32_t step = 1U; step <= 200U * CYCLIC_FULL_CYCLES; ++step) {
    hal_mock_set_millis(step * CYCLIC_DELAYTIME_A);
    tickTests();
    if (step == 100U) {
      TEST_ASSERT_FLOAT_WITHIN(.001f, 100.0f, pump->lastThrottle);
    }
    if (step == 200U) {
      TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->lastThrottle);
      TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_A,
                               getCurrentVP37CyclicDelayMs());
    }
  }
  TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_B, getCurrentVP37CyclicDelayMs());

  tickTestsHandleSerialLine("C");
  tickTests();
  TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_A, getCurrentVP37CyclicDelayMs());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->lastThrottle);
  hal_mock_set_millis(hal_millis() + CYCLIC_DELAYTIME_A);
  tickTests();
  TEST_ASSERT_FLOAT_WITHIN(.001f, 1.0f, pump->lastThrottle);
}

void test_vp37_cyclic_voltage_filter_command(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  TEST_ASSERT_TRUE(initTests());
  tickTestsHandleSerialLine("V0.5");
  tickTests();
  TEST_ASSERT_FLOAT_WITHIN(.001f, .5f, pump->voltageFilterTimeConstant);
  TEST_ASSERT_FALSE(pump->voltageReady);
}
#endif

void test_vp37_integral_authority_is_independent_of_ki(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, .2f, 0, true);
  VP37_setVP37Throttle(pump, 100);
  for (uint32_t ms = 5U; ms <= 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(7000, 144, 55, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, VP37_PID_TRIM_TOP_PWM,
                           pump->pidTerms.integral);
  VP37_setVP37PID(pump, 0, .4f, 0, false);
  for (uint32_t ms = 3005U; ms <= 3500U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(7000, 144, 55, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, VP37_PID_TRIM_TOP_PWM,
                           pump->pidTerms.integral);
  TEST_ASSERT_LESS_OR_EQUAL_FLOAT(pump->pidPositiveLimit,
                                  pump->pidTerms.integral);
}

void test_vp37_bench_cap_and_stop(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, .2f, 0, true);
  VP37_setVP37Throttle(pump, 100);
  pump->pidIntegralOverride = 25.0f;
  for (uint32_t ms = 5U; ms <= 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(7000, 144, 55, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, 25.0f, pump->pidTerms.integral);
  VP37_stop(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  TEST_ASSERT_FALSE(VP37_isVP37Enabled(pump));
  hal_mock_set_millis(3010);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT32(0, pump->finalPWM);
}

void test_vp37_invalid_feedback_stops_and_bus_failure_freezes_integral(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  setVP37AdjustometerFastFeedback(true);
  VP37_setVP37Throttle(pump, 70);
  adjustometer_feedback_t sample = {};
  sample.number = 1;
  sample.measuredUs = 100000;
  sample.pulseHz = 4000;
  sample.voltage = 144;
  sample.fuelTemp = 50;
  hal_mock_set_micros(100000);
  injectFastAdjustometer(sample);
  VP37_process(pump);
  const float integral = pump->pidTerms.integral;
#ifdef START_TEST_ENABLE_VP37_CYCLIC
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_startTrace(pump));
#endif
  hal_mock_i2c_set_busy(true);
  hal_mock_set_micros(105000);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_FLOAT(integral, pump->pidTerms.integral);
  hal_mock_i2c_set_busy(false);
  sample.number++;
  sample.measuredUs += 10000;
  hal_mock_set_micros(110000);
  injectFastAdjustometer(sample);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_UINT32(10000U, pump->pidDtUs);
  TEST_ASSERT_EQUAL_UINT32(5000U, pump->controlDtUs);
  sample.number++;
  sample.measuredUs += 5000;
  sample.status = ADJ_STATUS_SIGNAL_LOST;
  hal_mock_set_micros(115000);
  injectFastAdjustometer(sample);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_INT32(0, pump->finalPWM);
#ifdef START_TEST_ENABLE_VP37_CYCLIC
  VP37TraceSample trace;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(pump, &trace));
  TEST_ASSERT_EQUAL_UINT32(2U, trace.sequence);
  TEST_ASSERT_FALSE(trace.fresh);
  TEST_ASSERT_NOT_EQUAL(HAL_OK, trace.readStatus);
  TEST_ASSERT_EQUAL_UINT32(0U, trace.pidDtUs);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(pump, &trace));
  TEST_ASSERT_EQUAL_UINT32(3U, trace.sequence);
  TEST_ASSERT_TRUE(trace.fresh);
  TEST_ASSERT_EQUAL_UINT32(10000U, trace.pidDtUs);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(pump, &trace));
  TEST_ASSERT_EQUAL_UINT32(4U, trace.sequence);
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_SIGNAL_LOST, trace.status);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readTrace(pump, &trace));
#endif
}

void test_vp37_temperature_scales_unsaturated_ff_and_pid_together(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, .05f, .2f, 0, false);
  VP37_setVP37Throttle(pump, 50);
  injectAdjRegisterData(4400, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.0001f, .92894f, pump->temperatureCorrection);
  TEST_ASSERT_FALSE(pump->pidSaturatedHigh);
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pidTerms.integral);
  const float nominal = pump->pwmValue;
  const int32_t coldPWM = pump->finalPWM;
  // With identical position/error at the reference temperature, all terms
  // remain in the same domain; only the complete command multiplier changes.
  hal_pid_controller_reset(pump->adjustController);
  pump->temperatureReady = false;
  hal_mock_set_millis(5);
  injectAdjRegisterData(4400, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 1, pump->temperatureCorrection);
  TEST_ASSERT_FLOAT_WITHIN(.001f, nominal, pump->pwmValue);
  TEST_ASSERT_INT32_WITHIN(1, (int32_t)((float)pump->finalPWM * .92894f),
                           coldPWM);
  TEST_ASSERT_GREATER_THAN_INT32(coldPWM, pump->finalPWM);
}

void test_vp37_temperature_filter_holds_invalid_and_blends_bench_switch(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  const float coldFactor = pump->temperatureCorrection;
  for (uint32_t ms = 5; ms <= 10; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4500, 144, ms == 5 ? 121 : 49,
                          ms == 5 ? ADJ_STATUS_OK
                                  : ADJ_STATUS_FUEL_TEMP_BROKEN);
    VP37_process(pump);
    TEST_ASSERT_EQUAL_FLOAT(coldFactor, pump->temperatureCorrection);
  }
  pump->temperatureCompensationWeight = 0;
  hal_mock_set_millis(15);
  injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_GREATER_THAN_FLOAT(coldFactor, pump->temperatureCorrection);
  TEST_ASSERT_LESS_THAN_FLOAT(coldFactor + .001f, pump->temperatureCorrection);
  for (uint32_t ms = 20; ms <= 20000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.0001f, 1, pump->temperatureCorrection);
}

void test_vp37_temperature_bounds_and_physical_ceiling_reject_windup(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 1, .2f, 0, false);
  VP37_setVP37Throttle(pump, 100);
  for (uint32_t ms = 5; ms <= 1000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(1000, 70, 120, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 1.2f, pump->temperatureCorrection);
  TEST_ASSERT_INT32_WITHIN(1, VP37_PWM_MAX, pump->finalPWM);
  TEST_ASSERT_TRUE(pump->pidTerms.saturated_high);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pidTerms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.01f,
                           (float)VP37_PWM_MAX / (pump->voltageCorrection *
                                                  pump->temperatureCorrection) -
                               pump->pwmFeedForward,
                           pump->pidUpperLimit);

  pump->temperatureReady = false;
  hal_mock_set_millis(1005);
  injectAdjRegisterData(1000, 144, 0, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(VP37_TEMPERATURE_FACTOR_MIN,
                                     pump->temperatureCorrection);
  TEST_ASSERT_LESS_THAN_FLOAT(1, pump->temperatureCorrection);
}

void test_vp37_feedforward_is_nonlinear_and_calibration_independent(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  const float demand[] = {2.5f, 7.5f, 37.5f, 62.5f, 95.0f};
  const float expected[] = {597.5f, 622.5f, 747.5f, 812.5f, 828.0f};
  for (unsigned range = 0; range < 2U; ++range) {
    pump->VP37_ADJUST_MIN = range == 0U ? 100 : 400;
    pump->VP37_ADJUST_MAX = range == 0U ? 9100 : 7400;
    for (size_t i = 0; i < COUNTOF(demand); ++i) {
      pump->desiredAdjustometer = -1;
      VP37_setVP37Throttle(pump, demand[i]);
      hal_mock_set_millis(hal_millis() + 5U);
      injectAdjRegisterData((int16_t)pump->desiredAdjustometerTarget, 144, 49,
                            ADJ_STATUS_OK);
      VP37_process(pump);
      TEST_ASSERT_FLOAT_WITHIN(.05f, expected[i], pump->pwmFeedForward);
      TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pidTerms.integral);
    }
  }
}

void test_vp37_climb_floor_does_not_bypass_target_ramp(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  VP37_setVP37Throttle(pump, 0);
  injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setVP37Throttle(pump, 100);
  hal_mock_set_millis(5);
  injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_LESS_THAN_FLOAT(595, pump->pwmValue);
  TEST_ASSERT_LESS_THAN_FLOAT(0, pump->pidNegativeLimit);
  TEST_ASSERT_FALSE(pump->softFloorActive);
}

void test_vp37_integral_rejects_small_noise_but_corrects_small_bias(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  for (uint32_t ms = 5; ms <= 1000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(ms % 10U == 0U ? 4590 : 4610, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pidTerms.integral);
  for (uint32_t ms = 1005; ms <= 2000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4580, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, 1.6f, pump->pidTerms.integral);
}

void test_vp37_integral_limit_tapers_with_ramped_position(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  injectAdjRegisterData(4600, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 120, pump->pidIntegralLimit);
  VP37_setVP37Throttle(pump, 100);
  hal_mock_set_millis(5);
  injectAdjRegisterData(4600, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 120, pump->pidIntegralLimit);
  float previous = pump->pidIntegralLimit;
  for (uint32_t ms = 10; ms <= 400; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->desiredAdjustometer, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(previous, pump->pidIntegralLimit);
    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(45, pump->pidIntegralLimit);
    previous = pump->pidIntegralLimit;
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, 45, pump->pidIntegralLimit);
}

void test_vp37_upward_ramp_keeps_integral_for_holding_error(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 0);
  injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setVP37Throttle(pump, 50);
  for (uint32_t ms = 5; ms <= 100; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pidTerms.integral);
    TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pidTerms.proportional);
  }
  for (uint32_t ms = 105; ms <= 400; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4500, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(pump->desiredAdjustometerTarget,
                          pump->desiredAdjustometer);
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pidTerms.integral);
}

void test_vp37_upper_slew_brakes_ascent_without_delaying_release(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 80);
  injectAdjRegisterData(7300, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setVP37Throttle(pump, 100);
  hal_mock_set_millis(5);
  injectAdjRegisterData(7300, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_INT32_WITHIN(1, 7423, pump->desiredAdjustometer);
  VP37_setVP37Throttle(pump, 0);
  hal_mock_set_millis(10);
  injectAdjRegisterData(7300, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_INT32_WITHIN(1, 7288, pump->desiredAdjustometer);
}

void test_vp37_downward_ramp_does_not_store_reverse_tracking_lag(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 90);
  injectAdjRegisterData(8200, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setVP37Throttle(pump, 5);
  for (uint32_t ms = 5; ms <= 250; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(8200, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pidTerms.integral);
  }
  // Reverse before the off state: the last descent must not bias the next rise.
  VP37_setVP37Throttle(pump, 90);
  hal_mock_set_millis(255);
  injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pidTerms.integral);
}

void test_vp37_upper_feedforward_falls_without_changing_position_target(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  const float demand[] = {90, 92.5f, 95, 100};
  const float expected[] = {835, 831.5f, 828, 820};
  for (size_t i = 0; i < COUNTOF(demand); ++i) {
    pump->desiredAdjustometer = -1;
    VP37_setVP37Throttle(pump, demand[i]);
    hal_mock_set_millis((uint32_t)(i + 1U) * 5U);
    injectAdjRegisterData((int16_t)pump->desiredAdjustometerTarget, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(.01f, expected[i], pump->pwmFeedForward);
  }
  TEST_ASSERT_EQUAL_INT32(pump->VP37_ADJUST_MAX,
                          pump->desiredAdjustometerTarget);
}

void test_vp37_motion_feedforward_brakes_when_upper_ramp_stops(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  VP37_setVP37Throttle(pump, 75);
  injectAdjRegisterData(6850, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedForwardMotion);
  VP37_setVP37Throttle(pump, 95);
  for (uint32_t ms = 5; ms <= 20; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->desiredAdjustometer, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(40, pump->feedForwardMotion);
  TEST_ASSERT_LESS_THAN_FLOAT(50, pump->feedForwardMotion);
  for (uint32_t ms = 25; ms <= 500; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->desiredAdjustometer, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedForwardMotion);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 828, pump->pwmFeedForward);
  TEST_ASSERT_EQUAL_INT32(pump->desiredAdjustometerTarget,
                          pump->desiredAdjustometer);
  VP37_setVP37Throttle(pump, 0);
  for (uint32_t ms = 505; ms <= 1200; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedForwardMotion);
  }
  TEST_ASSERT_TRUE(pump->quantityAtRest);
  TEST_ASSERT_EQUAL_INT32(0, pump->finalPWM);
}

void test_vp37_ramp_unwinds_existing_negative_trim(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, .2f, 0, true);
  VP37_setVP37Throttle(pump, 90);
  for (uint32_t ms = 5; ms <= 500; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(8300, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  const float previous = pump->pidTerms.integral;
  TEST_ASSERT_LESS_THAN_FLOAT(-8, previous);
  VP37_setVP37Throttle(pump, 95);
  hal_mock_set_millis(505);
  injectAdjRegisterData(8100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_GREATER_THAN_FLOAT(previous, pump->pidTerms.integral);
  TEST_ASSERT_LESS_THAN_FLOAT(0, pump->pidTerms.integral);
}

void test_vp37_stationary_target_uses_soft_approach(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 80);
  injectAdjRegisterData(7300, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setVP37Throttle(pump, 100);
  for (uint32_t ms = 5; ms <= 250; ms += 5) {
    hal_mock_set_millis(ms);
    const int32_t previous = pump->desiredAdjustometer;
    injectAdjRegisterData((int16_t)previous, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    const int32_t step = pump->desiredAdjustometer - previous;
    if (ms == 5) {
      TEST_ASSERT_INT32_WITHIN(1, 124, step);
    }
    if (ms == 30) {
      TEST_ASSERT_INT32_WITHIN(1, 56, step);
    }
  }
  TEST_ASSERT_EQUAL_INT32(pump->desiredAdjustometerTarget,
                          pump->desiredAdjustometer);
}

void test_vp37_reinitialization_does_not_exhaust_pwm_channels(void) {
  for (unsigned int i = 0; i <= HAL_PWM_FREQ_MAX_CHANNELS; ++i) {
    pwm_init();
  }
}

void test_vp37_derivative_opposes_rebound_within_one_control_step(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 75);
  int16_t measured = 7300;
  for (uint32_t ms = 0; ms <= 50; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(measured, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    if (ms < 50U) {
      measured -= 90;
    }
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pidTerms.derivative);
  // The measured motion reverses at the same speed. D must stop adding
  // upward drive within the next 5 ms step, even with a retained history.
  hal_mock_set_millis(55);
  injectAdjRegisterData(measured + 90, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_LESS_THAN_FLOAT(0, pump->pidTerms.derivative);
}

void test_vp37_slew_tracks_a_250_percent_per_second_input(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  for (uint32_t ms = 0; ms <= 405; ++ms) {
    hal_mock_set_millis(ms);
    // A separately specified input ramp, sampled by the 5 ms controller.
    const float target = ms < 400U ? (float)(ms / 4U) : 100.0f;
    VP37_setVP37Throttle(pump, target);
    injectAdjRegisterData((int16_t)pump->desiredAdjustometerTarget, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    if (ms % 5U == 0U) {
      TEST_ASSERT_INT32_WITHIN(90, pump->desiredAdjustometerTarget,
                               pump->desiredAdjustometer);
    }
  }
  TEST_ASSERT_EQUAL_INT32(pump->VP37_ADJUST_MAX, pump->desiredAdjustometer);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_vp37_derivative_opposes_rebound_within_one_control_step);
  RUN_TEST(test_vp37_stationary_target_uses_soft_approach);
  RUN_TEST(test_vp37_reinitialization_does_not_exhaust_pwm_channels);
  RUN_TEST(test_vp37_slew_tracks_a_250_percent_per_second_input);
  RUN_TEST(test_vp37_ramp_unwinds_existing_negative_trim);
  RUN_TEST(test_vp37_motion_feedforward_brakes_when_upper_ramp_stops);
  RUN_TEST(test_vp37_downward_ramp_does_not_store_reverse_tracking_lag);
  RUN_TEST(test_vp37_upper_feedforward_falls_without_changing_position_target);
  RUN_TEST(test_vp37_upper_slew_brakes_ascent_without_delaying_release);
  RUN_TEST(test_vp37_upward_ramp_keeps_integral_for_holding_error);
  RUN_TEST(test_vp37_integral_rejects_small_noise_but_corrects_small_bias);
  RUN_TEST(test_vp37_integral_limit_tapers_with_ramped_position);
  RUN_TEST(test_vp37_feedforward_is_nonlinear_and_calibration_independent);
  RUN_TEST(test_vp37_climb_floor_does_not_bypass_target_ramp);
  RUN_TEST(test_vp37_temperature_scales_unsaturated_ff_and_pid_together);
  RUN_TEST(test_vp37_temperature_filter_holds_invalid_and_blends_bench_switch);
  RUN_TEST(test_vp37_temperature_bounds_and_physical_ceiling_reject_windup);
  RUN_TEST(test_vp37_invalid_feedback_stops_and_bus_failure_freezes_integral);

  RUN_TEST(test_vp37_pid_setter_updates_controller_gains);
  RUN_TEST(test_vp37_pid_reset_restores_pwm_tracking_state);
  RUN_TEST(test_vp37_throttle_caps_target_to_configured_range);
  RUN_TEST(test_vp37_throttle_caps_target_to_min_for_negative_input);
  RUN_TEST(test_vp37_pid_time_update_setter);
  RUN_TEST(test_vp37_percentage_error_constant);
  RUN_TEST(test_vp37_init_returns_already_initialized);
  RUN_TEST(test_vp37_init_returns_ok_when_baseline_ready);
  RUN_TEST(test_vp37_init_rejects_insufficient_calibration_travel);
  RUN_TEST(test_vp37_positive_limit_matches_hot_actuator_compensation);
  RUN_TEST(test_vp37_positive_limit_falls_back_for_bad_temperature);
  RUN_TEST(test_vp37_hot_positive_error_uses_expanded_range);
  RUN_TEST(test_vp37_hot_negative_error_keeps_original_range);
  RUN_TEST(test_vp37_pwm_limit_matches_physical_resolution);
  RUN_TEST(test_vp37_process_disables_after_adj_comm_cutoff_timeout);
  RUN_TEST(test_vp37_process_disables_when_rpm_above_max);
  RUN_TEST(test_vp37_process_updates_globals_from_adjustometer_reading);
  RUN_TEST(test_vp37_voltage_filter_rejects_quantized_steady_ripple);
  RUN_TEST(test_vp37_voltage_filter_handles_cranking_drop_and_recovery);
  RUN_TEST(test_vp37_voltage_filter_bypass_and_validation);

  RUN_TEST(test_vp37_period_skips_duplicate_updates_and_handles_wrap);
  RUN_TEST(test_vp37_pwm_floor_is_visible_to_integrator);
  RUN_TEST(test_vp37_zero_demand_releases_drive_despite_feedback_offset);
  RUN_TEST(test_vp37_zero_demand_finishes_slew_and_resumes_without_stored_pid);
  RUN_TEST(test_vp37_positive_demand_rounded_to_min_still_regulates);
  RUN_TEST(test_vp37_feedback_fault_at_rest_still_latches_drive_off);
  RUN_TEST(test_vp37_target_ramp_uses_elapsed_time_and_preserves_overshoot);
  RUN_TEST(test_vp37_invalid_timing_or_pid_step_disables_output);
#ifdef START_TEST_ENABLE_VP37_CYCLIC
  RUN_TEST(test_vp37_trace_preserves_consecutive_steps_until_drained);
  RUN_TEST(test_vp37_cyclic_counts_full_cycles_and_restarts_deterministically);
  RUN_TEST(test_vp37_cyclic_voltage_filter_command);
#endif
  RUN_TEST(test_vp37_integral_authority_is_independent_of_ki);
  RUN_TEST(test_vp37_bench_cap_and_stop);
  return UNITY_END();
}

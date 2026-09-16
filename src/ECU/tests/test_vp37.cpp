#include "dtcManager.h"
#include "ecuContext.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
#include "test_helpers.h"
#include "testable/adjustometer_test_helpers.h"
#include "testable/vp37_testable.h"
#include "unity.h"
#include "vp37.h"
#include <math.h>

// ── Helpers ──────────────────────────────────────────────────────────────────

static void injectLocalSupplyVoltage(float volts) {
  const float ratio =
      ((float)V_DIVIDER_R1 + (float)V_DIVIDER_R2) / (float)V_DIVIDER_R2;
  int adc = (int)((volts / ratio) * (4095.0f / 3.3f) + 0.5f);
  adc = hal_constrain(adc, 0, 4095);
  hal_mock_adc_inject(ADC_VOLT_PIN, adc);
}

static void injectAdjRegisterData(int16_t pulseHz, uint8_t voltage,
                                  uint8_t fuelTemp, uint8_t status) {
  uint8_t buf[5];
  buf[0] = (uint8_t)((uint16_t)pulseHz >> 8);
  buf[1] = (uint8_t)((uint16_t)pulseHz & 0xFF);
  buf[2] = voltage;
  buf[3] = fuelTemp;
  buf[4] = status;
  hal_mock_i2c_inject_rx(buf, 5);
  injectLocalSupplyVoltage((float)voltage * 0.1f);
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
  pump->pid.controller = hal_pid_controller_create();
  pump->vp37Initialized = true;
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 9100;
  pump->feedback.adjustMiddle =
      (pump->feedback.adjustMax + pump->feedback.adjustMin) / 2;
  pump->demand.target = -1;
  pump->demand.desired = -1;
  pump->demand.lastThrottle = -1.0f;
  pump->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  pump->pid.integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
  pump->feedforward.motionBoostUp = VP37_PWM_FF_MOTION_BOOST;
  pump->feedforward.motionBoostDown = VP37_PWM_FF_DESCENT_BOOST;
  pump->thermal.temperatureCorrection = 1.0f;
  pump->thermal.temperatureCompensationWeight = 1.0f;
  pump->supply.heldVolts = NOMINAL_VOLTAGE;
  pump->supply.ready = false;
  pump->demand.throttleRampLastMs = hal_millis();
  pump->feedback.lastStatus = ADJ_STATUS_OK;
  VP37_setVP37PID(pump, VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, false);
  hal_pid_controller_set_tf(pump->pid.controller, VP37_PID_TF);
  hal_pid_controller_set_max_integral(pump->pid.controller,
                                      VP37_PID_MAX_INTEGRAL);
  setGlobalValue(F_RPM, 1000.0f);
  setGlobalValue(F_VOLTS, 14.0f);
  injectLocalSupplyVoltage(14.0f);
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
  if (pump->pid.controller != NULL) {
    hal_pid_controller_destroy(pump->pid.controller);
    pump->pid.controller = NULL;
  }
}

// ── VP37 PID API tests ───────────────────────────────────────────────────────

void test_vp37_pid_setter_updates_controller_gains(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->pid.controller = hal_pid_controller_create();

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
  pump->pid.controller = hal_pid_controller_create();
  pump->output.lastPWMval = 777;
  pump->output.finalPWM = 888;
  pump->pid.integralHold = true;
  pump->pid.integralHoldReleasePending = true;
  pump->pid.integralHoldReleaseStartedMs = 123U;

  VP37_setVP37PID(pump, 0.20f, 0.10f, 0.05f, true);

  TEST_ASSERT_EQUAL_INT32(-1, pump->output.lastPWMval);
  TEST_ASSERT_EQUAL_INT32(VP37_PWM_MIN, pump->output.finalPWM);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  TEST_ASSERT_FALSE(pump->pid.integralHoldReleasePending);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->pid.integralHoldReleaseStartedMs);
}

void test_vp37_throttle_caps_target_to_configured_range(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 9100;

  VP37_setVP37Throttle(pump, 100.0f);

  int32_t expectedTarget = (int32_t)hal_math_map_f32(
      (float)VP37_ACCELERATION_MAX, VP37_PERCENT_MIN, VP37_PERCENT_MAX,
      (float)pump->feedback.adjustMin, (float)pump->feedback.adjustMax);
  TEST_ASSERT_EQUAL_INT32(expectedTarget, pump->demand.target);
}

void test_vp37_throttle_caps_target_to_min_for_negative_input(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 200;
  pump->feedback.adjustMax = 9200;

  VP37_setVP37Throttle(pump, -10.0f);

  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.target);
}

void test_vp37_potentiometer_rejects_short_single_percent_steps(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);

  VP37_setPotentiometerThrottle(pump, 73);
  const int32_t stableTarget = pump->demand.target;

  hal_mock_set_millis(5U);
  VP37_setPotentiometerThrottle(pump, 74);
  TEST_ASSERT_EQUAL_INT32(stableTarget, pump->demand.target);

  hal_mock_set_millis(100U);
  VP37_setPotentiometerThrottle(pump, 73);
  hal_mock_set_millis(105U);
  VP37_setPotentiometerThrottle(pump, 72);
  hal_mock_set_millis(200U);
  VP37_setPotentiometerThrottle(pump, 73);
  TEST_ASSERT_EQUAL_INT32(stableTarget, pump->demand.target);
}

void test_vp37_potentiometer_confirms_step_or_accepts_larger_change(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);

  VP37_setPotentiometerThrottle(pump, 73);
  const int32_t target73 = pump->demand.target;
  hal_mock_set_millis(10U);
  VP37_setPotentiometerThrottle(pump, 74);
  hal_mock_set_millis(159U);
  VP37_setPotentiometerThrottle(pump, 74);
  TEST_ASSERT_EQUAL_INT32(target73, pump->demand.target);
  hal_mock_set_millis(160U);
  VP37_setPotentiometerThrottle(pump, 74);
  TEST_ASSERT_GREATER_THAN_INT32(target73, pump->demand.target);

  const int32_t target74 = pump->demand.target;
  hal_mock_set_millis(165U);
  VP37_setPotentiometerThrottle(pump, 76);
  TEST_ASSERT_GREATER_THAN_INT32(target74, pump->demand.target);
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
  // The full-period supply mean scales the command by default.
  TEST_ASSERT_TRUE(pump->supply.cycleEnabled);
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_TRUE(pump->feedback.calibrationDone);
  TEST_ASSERT_EQUAL_INT32(100, pump->feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT32(8200, pump->feedback.adjustMax);
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
  TEST_ASSERT_FALSE(pump->feedback.calibrationDone);
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

  // 911.52 + 220 scaled by the 55 C copper factor exceeds the ceiling.
  TEST_ASSERT_FLOAT_WITHIN(0.05f, VP37_PID_CORR_LIMIT_POSITIVE_MAX,
                           pump->pid.positiveLimit);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, VP37_PID_CORR_LIMIT_POSITIVE_MAX,
                           pump->pid.correction);
  TEST_ASSERT_TRUE(pump->pid.saturatedHigh);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1251.52f, pump->output.pwmValue);
}

void test_vp37_hot_negative_error_keeps_original_range(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 1, 0, 0, false);

  VP37_setVP37Throttle(pump, 100.0f);
  injectAdjRegisterData(14100, 144, 55, ADJ_STATUS_OK);
  VP37_process(pump);

  TEST_ASSERT_FLOAT_WITHIN(0.05f, VP37_PID_CORR_LIMIT_POSITIVE_MAX,
                           pump->pid.positiveLimit);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -220.0f, pump->pid.correction);
  TEST_ASSERT_FALSE(pump->pid.saturatedHigh);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 691.52f, pump->output.pwmValue);
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
  TEST_ASSERT_EQUAL_UINT32(110, pump->feedback.commLostSince);

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

  TEST_ASSERT_EQUAL_INT32(321, pump->feedback.position);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 13.7f, getGlobalValue(F_VOLTS));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 44.0f, getGlobalValue(F_FUEL_TEMP));
}

static void runSupplyCycles(VP37Pump *pump, uint32_t &ms, uint32_t count,
                            int16_t position, uint8_t adjVoltageTenths,
                            float localVolts) {
  for (uint32_t i = 0U; i < count; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(position, adjVoltageTenths, 49U, ADJ_STATUS_OK);
    if (localVolts > 0.0f) {
      injectLocalSupplyVoltage(localVolts);
    }
    VP37_process(pump);
  }
}

void test_vp37_voltage_scale_ignores_quantized_adjustometer_ripple(void) {
  // The Adjustometer's tenth-of-a-volt reading only sets the rest-learned
  // scale; a toggling value must not reach the command while driving.
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 73.0f);

  injectAdjRegisterData(6670, 145, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 14.5f, pump->supply.heldVolts);

  float minimum = pump->supply.heldVolts;
  float maximum = pump->supply.heldVolts;
  for (uint32_t ms = 5U; ms <= 2000U; ms += 5U) {
    hal_mock_set_millis(ms);
    const uint8_t voltage = (ms % 10U) == 0U ? 143U : 147U;
    injectAdjRegisterData(6670, voltage, 49, ADJ_STATUS_OK);
    injectLocalSupplyVoltage(14.5f);
    VP37_process(pump);
    if (ms >= 1000U) {
      minimum = fminf(minimum, pump->supply.heldVolts);
      maximum = fmaxf(maximum, pump->supply.heldVolts);
    }
  }

  TEST_ASSERT_LESS_THAN_FLOAT(.03f, maximum - minimum);
  TEST_ASSERT_FLOAT_WITHIN(.03f, 14.5f, pump->supply.heldVolts);
}

void test_vp37_voltage_filter_rejects_local_snapshot_ripple(void) {
  // The local fallback is a 40 us snapshot that lands in either PWM phase.
  // The short filter keeps that alternation out of the command without any
  // dead band: a 0.44 V square wave leaves about 0.03 V on the held value.
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 72.0f);

  injectAdjRegisterData(5869, 145U, 49U, ADJ_STATUS_OK);
  injectLocalSupplyVoltage(14.5f);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 14.5f, pump->supply.heldVolts);

  float minimum = pump->supply.heldVolts;
  float maximum = pump->supply.heldVolts;
  for (uint32_t ms = 5U; ms <= 2000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(5869, 145U, 49U, ADJ_STATUS_OK);
    injectLocalSupplyVoltage((ms % 10U) == 0U ? 14.28f : 14.72f);
    VP37_process(pump);
    minimum = fminf(minimum, pump->supply.heldVolts);
    maximum = fmaxf(maximum, pump->supply.heldVolts);
  }

  TEST_ASSERT_LESS_THAN_FLOAT(.05f, maximum - minimum);
  TEST_ASSERT_FLOAT_WITHIN(.03f, 14.5f, pump->supply.heldVolts);
}

void test_vp37_voltage_scale_follows_cranking_drop_and_recovery(void) {
  // A 15 -> 8 V drop reaches the command through the filter within a quarter
  // of a second, monotonically, and the exact 1/V gain holds on every step.
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50.0f);

  injectAdjRegisterData(4600, 150, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 15.0f, pump->supply.heldVolts);
  TEST_ASSERT_FLOAT_WITHIN(.001f, .8f, pump->supply.correction);
  const int32_t pwmBeforeDrop = pump->output.finalPWM;

  uint32_t ms = 0U;
  float previous = pump->supply.heldVolts;
  for (uint32_t i = 0U; i < 50U; i++) {
    runSupplyCycles(pump, ms, 1U, 4600, 80U, 0.0f);
    TEST_ASSERT_TRUE(pump->supply.heldVolts <= previous);
    TEST_ASSERT_FLOAT_WITHIN(.001f, NOMINAL_VOLTAGE / pump->supply.heldVolts,
                             pump->supply.correction);
    previous = pump->supply.heldVolts;
  }
  TEST_ASSERT_FLOAT_WITHIN(.15f, 8.0f, pump->supply.heldVolts);
  TEST_ASSERT_FLOAT_WITHIN(.03f, 1.5f, pump->supply.correction);
  TEST_ASSERT_GREATER_THAN_INT32(pwmBeforeDrop, pump->output.finalPWM);
  const int32_t pwmDuringDrop = pump->output.finalPWM;

  runSupplyCycles(pump, ms, 50U, 4600, 150U, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(.15f, 15.0f, pump->supply.heldVolts);
  TEST_ASSERT_FLOAT_WITHIN(.01f, .8f, pump->supply.correction);
  TEST_ASSERT_LESS_THAN_INT32(pwmDuringDrop, pump->output.finalPWM);
}

void test_vp37_voltage_correction_tracks_exact_gain_during_drop(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50.0f);

  injectAdjRegisterData(4600, 150U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  for (uint32_t ms = 5U; ms <= 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 80U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(.001f, NOMINAL_VOLTAGE / pump->supply.heldVolts,
                             pump->supply.correction);
  }

  TEST_ASSERT_FLOAT_WITHIN(.03f, 8.0f, pump->supply.heldVolts);
  TEST_ASSERT_FLOAT_WITHIN(.001f, NOMINAL_VOLTAGE / pump->supply.heldVolts,
                           pump->supply.correction);
}

void test_vp37_voltage_filter_settles_at_center_of_local_ripple(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 72.0f);

  injectAdjRegisterData(5869, 135U, 49U, ADJ_STATUS_OK);
  injectLocalSupplyVoltage(13.5f);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 13.5f, pump->supply.heldVolts);

  for (uint32_t ms = 5U; ms <= 2200U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(5869, 145U, 49U, ADJ_STATUS_OK);
    injectLocalSupplyVoltage((ms % 10U) == 0U ? 14.28f : 14.72f);
    VP37_process(pump);
  }

  TEST_ASSERT_FLOAT_WITHIN(.03f, 14.5f, pump->supply.heldVolts);
}

void test_vp37_voltage_compensation_uses_fast_local_adc_and_fallback(void) {
  // The input follows the local ADC at once; the held value follows it
  // through the filter. An invalid local reading hands over to the
  // Adjustometer value without a dead band in the way.
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50.0f);

  injectAdjRegisterData(4600, 150U, 49U, ADJ_STATUS_OK);
  injectLocalSupplyVoltage(15.0f);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.05f, 15.0f, pump->supply.inputVolts);

  uint32_t ms = 0U;
  runSupplyCycles(pump, ms, 1U, 4600, 141U, 8.0f);
  TEST_ASSERT_FLOAT_WITHIN(.05f, 14.1f, pump->supply.lastVolts);
  TEST_ASSERT_FLOAT_WITHIN(.15f, 8.0f, pump->supply.inputVolts);
  TEST_ASSERT_LESS_THAN_FLOAT(15.0f, pump->supply.heldVolts);
  runSupplyCycles(pump, ms, 49U, 4600, 141U, 8.0f);
  TEST_ASSERT_FLOAT_WITHIN(.15f, 8.0f, pump->supply.heldVolts);

  runSupplyCycles(pump, ms, 1U, 4600, 141U, 15.0f);
  TEST_ASSERT_FLOAT_WITHIN(.15f, 15.0f, pump->supply.inputVolts);
  runSupplyCycles(pump, ms, 49U, 4600, 141U, 15.0f);
  TEST_ASSERT_FLOAT_WITHIN(.15f, 15.0f, pump->supply.heldVolts);

  runSupplyCycles(pump, ms, 1U, 4600, 120U, 0.0f);
  injectLocalSupplyVoltage(0.0f);
  ms += 5U;
  hal_mock_set_millis(ms);
  injectAdjRegisterData(4600, 120U, 49U, ADJ_STATUS_OK);
  injectLocalSupplyVoltage(0.0f);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->supply.localReady);
  TEST_ASSERT_FLOAT_WITHIN(.05f, 12.0f, pump->supply.inputVolts);
  for (uint32_t i = 0U; i < 50U; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 120U, 49U, ADJ_STATUS_OK);
    injectLocalSupplyVoltage(0.0f);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.05f, 12.0f, pump->supply.heldVolts);
}

void test_vp37_cycle_voltage_selection_rejects_stale_invalid_and_rest_samples(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.observationEnabled = true;
  pump->supply.cycleEnabled = true;
  pump->supply.cycleValid = true;
  pump->supply.cycleVolts = 14.2f;
  pump->supply.cycleUs = 0U;
  pump->supply.localReady = true;
  pump->supply.localScale = 1.0f;
  VP37_setVP37Throttle(pump, 50);
  const uint32_t times[] = {0U, 95000U, 100000U};
  for (uint32_t now : times) {
    hal_mock_set_micros(now);
    injectAdjRegisterData(4500, 147, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_EQUAL(now < 100000U, pump->supply.cycleUsed);
    // The raw local fixture includes RP2040 ADC compensation; scale is 1.
    TEST_ASSERT_FLOAT_WITHIN(.02f, now < 100000U ? 14.2f : 14.8055f,
                             pump->supply.inputVolts);
  }
  pump->supply.cycleUs = 105000U;
  pump->supply.cycleVolts = NAN;
  hal_mock_set_micros(105000U);
  injectAdjRegisterData(4500, 147, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->supply.cycleUsed);
  pump->supply.cycleVolts = 14.2f;
  pump->thermal.observationEnabled = false;
  hal_mock_set_micros(110000U);
  injectAdjRegisterData(4500, 147, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->supply.cycleUsed);
  pump->thermal.observationEnabled = true;
  VP37_setVP37Throttle(pump, 0);
  pump->demand.desiredPosition = (float)pump->feedback.adjustMin;
  pump->demand.desired = pump->feedback.adjustMin;
  hal_mock_set_micros(115000U);
  injectAdjRegisterData(100, 147, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->demand.atRest);
  TEST_ASSERT_FALSE(pump->supply.cycleUsed);
}

void test_vp37_voltage_compensation_uses_safe_dual_fault_fallback(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50.0f);

  injectAdjRegisterData(4600, 80U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 1.5f, pump->supply.correction);

  // Both sources invalid: the input jumps to the highest expected supply at
  // once, and the scale settles there so the fallback cannot raise the drive.
  uint32_t ms = 0U;
  for (uint32_t i = 0U; i < 60U; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 80U, 49U, ADJ_STATUS_VOLTAGE_BAD);
    injectLocalSupplyVoltage(0.0f);
    VP37_process(pump);
    TEST_ASSERT_FALSE(pump->supply.localReady);
    TEST_ASSERT_FLOAT_WITHIN(.001f, VP37_MAX_EXPECTED_SUPPLY_VOLTAGE,
                             pump->supply.inputVolts);
    TEST_ASSERT_TRUE(pump->supply.correction <= 1.5f);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, .8f, pump->supply.correction);
}

void test_vp37_over_range_supply_keeps_reducing_the_command(void) {
  // Above 17 V the Adjustometer flags its reading bad and the local divider
  // saturates near 18.8 V. The saturated reading is a lower bound of the
  // rail, so it must keep scaling the command down instead of the 15 V
  // dual-fault value, which would raise the drive by a quarter.
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50.0f);

  injectAdjRegisterData(4600, 145U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 14.5f, pump->supply.heldVolts);
  const float scale = pump->supply.localScale;

  uint32_t ms = 0U;
  for (uint32_t i = 0U; i < 80U; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 185U, 49U, ADJ_STATUS_VOLTAGE_BAD);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->supply.overRange);
    TEST_ASSERT_TRUE(pump->supply.localReady);
    // The mock ADC reads the injected 18.5 V through the RP2040 compensation.
    TEST_ASSERT_FLOAT_WITHIN(.05f, pump->supply.localVolts * scale,
                             pump->supply.inputVolts);
    TEST_ASSERT_FLOAT_WITHIN(.001f, scale, pump->supply.localScale);
  }
  TEST_ASSERT_FLOAT_WITHIN(.25f, 18.5f, pump->supply.heldVolts);
  TEST_ASSERT_LESS_THAN_FLOAT(NOMINAL_VOLTAGE / 17.0f, pump->supply.correction);

  // Back inside the range the ordinary path resumes without a re-seed.
  runSupplyCycles(pump, ms, 40U, 4600, 145U, 14.5f);
  TEST_ASSERT_FALSE(pump->supply.overRange);
  TEST_ASSERT_FLOAT_WITHIN(.1f, 14.5f, pump->supply.heldVolts);
}

void test_vp37_voltage_scale_follows_gradual_supply_changes_closely(void) {
  // A 2 V/s ramp through the 50 ms filter lags by about 0.1 V; the old dead
  // band left up to 0.5 V to the integrator for seconds.
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50.0f);

  uint32_t ms = 0U;
  injectAdjRegisterData(4600, 150U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 15.0f, pump->supply.heldVolts);

  for (uint8_t voltage = 149U; voltage >= 80U; --voltage) {
    runSupplyCycles(pump, ms, 10U, 4600, voltage, 0.0f);
    const float trueVolts = (float)voltage * .1f;
    TEST_ASSERT_FLOAT_WITHIN(.15f, trueVolts, pump->supply.heldVolts);
    TEST_ASSERT_FLOAT_WITHIN(.001f, NOMINAL_VOLTAGE / pump->supply.heldVolts,
                             pump->supply.correction);
  }

  for (uint8_t voltage = 81U; voltage <= 150U; ++voltage) {
    runSupplyCycles(pump, ms, 10U, 4600, voltage, 0.0f);
    const float trueVolts = (float)voltage * .1f;
    TEST_ASSERT_FLOAT_WITHIN(.15f, trueVolts, pump->supply.heldVolts);
    TEST_ASSERT_FLOAT_WITHIN(.001f, NOMINAL_VOLTAGE / pump->supply.heldVolts,
                             pump->supply.correction);
  }
}

void test_vp37_period_skips_duplicate_updates_and_handles_wrap(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50.0f);
  hal_mock_set_micros(UINT32_MAX - 1000U);
  injectAdjRegisterData(4600, 144, 26, ADJ_STATUS_OK);
  VP37_process(pump);
  const uint32_t firstSequence = pump->controlSequence;
  const float firstIntegral = pump->pid.terms.integral;
  for (unsigned i = 0; i < 10; ++i) {
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_UINT32(firstSequence, pump->controlSequence);
  TEST_ASSERT_FLOAT_WITHIN(.001f, firstIntegral, pump->pid.terms.integral);
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
  TEST_ASSERT_EQUAL_INT32(VP37_PWM_MIN, pump->output.finalPWM);
  TEST_ASSERT_TRUE(pump->pid.terms.saturated_low);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(
      .1f,
      VP37_PWM_MIN /
              (pump->supply.correction * pump->thermal.temperatureCorrection) -
          pump->feedforward.pwm,
      pump->pid.correction);
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
    TEST_ASSERT_TRUE(pump->demand.atRest);
    TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.desired);
    TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
    TEST_ASSERT_EQUAL_INT32(0, pump->output.lastPWMval);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedforward.pwm);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.output);
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
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pid.terms.integral);
  VP37_setVP37Throttle(pump, 0);
  for (uint32_t ms = 505; ms <= 1000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(1000, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    if (ms == 505) {
      TEST_ASSERT_FALSE(pump->demand.atRest);
      TEST_ASSERT_GREATER_THAN_INT32(0, pump->output.finalPWM);
      TEST_ASSERT_INT32_WITHIN(1, 4465, pump->demand.desired);
    }
  }
  TEST_ASSERT_TRUE(pump->demand.atRest);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);

  // The new measurement must seed D; it must not reuse the pre-release value.
  VP37_setVP37Throttle(pump, 5);
  hal_mock_set_millis(1005);
  injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_FALSE(pump->demand.atRest);
  TEST_ASSERT_GREATER_THAN_INT32(0, pump->output.finalPWM);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.derivative);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.terms.integral);
}

void test_vp37_positive_demand_rounded_to_min_still_regulates(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, .001f);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.desired);
  TEST_ASSERT_FALSE(pump->demand.atRest);
  TEST_ASSERT_GREATER_THAN_INT32(0, pump->output.finalPWM);
}

void test_vp37_feedback_fault_at_rest_still_latches_drive_off(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 0);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->demand.atRest);
  hal_mock_set_millis(5);
  injectAdjRegisterData(0, 144, 29, ADJ_STATUS_SIGNAL_LOST);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  VP37_setVP37Throttle(pump, 50);
  hal_mock_set_millis(10);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
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
  TEST_ASSERT_INT32_WITHIN(1, 370, pump->demand.desired);
  TEST_ASSERT_EQUAL_INT32(pump->demand.desired - 9500, pump->pid.error);
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
  pump->supply.ready = true;
  pump->supply.heldVolts = 15.0f;
  pump->supply.correction = .8f;
  injectAdjRegisterData(4700, 80, 26, ADJ_STATUS_OK);
  VP37_process(pump);
  const float expectedVoltageCorrection = pump->supply.correction;
  pump->vp37Initialized = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(pump, &sample));
  TEST_ASSERT_EQUAL_INT32(4700, sample.measured);
  TEST_ASSERT_FLOAT_WITHIN(.0001f, expectedVoltageCorrection,
                           sample.voltageCorrection);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readTrace(pump, &sample));
}
#endif

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
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.lastThrottle);
  TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_A, testsCyclicDelayMs());

  for (uint32_t step = 1U; step <= 200U * CYCLIC_FULL_CYCLES; ++step) {
    hal_mock_set_millis(step * CYCLIC_DELAYTIME_A);
    TEST_ASSERT_TRUE(tickTests());
    if (step == 100U) {
      TEST_ASSERT_FLOAT_WITHIN(.001f, 100.0f, pump->demand.lastThrottle);
    }
    if (step == 200U) {
      TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.lastThrottle);
      TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_A, testsCyclicDelayMs());
    }
  }
  TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_B, testsCyclicDelayMs());

  // Restarting the same test rewinds the generator to the first profile.
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_CYCLIC));
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_EQUAL_UINT32(CYCLIC_DELAYTIME_A, testsCyclicDelayMs());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.lastThrottle);
  hal_mock_set_millis(hal_millis() + CYCLIC_DELAYTIME_A);
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 1.0f, pump->demand.lastThrottle);

  // Stopping hands the demand back and commands zero on the way out.
  TEST_ASSERT_EQUAL_INT(HAL_OK, stopTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.lastThrottle);
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
  TEST_ASSERT_FLOAT_WITHIN(.001f, 73.0f, pump->demand.lastThrottle);

  hal_mock_set_millis(60000U);
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 73.0f, pump->demand.lastThrottle);

  tickTestsHandleSerialLine("S25.5");
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 25.5f, pump->demand.lastThrottle);

  // An auto-zero deadline releases the demand without ending the test.
  tickTestsHandleSerialLine("G2000");
  TEST_ASSERT_TRUE(tickTests());
  tickTestsHandleSerialLine("S40");
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 40.0f, pump->demand.lastThrottle);
  hal_mock_set_millis(hal_millis() + 2001U);
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.lastThrottle);
}
#endif

#if ECU_FUNCTIONAL_TESTS_ENABLED
void test_vp37_current_observation_command_preserves_control_state(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  TEST_ASSERT_TRUE(initTests());
  tickTestsHandleSerialLine("S73");
  tickTests();
  tickTests();
  const int32_t pwm = pump->output.finalPWM;
  const float integral = pump->pid.terms.integral;
  const int32_t target = pump->demand.target;
  const char *commands[] = {"Q1", "Q0", "Q0.5", "Q1extra", "Q1"};
  const bool expected[] = {true, false, false, false, true};
  for (size_t i = 0U; i < COUNTOF(commands); i++) {
    tickTestsHandleSerialLine(commands[i]);
    tickTests();
    tickTests();
    TEST_ASSERT_EQUAL(expected[i], pump->thermal.observationEnabled);
    TEST_ASSERT_EQUAL_INT32(pwm, pump->output.finalPWM);
    TEST_ASSERT_EQUAL_INT32(target, pump->demand.target);
    TEST_ASSERT_FLOAT_WITHIN(.001f, integral, pump->pid.terms.integral);
  }
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
  TEST_ASSERT_FLOAT_WITHIN(.01f,
                           VP37_INTEGRAL_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS - 1U]
                                                  [VP37_TAPER_COL_VALUE],
                           pump->pid.terms.integral);
  VP37_setVP37PID(pump, 0, .4f, 0, false);
  for (uint32_t ms = 3005U; ms <= 3500U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(7000, 144, 55, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f,
                           VP37_INTEGRAL_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS - 1U]
                                                  [VP37_TAPER_COL_VALUE],
                           pump->pid.terms.integral);
  TEST_ASSERT_LESS_OR_EQUAL_FLOAT(pump->pid.positiveLimit,
                                  pump->pid.terms.integral);
}

void test_vp37_bench_cap_and_stop(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, .2f, 0, true);
  VP37_setVP37Throttle(pump, 100);
  pump->pid.integralOverride = 25.0f;
  for (uint32_t ms = 5U; ms <= 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(7000, 144, 55, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, 25.0f, pump->pid.terms.integral);
  VP37_stop(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  TEST_ASSERT_FALSE(VP37_isVP37Enabled(pump));
  hal_mock_set_millis(3010);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
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
  const float integral = pump->pid.terms.integral;
#if ECU_FUNCTIONAL_TESTS_ENABLED
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_startTrace(pump));
#endif
  hal_mock_i2c_set_busy(true);
  hal_mock_set_micros(105000);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_FLOAT(integral, pump->pid.terms.integral);
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
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
#if ECU_FUNCTIONAL_TESTS_ENABLED
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
  TEST_ASSERT_FLOAT_WITHIN(.0001f, .92894f,
                           pump->thermal.temperatureCorrection);
  TEST_ASSERT_FALSE(pump->pid.saturatedHigh);
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pid.terms.integral);
  const float nominal = pump->output.pwmValue;
  const int32_t coldPWM = pump->output.finalPWM;
  // With identical position/error at the reference temperature, all terms
  // remain in the same domain; only the complete command multiplier changes.
  hal_pid_controller_reset(pump->pid.controller);
  pump->thermal.temperatureReady = false;
  // The applied multiplier is rate limited, so let it take the new value at
  // once; the ramp itself has its own test.
  pump->thermal.scaleReady = false;
  hal_mock_set_millis(5);
  injectAdjRegisterData(4400, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 1, pump->thermal.temperatureCorrection);
  TEST_ASSERT_FLOAT_WITHIN(.001f, nominal, pump->output.pwmValue);
  TEST_ASSERT_INT32_WITHIN(1, (int32_t)((float)pump->output.finalPWM * .92894f),
                           coldPWM);
  TEST_ASSERT_GREATER_THAN_INT32(coldPWM, pump->output.finalPWM);
}

void test_vp37_temperature_filter_holds_invalid_and_blends_bench_switch(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  const float coldFactor = pump->thermal.temperatureCorrection;
  for (uint32_t ms = 5; ms <= 10; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4500, 144, ms == 5 ? 121 : 49,
                          ms == 5 ? ADJ_STATUS_OK
                                  : ADJ_STATUS_FUEL_TEMP_BROKEN);
    VP37_process(pump);
    TEST_ASSERT_EQUAL_FLOAT(coldFactor, pump->thermal.temperatureCorrection);
  }
  pump->thermal.temperatureCompensationWeight = 0;
  hal_mock_set_millis(15);
  injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_GREATER_THAN_FLOAT(coldFactor,
                                 pump->thermal.temperatureCorrection);
  TEST_ASSERT_LESS_THAN_FLOAT(coldFactor + .001f,
                              pump->thermal.temperatureCorrection);
  for (uint32_t ms = 20; ms <= 20000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.0001f, 1, pump->thermal.temperatureCorrection);
}

void test_vp37_thermal_scale_ramps_the_handover_to_the_measured_path(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  // Without a measured estimate the model owns the command outright.
  TEST_ASSERT_FALSE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_EQUAL_FLOAT(pump->thermal.temperatureCorrection,
                          pump->thermal.scale);
  const float model = pump->thermal.scale;

  // Hand over to a measured value the coil's self-heating has pushed above the
  // model. Before the rate limit this landed on the command in one step.
  pump->thermal.driveCorrection = model + .05f;
  pump->thermal.driveResistanceReady = true;
  pump->thermal.driveCompensationEnabled = true;
  pump->thermal.driveUpdatedMs = hal_millis();
  hal_mock_set_millis(5);
  injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f,
                           model + (VP37_THERMAL_SCALE_SLEW_PER_S * .005f),
                           pump->thermal.scale);

  // The ramp still closes in seconds, two decades faster than the drift it
  // follows.
  for (uint32_t ms = 10; ms <= 5000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_FLOAT_WITHIN(.0005f, pump->thermal.driveCorrection,
                           pump->thermal.scale);
}

void test_vp37_thermal_scale_keeps_its_limit_across_rest(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->demand.atRest);

  VP37_setVP37Throttle(pump, 0);
  for (uint32_t ms = 5; ms <= 4000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->demand.atRest);
  const float before = pump->thermal.scale;

  // A cyclic ramp touches zero demand several times a second. Taking the
  // estimate whole at rest would hand the command every wild value such a ramp
  // produces, so rest earns no exemption from the limit.
  pump->thermal.driveCorrection = before + .05f;
  pump->thermal.driveResistanceReady = true;
  pump->thermal.driveCompensationEnabled = true;
  pump->thermal.driveUpdatedMs = hal_millis();
  hal_mock_set_millis(4005);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->demand.atRest);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f,
                           before + (VP37_THERMAL_SCALE_SLEW_PER_S * .005f),
                           pump->thermal.scale);
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
  TEST_ASSERT_FLOAT_WITHIN(.001f, 1.2f, pump->thermal.temperatureCorrection);
  TEST_ASSERT_INT32_WITHIN(1, VP37_PWM_MAX, pump->output.finalPWM);
  TEST_ASSERT_TRUE(pump->pid.terms.saturated_high);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(
      .01f,
      (float)VP37_PWM_MAX /
              (pump->supply.correction * pump->thermal.temperatureCorrection) -
          pump->feedforward.pwm,
      pump->pid.upperLimit);

  pump->thermal.temperatureReady = false;
  hal_mock_set_millis(1005);
  injectAdjRegisterData(1000, 144, 0, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(VP37_TEMPERATURE_FACTOR_MIN,
                                     pump->thermal.temperatureCorrection);
  TEST_ASSERT_LESS_THAN_FLOAT(1, pump->thermal.temperatureCorrection);
}

void test_vp37_feedforward_is_nonlinear_and_calibration_independent(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  const float demand[] = {2.5f, 7.5f, 37.5f, 62.5f, 95.0f};
  // Holding commands include the measured 0.22-ohm source-shunt adjustment.
  const float expected[] = {621.54f, 641.52f, 779.76f, 861.84f, 911.52f};
  for (unsigned range = 0; range < 2U; ++range) {
    pump->feedback.adjustMin = range == 0U ? 100 : 400;
    pump->feedback.adjustMax = range == 0U ? 9100 : 7400;
    for (size_t i = 0; i < COUNTOF(demand); ++i) {
      pump->demand.desired = -1;
      VP37_setVP37Throttle(pump, demand[i]);
      hal_mock_set_millis(hal_millis() + 5U);
      injectAdjRegisterData((int16_t)pump->demand.target, 144, 49,
                            ADJ_STATUS_OK);
      VP37_process(pump);
      TEST_ASSERT_FLOAT_WITHIN(.05f, expected[i], pump->feedforward.pwm);
      TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
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
  TEST_ASSERT_LESS_THAN_FLOAT(642.6f, pump->output.pwmValue);
  TEST_ASSERT_LESS_THAN_FLOAT(0, pump->pid.negativeLimit);
  TEST_ASSERT_FALSE(pump->pid.softFloorActive);
}

void test_vp37_integral_hold_rejects_bias_and_releases_on_persistent_error(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  for (uint32_t ms = 5; ms <= 1000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(ms % 10U == 0U ? 4590 : 4610, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  for (uint32_t ms = 1005; ms <= 2000; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4580, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
  TEST_ASSERT_TRUE(pump->pid.integralHold);

  for (uint32_t ms = 2005U; ms <= 2495U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4550, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  TEST_ASSERT_TRUE(pump->pid.integralHoldReleasePending);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);

  hal_mock_set_millis(2500U);
  injectAdjRegisterData(4580, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  TEST_ASSERT_FALSE(pump->pid.integralHoldReleasePending);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);

  for (uint32_t ms = 2505U; ms <= 3105U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4550, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pid.terms.integral);

  for (uint32_t ms = 3110U; ms <= 3210U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4585, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_EQUAL(ms >= 3210U, pump->pid.integralHold);
  }
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  const float heldIntegral = pump->pid.terms.integral;
  for (uint32_t ms = 3215U; ms <= 3400U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4585, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, heldIntegral, pump->pid.terms.integral);
}

void test_vp37_integral_hold_requires_continuous_time_near_target(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  for (uint32_t ms = 5U; ms <= 250U; ms += 5U) {
    hal_mock_set_millis(ms);
    // A brief crossing cannot freeze I; leaving the band restarts the dwell.
    const bool crossing = (ms >= 100U) && (ms < 145U);
    injectAdjRegisterData((crossing || (ms >= 150U)) ? 4600 : 4500, 144U, 49U,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_EQUAL(ms >= 250U, pump->pid.integralHold);
  }
}

void test_vp37_supply_change_does_not_freeze_integral(void) {
  // The supply is scaled out of the command before the actuator, so a drop
  // is no reason to stop integrating a real position error.
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);

  for (uint32_t ms = 5U; ms <= 200U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4500, 145U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pid.terms.integral);
  const float integralBeforeDrop = pump->pid.terms.integral;

  for (uint32_t ms = 205U; ms <= 700U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4500, 80U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(integralBeforeDrop, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.15f, 8.0f, pump->supply.heldVolts);
}

void test_vp37_supply_change_alone_leaves_the_integral_hold_engaged(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);

  for (uint32_t ms = 5U; ms <= 200U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 145U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  const float heldIntegral = pump->pid.terms.integral;

  // The rail drops while the position stays on target: the hold keeps its
  // own bands and the scale absorbs the change.
  for (uint32_t ms = 205U; ms <= 700U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 80U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->pid.integralHold);
    TEST_ASSERT_FALSE(pump->pid.integralHoldReleasePending);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, heldIntegral, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.15f, 8.0f, pump->supply.heldVolts);
}

void test_vp37_integral_limit_tapers_with_ramped_position(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 50);
  injectAdjRegisterData(4600, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 120, pump->pid.integralLimit);
  VP37_setVP37Throttle(pump, 100);
  hal_mock_set_millis(5);
  injectAdjRegisterData(4600, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 120, pump->pid.integralLimit);
  float previous = pump->pid.integralLimit;
  for (uint32_t ms = 10; ms <= 400; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(previous, pump->pid.integralLimit);
    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(45, pump->pid.integralLimit);
    previous = pump->pid.integralLimit;
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, 45, pump->pid.integralLimit);
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
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
    TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pid.terms.proportional);
  }
  for (uint32_t ms = 105; ms <= 400; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4500, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pid.terms.integral);
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
  TEST_ASSERT_INT32_WITHIN(1, 7423, pump->demand.desired);
  VP37_setVP37Throttle(pump, 0);
  hal_mock_set_millis(10);
  injectAdjRegisterData(7300, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_INT32_WITHIN(1, 7288, pump->demand.desired);
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
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
  }
  // Reverse before the off state: the last descent must not bias the next rise.
  VP37_setVP37Throttle(pump, 90);
  hal_mock_set_millis(255);
  injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
}

void test_vp37_upper_feedforward_flattens_without_changing_position_target(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  const float demand[] = {90, 92.5f, 95, 100};
  const float expected[] = {905.04f, 908.28f, 911.52f, 911.52f};
  for (size_t i = 0; i < COUNTOF(demand); ++i) {
    pump->demand.desired = -1;
    VP37_setVP37Throttle(pump, demand[i]);
    hal_mock_set_millis((uint32_t)(i + 1U) * 5U);
    injectAdjRegisterData((int16_t)pump->demand.target, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(.01f, expected[i], pump->feedforward.pwm);
  }
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.target);
}

void test_vp37_motion_feedforward_brakes_when_upper_ramp_stops(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  VP37_setVP37Throttle(pump, 75);
  injectAdjRegisterData(6850, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedforward.motion);
  VP37_setVP37Throttle(pump, 95);
  for (uint32_t ms = 5; ms <= 20; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(40, pump->feedforward.motion);
  TEST_ASSERT_LESS_THAN_FLOAT(50, pump->feedforward.motion);
  for (uint32_t ms = 25; ms <= 500; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedforward.motion);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 911.52f, pump->feedforward.pwm);
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  VP37_setVP37Throttle(pump, 0);
  for (uint32_t ms = 505; ms <= 1200; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    // The upward term stays out of a descent; the descent term is off here.
    TEST_ASSERT_TRUE(pump->feedforward.motion <= 0.001f);
  }
  TEST_ASSERT_TRUE(pump->demand.atRest);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
}

void test_vp37_descent_feedforward_lowers_the_command_while_the_target_falls(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  pump->feedforward.motionBoostDown = 40.0f;
  VP37_setVP37Throttle(pump, 90);
  injectAdjRegisterData(8200, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedforward.motion);
  const float holding = pump->feedforward.pwm;
  // A 300 %/s descent is 2.4 reference rates; the target counts as stationary
  // after 25 ms and the rate weight drops, so the term peaks around -77.
  VP37_setVP37Throttle(pump, 10);
  float lowest = 0.0f;
  for (uint32_t ms = 5; ms <= 200; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    lowest = fminf(lowest, pump->feedforward.motion);
  }
  TEST_ASSERT_LESS_THAN_FLOAT(-60.0f, lowest);
  TEST_ASSERT_GREATER_THAN_FLOAT(-100.0f, lowest);
  TEST_ASSERT_TRUE(pump->feedforward.pwm < holding);
  // The ramp ends: the term decays and the holding map is back.
  for (uint32_t ms = 205; ms <= 700; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedforward.motion);
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  // With the term off the descent leaves the map untouched.
  pump->feedforward.motionBoostDown = 0.0f;
  VP37_setVP37Throttle(pump, 5);
  for (uint32_t ms = 705; ms <= 800; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedforward.motion);
  }
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
  const float previous = pump->pid.terms.integral;
  TEST_ASSERT_LESS_THAN_FLOAT(-8, previous);
  VP37_setVP37Throttle(pump, 95);
  hal_mock_set_millis(505);
  injectAdjRegisterData(8100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_GREATER_THAN_FLOAT(previous, pump->pid.terms.integral);
  TEST_ASSERT_LESS_THAN_FLOAT(0, pump->pid.terms.integral);
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
    const int32_t previous = pump->demand.desired;
    injectAdjRegisterData((int16_t)previous, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    const int32_t step = pump->demand.desired - previous;
    if (ms == 5) {
      TEST_ASSERT_INT32_WITHIN(1, 124, step);
    }
    if (ms == 30) {
      TEST_ASSERT_INT32_WITHIN(1, 56, step);
    }
  }
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
}

void test_vp37_reinitialization_does_not_exhaust_pwm_channels(void) {
  for (unsigned int i = 0; i <= HAL_PWM_FREQ_MAX_CHANNELS; ++i) {
    pwm_init();
  }
}

void test_vp37_derivative_opposes_rebound_within_one_control_step(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, VP37_PID_KP, VP37_PID_KI, .001f, false);
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
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pid.terms.derivative);
  // The measured motion reverses at the same speed. D must stop adding
  // upward drive within the next 5 ms step, even with a retained history.
  hal_mock_set_millis(55);
  injectAdjRegisterData(measured + 90, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_LESS_THAN_FLOAT(0, pump->pid.terms.derivative);
}

void test_vp37_default_derivative_ignores_large_feedback_step(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37Throttle(pump, 73);

  injectAdjRegisterData(6100, 144U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  hal_mock_set_millis(5U);
  injectAdjRegisterData(7600, 144U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);

  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.kd);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.terms.derivative);
}

void test_vp37_slew_tracks_a_250_percent_per_second_input(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  for (uint32_t ms = 0; ms <= 405; ++ms) {
    hal_mock_set_millis(ms);
    // A separately specified input ramp, sampled by the 5 ms controller.
    const float target = ms < 400U ? (float)(ms / 4U) : 100.0f;
    VP37_setVP37Throttle(pump, target);
    injectAdjRegisterData((int16_t)pump->demand.target, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    if (ms % 5U == 0U) {
      TEST_ASSERT_INT32_WITHIN(90, pump->demand.target, pump->demand.desired);
    }
  }
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.desired);
}

// ── Measured drive-path compensation ─────────────────────────────────────────

static void feedDriveObservation(VP37Pump *pump, float ohms, float volts) {
  const int32_t drive = pump->output.finalPWM;
  pump->thermal.cycleDrive = drive;
  pump->thermal.cyclePwm = drive;
  pump->thermal.cycleVolts = volts;
  pump->thermal.cycleAmps =
      ((float)drive / (float)PWM_RESOLUTION) * volts / ohms;
  pump->thermal.cycleUs = hal_micros();
  pump->thermal.cycleValid = true;
}

static void runDriveCycles(VP37Pump *pump, uint32_t &ms, uint32_t count,
                           float ohms, bool feed) {
  for (uint32_t i = 0U; i < count; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4400, 144, 29, ADJ_STATUS_OK);
    if (feed) {
      feedDriveObservation(pump, ohms, 14.0f);
    }
    VP37_process(pump);
  }
}

void test_vp37_measured_drive_ignores_a_wild_capture_until_it_settles(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setVP37Throttle(pump, 50);
  uint32_t ms = 0U;

  // A capture taken while the actuator slams through its stroke reconstructs a
  // resistance that is not the coil's. A burst of them may not become the
  // estimate: the filter starts at the reference and only creeps away from it.
  const float wild = VP37_DRIVE_REFERENCE_OHMS * 1.35f;
  runDriveCycles(pump, ms, 40U, wild, true);
  TEST_ASSERT_FLOAT_WITHIN(.05f, VP37_DRIVE_REFERENCE_OHMS,
                           pump->thermal.driveResistance);
  TEST_ASSERT_FALSE(pump->thermal.driveResistanceReady);
  TEST_ASSERT_FALSE(pump->thermal.driveCompensationUsed);

  // Nor may they reach the command before the filter has had its settling
  // time; the fuel-temperature model keeps the actuator until then.
  runDriveCycles(pump, ms, 400U, wild, true);
  TEST_ASSERT_FALSE(pump->thermal.driveResistanceReady);
  TEST_ASSERT_FALSE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_EQUAL_FLOAT(pump->thermal.temperatureCorrection,
                          pump->thermal.scale);
}

void test_vp37_measured_drive_replaces_the_fuel_temperature_multiplier(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setVP37Throttle(pump, 50);
  uint32_t ms = 0U;
  const float cold = VP37_DRIVE_REFERENCE_OHMS;

  // Sample count alone is not enough: the filter starts at the reference and
  // needs time to reach what the path measures.
  runDriveCycles(pump, ms, 40U, cold, true);
  TEST_ASSERT_FALSE(pump->thermal.driveResistanceReady);
  runDriveCycles(pump, ms, 1400U, cold, true);
  TEST_ASSERT_TRUE(pump->thermal.driveResistanceReady);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_FLOAT_WITHIN(.01f, cold, pump->thermal.driveResistance);
  // At the reference resistance the measured multiplier is unity, so it must
  // not reproduce the 29 C fuel-temperature factor.
  TEST_ASSERT_FLOAT_WITHIN(.01f, 1.0f, pump->thermal.driveCorrection);
  TEST_ASSERT_LESS_THAN_FLOAT(.95f, pump->thermal.temperatureCorrection);
  const int32_t referencePWM = pump->output.finalPWM;

  // Self-heating raises the drive-path resistance while the fluid does not
  // move; the command has to follow the measurement.
  const float warm = cold * 1.1f;
  runDriveCycles(pump, ms, 2000U, warm, true);
  TEST_ASSERT_FLOAT_WITHIN(.02f, warm, pump->thermal.driveResistance);
  TEST_ASSERT_FLOAT_WITHIN(.02f, 1.1f, pump->thermal.driveCorrection);
  TEST_ASSERT_GREATER_THAN_INT32(referencePWM, pump->output.finalPWM);
}

void test_vp37_measured_drive_rejects_stale_and_mismatched_captures(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setVP37Throttle(pump, 50);
  uint32_t ms = 0U;
  const float warm = VP37_DRIVE_REFERENCE_OHMS * 1.1f;
  runDriveCycles(pump, ms, 1400U, warm, true);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  const float learned = pump->thermal.driveResistance;
  TEST_ASSERT_GREATER_THAN_FLOAT(1.0f, learned);

  // Rejected captures do not touch the estimate and, while it is fresh, do
  // not hand the command back to the model either: motion rejects most
  // captures and a per-cycle fallback stepped the command by the whole
  // thermal difference.
  ms += 5U;
  hal_mock_set_millis(ms);
  injectAdjRegisterData(4400, 144, 29, ADJ_STATUS_OK);
  feedDriveObservation(pump, warm, 14.0f);
  pump->thermal.cycleUs = hal_micros() - (VP37_DRIVE_MAX_AGE_US * 2U);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_EQUAL_FLOAT(learned, pump->thermal.driveResistance);

  ms += 5U;
  hal_mock_set_millis(ms);
  injectAdjRegisterData(4400, 144, 29, ADJ_STATUS_OK);
  feedDriveObservation(pump, warm, 14.0f);
  pump->thermal.cycleDrive =
      pump->output.finalPWM + (VP37_DRIVE_COMMAND_MATCH_COUNTS * 4);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_EQUAL_FLOAT(learned, pump->thermal.driveResistance);

  ms += 5U;
  hal_mock_set_millis(ms);
  injectAdjRegisterData(4400, 144, 29, ADJ_STATUS_OK);
  feedDriveObservation(pump, warm, 14.0f);
  pump->thermal.cycleValid = false;
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_EQUAL_FLOAT(learned, pump->thermal.driveResistance);

  // Only a long silence hands the command back to the fuel-temperature model,
  // and even then the estimate survives instead of collapsing to zero ohms.
  const uint32_t silent = (VP37_DRIVE_STALE_MS / 5U) + 2U;
  for (uint32_t i = 0U; i < silent; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4400, 144, 29, ADJ_STATUS_OK);
    pump->thermal.cycleValid = false;
    VP37_process(pump);
  }
  TEST_ASSERT_FALSE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_EQUAL_FLOAT(learned, pump->thermal.driveResistance);

  // One accepted capture brings the measured path straight back.
  ms += 5U;
  hal_mock_set_millis(ms);
  injectAdjRegisterData(4400, 144, 29, ADJ_STATUS_OK);
  feedDriveObservation(pump, warm, 14.0f);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
}

// The taper holds both ends flat, returns a flat segment's value exactly and
// interpolates linearly inside a slope; the two stroke tables rely on all
// three, and the dead zone's runtime top rides on the same walk.
void test_vp37_stroke_taper_holds_ends_flat_and_walks_the_knots(void) {
  const float taper[3U * VP37_STROKE_TAPER_COLUMNS] = {0.0f,  12.0f,  75.0f,
                                                       12.0f, 100.0f, 120.0f};
  TEST_ASSERT_EQUAL_FLOAT(12.0f, VP37_strokeTaper(taper, 3U, -5.0f));
  TEST_ASSERT_EQUAL_FLOAT(12.0f, VP37_strokeTaper(taper, 3U, 0.0f));
  TEST_ASSERT_EQUAL_FLOAT(12.0f, VP37_strokeTaper(taper, 3U, 50.0f));
  TEST_ASSERT_EQUAL_FLOAT(12.0f, VP37_strokeTaper(taper, 3U, 75.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 66.0f, VP37_strokeTaper(taper, 3U, 87.5f));
  TEST_ASSERT_EQUAL_FLOAT(120.0f, VP37_strokeTaper(taper, 3U, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(120.0f, VP37_strokeTaper(taper, 3U, 130.0f));
  const float single[VP37_STROKE_TAPER_COLUMNS] = {10.0f, 7.0f};
  TEST_ASSERT_EQUAL_FLOAT(7.0f, VP37_strokeTaper(single, 1U, 3.0f));
  TEST_ASSERT_EQUAL_FLOAT(7.0f, VP37_strokeTaper(single, 1U, 30.0f));
  // The real tables through the same walk.
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_TRIM_PWM,
                          VP37_strokeTaper(&VP37_INTEGRAL_LIMIT_MAP[0U][0U],
                                           VP37_STROKE_TAPER_KNOTS, 40.0f));
  TEST_ASSERT_EQUAL_FLOAT(VP37_INTEGRAL_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS - 1U]
                                                 [VP37_TAPER_COL_VALUE],
                          VP37_strokeTaper(&VP37_INTEGRAL_LIMIT_MAP[0U][0U],
                                           VP37_STROKE_TAPER_KNOTS, 100.0f));
}

void test_vp37_integral_deadband_widens_only_in_the_upper_stroke(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  uint32_t ms = 0U;

  // Below the taper start the loop keeps its full accuracy.
  VP37_setVP37Throttle(pump, 50);
  runDriveCycles(pump, ms, 400U, VP37_DRIVE_REFERENCE_OHMS, false);
  TEST_ASSERT_EQUAL_FLOAT((float)VP37_PID_DEADBAND,
                          pump->pid.integralDeadbandHz);

  // At full stroke the dead zone reaches the configured top.
  VP37_setVP37Throttle(pump, 100);
  runDriveCycles(pump, ms, 400U, VP37_DRIVE_REFERENCE_OHMS, false);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.desired);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, VP37_PID_DEADBAND_TOP_HZ,
                           pump->pid.integralDeadbandHz);

  // An error inside that band must not wind the integral any further.
  const float held = pump->pid.terms.integral;
  for (uint32_t i = 0U; i < 200U; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)(pump->feedback.adjustMax - 120), 144, 29,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_FLOAT(held, pump->pid.terms.integral);

  // Disabling the schedule restores the base dead zone at the same position.
  pump->pid.integralDeadbandTopHz = 0.0f;
  ms += 5U;
  hal_mock_set_millis(ms);
  injectAdjRegisterData((int16_t)(pump->feedback.adjustMax - 120), 144, 29,
                        ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_FLOAT((float)VP37_PID_DEADBAND,
                          pump->pid.integralDeadbandHz);
}

static void holdAt(VP37Pump *pump, uint32_t &ms, uint32_t count,
                   int16_t position) {
  for (uint32_t i = 0U; i < count; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(position, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
}

void test_vp37_integral_hold_bands_stay_fixed_under_the_scheduled_dead_zone(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  uint32_t ms = 0U;
  VP37_setVP37Throttle(pump, 100);
  // Ramp to the top, then sit 100 Hz short of it: inside the widened dead
  // zone, which already stops integration, but outside the fixed 20 Hz hold
  // band. A hold here would only let that error stand until twice the zone.
  holdAt(pump, ms, 400U, (int16_t)(pump->feedback.adjustMax - 100));
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.desired);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, VP37_PID_DEADBAND_TOP_HZ,
                           pump->pid.integralDeadbandHz);
  holdAt(pump, ms, 60U, (int16_t)(pump->feedback.adjustMax - 10));
  TEST_ASSERT_TRUE(pump->pid.integralHold);

  // Below the taper the same 20 Hz band applies: 30 Hz short never engages
  // the hold, 10 Hz short does.
  VP37_setVP37Throttle(pump, 50);
  const int32_t target = pump->demand.target;
  holdAt(pump, ms, 600U, (int16_t)(target - 30));
  TEST_ASSERT_EQUAL_INT32(target, pump->demand.desired);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  holdAt(pump, ms, 60U, (int16_t)(target - 10));
  TEST_ASSERT_TRUE(pump->pid.integralHold);
}

void test_vp37_map_trim_absorbs_the_settled_integral_without_a_bump(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->feedforward.mapTrimEnabled = true;
  pump->pid.integralDeadbandTopHz = 0.0f;
  uint32_t ms = 0U;
  VP37_setVP37Throttle(pump, 50);
  const int32_t target = pump->demand.target;
  // Sit 60 Hz short of the target long enough for a clear integral that the
  // trim bound still accepts.
  holdAt(pump, ms, 400U, (int16_t)(target - 60));
  TEST_ASSERT_EQUAL_INT32(target, pump->demand.desired);
  TEST_ASSERT_GREATER_THAN_FLOAT(5.0f, pump->pid.terms.integral);
  TEST_ASSERT_LESS_THAN_FLOAT(VP37_MAP_TRIM_LIMIT_PWM,
                              pump->pid.terms.integral);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->feedforward.mapTrimTransfers);

  // Arrive: the hold engages after the confirmation time and the integral
  // moves into the map trim in one step.
  const float integral = pump->pid.terms.integral;
  const float feedForwardBefore = pump->feedforward.pwm;
  int32_t commandAtTransfer = 0;
  for (uint32_t i = 0U; i < 60U; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)target, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
    if ((pump->feedforward.mapTrimTransfers == 1U) &&
        (commandAtTransfer == 0)) {
      commandAtTransfer = pump->output.finalPWM;
      TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.terms.integral);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1U, pump->feedforward.mapTrimTransfers);
  TEST_ASSERT_FLOAT_WITHIN(.05f, integral, pump->feedforward.mapTrimApplied);
  TEST_ASSERT_FLOAT_WITHIN(.05f, feedForwardBefore + integral,
                           pump->feedforward.pwm);
  TEST_ASSERT_INT32_WITHIN(1, commandAtTransfer, pump->output.finalPWM);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.terms.integral);

  // Rest clears the controller but not what it learned; the next approach
  // to the same region starts with the residual already in the feedforward.
  VP37_setVP37Throttle(pump, 0);
  holdAt(pump, ms, 1200U, 120);
  TEST_ASSERT_TRUE(pump->demand.atRest);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.terms.integral);
  VP37_setVP37Throttle(pump, 50);
  holdAt(pump, ms, 2U, (int16_t)(target - 60));
  TEST_ASSERT_FALSE(pump->demand.atRest);
  // The trim belongs to the knot of the demanded position, so it comes in
  // once the slewed demand reaches that region.
  holdAt(pump, ms, 120U, (int16_t)(target - 60));
  TEST_ASSERT_EQUAL_INT32(target, pump->demand.desired);
  TEST_ASSERT_FLOAT_WITHIN(.05f, integral, pump->feedforward.mapTrimApplied);

  // The bound refuses a transfer that would run away instead of clipping it.
  pump->feedforward.mapTrim[VP37_MAP_TRIM_KNOTS / 2U] =
      VP37_MAP_TRIM_LIMIT_PWM - 1.0f;
  holdAt(pump, ms, 400U, (int16_t)(target - 60));
  const float held = pump->pid.terms.integral;
  TEST_ASSERT_GREATER_THAN_FLOAT(5.0f, held);
  holdAt(pump, ms, 60U, (int16_t)target);
  TEST_ASSERT_EQUAL_UINT32(1U, pump->feedforward.mapTrimTransfers);
  TEST_ASSERT_FLOAT_WITHIN(.001f, held, pump->pid.terms.integral);

  // Disabling learning drops the trim out of the feedforward at once.
  pump->feedforward.mapTrimEnabled = false;
  holdAt(pump, ms, 2U, (int16_t)target);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->feedforward.mapTrimApplied);
}

void test_vp37_full_period_supply_scales_the_command_without_a_filter(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.observationEnabled = true;
  pump->supply.cycleEnabled = true;
  pump->supply.cycleValid = true;
  pump->supply.cycleVolts = 14.5f;
  pump->supply.cycleUs = 0U;
  pump->supply.localReady = true;
  pump->supply.localScale = 1.0f;
  VP37_setVP37Throttle(pump, 50);
  hal_mock_set_micros(0U);
  injectAdjRegisterData(4500, 145, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->supply.cycleUsed);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 14.5f, pump->supply.heldVolts);

  // A cranking-sized drop in the captured mean reaches the scale in the very
  // next control step; the fallback filter is not in this path.
  pump->supply.cycleVolts = 8.0f;
  pump->supply.cycleUs = 5000U;
  hal_mock_set_micros(5000U);
  injectAdjRegisterData(4500, 145, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->supply.cycleUsed);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 8.0f, pump->supply.heldVolts);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 1.5f, pump->supply.correction);

  // V2 holds the scale where it is while the input keeps moving.
  pump->supply.frozen = true;
  pump->supply.cycleVolts = 14.5f;
  pump->supply.cycleUs = 10000U;
  hal_mock_set_micros(10000U);
  injectAdjRegisterData(4500, 145, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 14.5f, pump->supply.inputVolts);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 8.0f, pump->supply.heldVolts);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_vp37_derivative_opposes_rebound_within_one_control_step);
  RUN_TEST(test_vp37_default_derivative_ignores_large_feedback_step);
  RUN_TEST(test_vp37_stationary_target_uses_soft_approach);
  RUN_TEST(test_vp37_reinitialization_does_not_exhaust_pwm_channels);
  RUN_TEST(test_vp37_slew_tracks_a_250_percent_per_second_input);
  RUN_TEST(test_vp37_ramp_unwinds_existing_negative_trim);
  RUN_TEST(test_vp37_motion_feedforward_brakes_when_upper_ramp_stops);
  RUN_TEST(
      test_vp37_descent_feedforward_lowers_the_command_while_the_target_falls);
  RUN_TEST(test_vp37_downward_ramp_does_not_store_reverse_tracking_lag);
  RUN_TEST(
      test_vp37_upper_feedforward_flattens_without_changing_position_target);
  RUN_TEST(test_vp37_upper_slew_brakes_ascent_without_delaying_release);
  RUN_TEST(test_vp37_upward_ramp_keeps_integral_for_holding_error);
  RUN_TEST(
      test_vp37_integral_hold_rejects_bias_and_releases_on_persistent_error);
  RUN_TEST(test_vp37_integral_hold_requires_continuous_time_near_target);
  RUN_TEST(test_vp37_supply_change_does_not_freeze_integral);
  RUN_TEST(test_vp37_supply_change_alone_leaves_the_integral_hold_engaged);
  RUN_TEST(test_vp37_integral_limit_tapers_with_ramped_position);
  RUN_TEST(test_vp37_feedforward_is_nonlinear_and_calibration_independent);
  RUN_TEST(test_vp37_climb_floor_does_not_bypass_target_ramp);
  RUN_TEST(test_vp37_temperature_scales_unsaturated_ff_and_pid_together);
  RUN_TEST(test_vp37_temperature_filter_holds_invalid_and_blends_bench_switch);
  RUN_TEST(test_vp37_thermal_scale_ramps_the_handover_to_the_measured_path);
  RUN_TEST(test_vp37_thermal_scale_keeps_its_limit_across_rest);
  RUN_TEST(test_vp37_measured_drive_ignores_a_wild_capture_until_it_settles);
  RUN_TEST(test_vp37_temperature_bounds_and_physical_ceiling_reject_windup);
  RUN_TEST(test_vp37_invalid_feedback_stops_and_bus_failure_freezes_integral);

  RUN_TEST(test_vp37_pid_setter_updates_controller_gains);
  RUN_TEST(test_vp37_pid_reset_restores_pwm_tracking_state);
  RUN_TEST(test_vp37_throttle_caps_target_to_configured_range);
  RUN_TEST(test_vp37_throttle_caps_target_to_min_for_negative_input);
  RUN_TEST(test_vp37_potentiometer_rejects_short_single_percent_steps);
  RUN_TEST(test_vp37_potentiometer_confirms_step_or_accepts_larger_change);
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
  RUN_TEST(test_vp37_voltage_scale_ignores_quantized_adjustometer_ripple);
  RUN_TEST(test_vp37_voltage_filter_rejects_local_snapshot_ripple);
  RUN_TEST(test_vp37_voltage_scale_follows_cranking_drop_and_recovery);
  RUN_TEST(test_vp37_voltage_correction_tracks_exact_gain_during_drop);
  RUN_TEST(test_vp37_voltage_filter_settles_at_center_of_local_ripple);
  RUN_TEST(test_vp37_voltage_compensation_uses_fast_local_adc_and_fallback);
  RUN_TEST(
      test_vp37_cycle_voltage_selection_rejects_stale_invalid_and_rest_samples);
  RUN_TEST(test_vp37_voltage_compensation_uses_safe_dual_fault_fallback);
  RUN_TEST(test_vp37_over_range_supply_keeps_reducing_the_command);
  RUN_TEST(test_vp37_voltage_scale_follows_gradual_supply_changes_closely);

  RUN_TEST(test_vp37_period_skips_duplicate_updates_and_handles_wrap);
  RUN_TEST(test_vp37_pwm_floor_is_visible_to_integrator);
  RUN_TEST(test_vp37_zero_demand_releases_drive_despite_feedback_offset);
  RUN_TEST(test_vp37_zero_demand_finishes_slew_and_resumes_without_stored_pid);
  RUN_TEST(test_vp37_positive_demand_rounded_to_min_still_regulates);
  RUN_TEST(test_vp37_feedback_fault_at_rest_still_latches_drive_off);
  RUN_TEST(test_vp37_target_ramp_uses_elapsed_time_and_preserves_overshoot);
  RUN_TEST(test_vp37_invalid_timing_or_pid_step_disables_output);
#if ECU_FUNCTIONAL_TESTS_ENABLED
  RUN_TEST(test_vp37_trace_preserves_consecutive_steps_until_drained);
  RUN_TEST(test_vp37_current_observation_command_preserves_control_state);
#endif
#if ECU_FUNCTIONAL_TESTS_ENABLED
  RUN_TEST(test_vp37_cyclic_counts_full_cycles_and_restarts_deterministically);
  RUN_TEST(test_vp37_serial_demand_remains_until_the_next_command);
#endif
  RUN_TEST(test_vp37_integral_authority_is_independent_of_ki);
  RUN_TEST(test_vp37_bench_cap_and_stop);
  RUN_TEST(test_vp37_measured_drive_replaces_the_fuel_temperature_multiplier);
  RUN_TEST(test_vp37_measured_drive_rejects_stale_and_mismatched_captures);
  RUN_TEST(test_vp37_stroke_taper_holds_ends_flat_and_walks_the_knots);
  RUN_TEST(test_vp37_integral_deadband_widens_only_in_the_upper_stroke);
  RUN_TEST(
      test_vp37_integral_hold_bands_stay_fixed_under_the_scheduled_dead_zone);
  RUN_TEST(test_vp37_map_trim_absorbs_the_settled_integral_without_a_bump);
  RUN_TEST(test_vp37_full_period_supply_scales_the_command_without_a_filter);
  return UNITY_END();
}

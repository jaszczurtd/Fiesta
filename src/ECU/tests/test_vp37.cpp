#include "dtcManager.h"
#include "ecuContext.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
#include "test_helpers.h"
#include "testable/adjustometer_test_helpers.h"
#include "testable/vp37_testable.h"
#include "unity.h"
#include "vp37.h"
#include "vp37_internal.h"
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

/* Holding command the map gives at a demand, interpolated between its knots
 * and carried through the source-shunt adjustment. The feedforward tests check
 * the shape of whichever table this build compiles, not the numbers of one
 * frequency's calibration. */
static float expectedHoldingFF(float percent) {
  for (size_t i = 1U; i < VP37_FF_KNOTS; ++i) {
    const float *lower = VP37_FF_MAP[i - 1U];
    const float *upper = VP37_FF_MAP[i];
    if (percent <= upper[VP37_FF_COL_PERCENT]) {
      const float holding =
          lower[VP37_FF_COL_PWM] +
          (upper[VP37_FF_COL_PWM] - lower[VP37_FF_COL_PWM]) *
              (percent - lower[VP37_FF_COL_PERCENT]) /
              (upper[VP37_FF_COL_PERCENT] - lower[VP37_FF_COL_PERCENT]);
      return holding * VP37_PWM_FF_HARDWARE_GAIN;
    }
  }
  return VP37_PWM_FF_AT_MAX * VP37_PWM_FF_HARDWARE_GAIN;
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
  pump->demand.requestedPercent = -1.0f;
  pump->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  pump->pid.topKd = VP37_PID_TOP_KD;
  pump->feedback.leadWeight = VP37_FEEDBACK_LEAD_WEIGHT;
  pump->pid.integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
  pump->feedforward.motionBoostUp = VP37_PWM_FF_MOTION_BOOST;
  pump->feedforward.motionBoostDown = VP37_PWM_FF_DESCENT_BOOST;
  pump->thermal.temperatureCorrection = 1.0f;
  pump->thermal.temperatureCompensationWeight = 1.0f;
  pump->supply.heldVolts = NOMINAL_VOLTAGE;
  pump->supply.ready = false;
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
  (void)VP37_currentScanStop();
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

void test_vp37_position_demand_reaches_calibrated_maximum(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 9100;

  VP37_setPositionDemandPercentage(pump, 100.0f);

  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.target);
}

void test_vp37_position_demand_clamps_negative_input(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 200;
  pump->feedback.adjustMax = 9200;

  VP37_setPositionDemandPercentage(pump, -10.0f);

  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.target);
}

void test_vp37_position_demand_validates_input_and_calibration(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  TEST_ASSERT_EQUAL(HAL_EINVAL, VP37_setPositionDemandPercentage(NULL, 50.0f));
  pump->feedback.calibrationDone = false;
  TEST_ASSERT_EQUAL(HAL_ESTATE, VP37_setPositionDemandPercentage(pump, 50.0f));
  TEST_ASSERT_EQUAL_INT32(-1, pump->demand.target);
  pump->feedback.calibrationDone = true;

  const float invalid[] = {NAN, INFINITY, -INFINITY};
  for (size_t i = 0U; i < COUNTOF(invalid); ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 95.0f));
    TEST_ASSERT_EQUAL(HAL_EINVAL,
                      VP37_setPositionDemandPercentage(pump, invalid[i]));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->demand.requestedPercent);
    TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.target);
  }
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 120.0f));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.target);
}

void test_vp37_position_demand_preserves_fractional_targets_and_hold_age(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  hal_mock_set_millis(10U);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 73.25f));
  TEST_ASSERT_EQUAL_FLOAT(73.25f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(6692, pump->demand.target);
  const uint32_t changed = pump->demand.targetChangedMs;
  hal_mock_set_millis(100U);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 73.25f));
  TEST_ASSERT_EQUAL_UINT32(changed, pump->demand.targetChangedMs);

  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 73.30f));
  TEST_ASSERT_EQUAL_INT32(6697, pump->demand.target);
  TEST_ASSERT_EQUAL_UINT32(100U, pump->demand.targetChangedMs);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 0.0f));
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.target);
}

void test_vp37_position_demand_in_counts_matches_the_percentage_entry(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  const int32_t min = VP37_getPositionDemandMinValue(pump);
  const int32_t max = VP37_getPositionDemandMaxValue(pump);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, min);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, max);
  TEST_ASSERT_EQUAL_INT32(-1, VP37_getPositionDemandMinValue(NULL));
  TEST_ASSERT_EQUAL_INT32(-1, VP37_getPositionDemandMaxValue(NULL));

  // Counts land on the target exactly and still report a percentage.
  const int32_t middle = min + ((max - min) / 2);
  hal_mock_set_millis(10U);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandValue(pump, middle));
  TEST_ASSERT_EQUAL_INT32(middle, pump->demand.target);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, pump->demand.requestedPercent);
  const uint32_t changed = pump->demand.targetChangedMs;

  // Repeating a position leaves the settle timer alone, as percent does.
  hal_mock_set_millis(100U);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandValue(pump, middle));
  TEST_ASSERT_EQUAL_UINT32(changed, pump->demand.targetChangedMs);

  // Out of range clamps to the calibrated stroke instead of being refused.
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandValue(pump, max + 5000));
  TEST_ASSERT_EQUAL_INT32(max, pump->demand.target);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandValue(pump, min - 5000));
  TEST_ASSERT_EQUAL_INT32(min, pump->demand.target);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->demand.requestedPercent);

  TEST_ASSERT_EQUAL(HAL_EINVAL, VP37_setPositionDemandValue(NULL, middle));
  pump->feedback.calibrationDone = false;
  TEST_ASSERT_EQUAL(HAL_ESTATE, VP37_setPositionDemandValue(pump, middle));
  TEST_ASSERT_EQUAL_INT32(min, pump->demand.target);
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
  TEST_ASSERT_TRUE(pump->currentControl.enabled);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->currentControl.correctionPwm);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->currentControl.count);
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_TRUE(pump->feedback.calibrationDone);
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_TOP_KD, pump->pid.topKd);
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

  VP37_setPositionDemandPercentage(pump, 100.0f);
  injectAdjRegisterData(100, 144, 55, ADJ_STATUS_OK);
  VP37_process(pump);

  // The holding command at full demand plus 220, scaled by the 55 C copper
  // factor, exceeds the ceiling.
  TEST_ASSERT_FLOAT_WITHIN(0.05f, VP37_PID_CORR_LIMIT_POSITIVE_MAX,
                           pump->pid.positiveLimit);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, VP37_PID_CORR_LIMIT_POSITIVE_MAX,
                           pump->pid.correction);
  TEST_ASSERT_TRUE(pump->pid.saturatedHigh);
  TEST_ASSERT_FLOAT_WITHIN(
      0.1f, expectedHoldingFF(100) + VP37_PID_CORR_LIMIT_POSITIVE_MAX,
      pump->output.pwmValue);
}

void test_vp37_hot_negative_error_keeps_original_range(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 1, 0, 0, false);

  VP37_setPositionDemandPercentage(pump, 100.0f);
  injectAdjRegisterData(14100, 144, 55, ADJ_STATUS_OK);
  VP37_process(pump);

  TEST_ASSERT_FLOAT_WITHIN(0.05f, VP37_PID_CORR_LIMIT_POSITIVE_MAX,
                           pump->pid.positiveLimit);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -220.0f, pump->pid.correction);
  TEST_ASSERT_FALSE(pump->pid.saturatedHigh);
  TEST_ASSERT_FLOAT_WITHIN(
      0.1f, expectedHoldingFF(100) - VP37_PID_CORR_LIMIT_NEGATIVE,
      pump->output.pwmValue);
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
  VP37_setPositionDemandPercentage(pump, 73.0f);

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
  VP37_setPositionDemandPercentage(pump, 72.0f);

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
  VP37_setPositionDemandPercentage(pump, 50.0f);

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
  VP37_setPositionDemandPercentage(pump, 50.0f);

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
  VP37_setPositionDemandPercentage(pump, 72.0f);

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
  VP37_setPositionDemandPercentage(pump, 50.0f);

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
  VP37_setPositionDemandPercentage(pump, 50);
  const uint32_t times[] = {0U, 15000U, 20000U};
  for (uint32_t now : times) {
    hal_mock_set_micros(now);
    injectAdjRegisterData(4500, 147, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_EQUAL(now < 20000U, pump->supply.cycleUsed);
    // The raw local fixture includes RP2040 ADC compensation; scale is 1.
    TEST_ASSERT_FLOAT_WITHIN(.02f, now < 20000U ? 14.2f : 14.8055f,
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
  VP37_setPositionDemandPercentage(pump, 0);
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
  VP37_setPositionDemandPercentage(pump, 50.0f);

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
  VP37_setPositionDemandPercentage(pump, 50.0f);

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
  VP37_setPositionDemandPercentage(pump, 50.0f);

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

// Exercise acquisition and control on separate clocks. Supply is an external
// waveform; no plant or controller equation generates the position feedback.
struct SupplyScanFixture {
  uint32_t startUs;
  bool steps;
  bool ripple;
  bool clippedCurrent;
};

static uint16_t supplyScanRaw(float volts) {
  const float voltsPerCode = 3.3f *
                             ((float)V_DIVIDER_R1 + (float)V_DIVIDER_R2) /
                             ((float)V_DIVIDER_R2 * 4095.0f);
  const int target = (int)(volts / voltsPerCode + .5f);
  int lower = 0;
  int upper = 4095;
  while (lower < upper) {
    const int middle = lower + (upper - lower) / 2;
    if (hal_adc_compensate_rp2040_12bit(middle) < target) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  return (uint16_t)lower;
}

static float scanRailVolts(const SupplyScanFixture &fixture, uint32_t us) {
  if (fixture.ripple) {
    return 14.0f;
  }
  if (fixture.steps) {
    return ((us >= 120000U) && (us < 390000U)) ? 15.0f : 12.0f;
  }
  const float rise =
      hal_constrain(((float)us - 120000.0f) / 150000.0f, 0.0f, 1.0f);
  const float fall =
      hal_constrain(((float)us - 390000.0f) / 150000.0f, 0.0f, 1.0f);
  return 12.0f + 3.0f * (rise - fall);
}

static void completeSupplyScan(const SupplyScanFixture &fixture,
                               uint32_t completedUs, float currentAmps = 4.0f,
                               int32_t pwm = -1, bool lateOn = false) {
  uint16_t samples[VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_PINS];
  const uint32_t blockUs =
      VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_FRAME_NS / 1000U;
  const uint32_t periodUs = 1000000U / VP37_PWM_FREQUENCY_HZ;
  for (uint32_t i = 0U; i < VP37_CURRENT_SCAN_BLOCK_FRAMES; ++i) {
    const uint32_t sampleUs =
        completedUs - blockUs + i * VP37_CURRENT_SCAN_FRAME_NS / 1000U;
    const uint32_t phase = sampleUs % periodUs;
    const uint32_t onTimeUs = pwm >= 0
                                  ? (periodUs * (uint32_t)pwm) / PWM_RESOLUTION
                                  : (periodUs * 2U) / 5U;
    const bool on = lateOn ? phase >= (periodUs - onTimeUs) : phase < onTimeUs;
    const float ripple =
        fixture.ripple ? ((phase < periodUs / 2U) ? .22f : -.22f) : 0.0f;
    samples[i * VP37_CURRENT_SCAN_PINS] =
        on ? (fixture.clippedCurrent ? 4095U
                                     : VP37_currentAmpsToRaw(currentAmps))
           : 0U;
    samples[i * VP37_CURRENT_SCAN_PINS + 1U] = 1234U;
    samples[i * VP37_CURRENT_SCAN_PINS + 2U] =
        supplyScanRaw(scanRailVolts(fixture, sampleUs) + ripple);
  }
  hal_mock_set_micros(fixture.startUs + completedUs);
  TEST_ASSERT_EQUAL(HAL_OK, hal_mock_adc_scan_complete(
                                samples, VP37_CURRENT_SCAN_BLOCK_FRAMES));
}

static void setupSupplyScan(VP37Pump *pump, uint32_t startUs) {
  setupPumpForProcessTests(pump);
  pump->thermal.observationEnabled = true;
  pump->thermal.temperatureCompensationWeight = 0.0f;
  pump->supply.cycleEnabled = true;
  pump->supply.localScale = 1.0f;
  pump->supply.localReady = true;
  VP37_setVP37PID(pump, 0.0f, 0.0f, 0.0f, false);
  hal_mock_set_micros(startUs);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 50.0f));
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 0);
  VP37_currentSenseInit();
  TEST_ASSERT_EQUAL(HAL_OK, VP37_currentScanStart());
}

struct CurrentScanCommands {
  int32_t baselinePWM;
  float nominalPWM;
  float oldTarget;
  uint32_t offPhaseWriteUs;
  float offPhaseTarget;
  int32_t pendingPWM;
};

static CurrentScanCommands runCurrentScan(float measuredAmps,
                                          bool changeDuringOff = false) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  const SupplyScanFixture fixture = {100000U, false, false, false};
  setupSupplyScan(pump, fixture.startUs);
  pump->currentControl.enabled = true;
  hal_mock_set_micros(fixture.startUs);
  injectAdjRegisterData(4600, 120U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  CurrentScanCommands commands = {};
  commands.baselinePWM = pump->output.finalPWM;
  commands.nominalPWM = pump->output.pwmValue;
  commands.oldTarget = pump->currentControl.targetAmps;
  const uint32_t blockUs =
      VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_FRAME_NS / 1000U;
  uint32_t nextBlockUs = blockUs;
  for (uint32_t us = 5000U; us <= 25000U; us += 5000U) {
    while (nextBlockUs <= us) {
      completeSupplyScan(fixture, nextBlockUs, measuredAmps,
                         commands.baselinePWM, true);
      nextBlockUs += blockUs;
    }
    hal_mock_set_micros(fixture.startUs + us);
    if (changeDuringOff && (us == 10000U)) {
      // Wrap was at 7.69 ms; the MOSFET turns ON later in that period.
      // This request arrives during OFF and applies at the following wrap.
      TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 51.0f));
    }
    injectAdjRegisterData(4600, 120U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    if (us == 10000U) {
      commands.offPhaseWriteUs = hal_micros();
      commands.offPhaseTarget = pump->currentControl.targetAmps;
      commands.pendingPWM = pump->output.finalPWM;
    }
  }
  TEST_ASSERT_TRUE(pump->scan.running);
  TEST_ASSERT_EQUAL(HAL_OK, pump->scan.cycleResultStatus);
  TEST_ASSERT_TRUE(pump->scan.cycleResult.latchValid);
  TEST_ASSERT_TRUE(pump->currentControl.active);
  return commands;
}

static void runCurrentScanCorrection(float measuredAmps, bool expectIncrease) {
  const CurrentScanCommands commands = runCurrentScan(measuredAmps);
  VP37Pump *pump = &getECUContext()->injectionPump;
  TEST_ASSERT_FLOAT_WITHIN(.001f, commands.nominalPWM, pump->output.pwmValue);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.terms.output);
  // Acquisition replaces the initial ADC snapshot with the period mean.
  // Compare both commands at the same voltage/thermal multiplier.
  const int32_t baselinePWM =
      (int32_t)(commands.nominalPWM * pump->supply.correction *
                pump->thermal.scale);
  if (expectIncrease) {
    TEST_ASSERT_GREATER_THAN_INT32(baselinePWM, pump->output.finalPWM);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->currentControl.correctionPwm);
  } else {
    TEST_ASSERT_LESS_THAN_INT32(baselinePWM, pump->output.finalPWM);
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, pump->currentControl.correctionPwm);
  }
  TEST_ASSERT_FLOAT_WITHIN(
      1.0f,
      (commands.nominalPWM + pump->currentControl.correctionPwm) *
          pump->supply.correction * pump->thermal.scale,
      (float)pump->output.finalPWM);
}

void test_vp37_current_scan_increases_drive_for_a_current_deficit(void) {
  runCurrentScanCorrection(2.5f, true);
}

void test_vp37_current_scan_reduces_drive_for_a_current_excess(void) {
  runCurrentScanCorrection(5.5f, false);
}

void test_vp37_current_scan_matches_target_before_active_low_latch(void) {
  const CurrentScanCommands commands = runCurrentScan(3.5f, true);
  VP37Pump *pump = &getECUContext()->injectionPump;
  const VP37CurrentPulseResult &sample = pump->scan.cycleResult;
  TEST_ASSERT_LESS_THAN_UINT32(commands.offPhaseWriteUs, sample.latchUs);
  TEST_ASSERT_GREATER_THAN_UINT32(commands.offPhaseWriteUs,
                                  sample.cycleStartUs);
  TEST_ASSERT_GREATER_THAN_FLOAT(
      .001f, fabsf(commands.offPhaseTarget - commands.oldTarget));
  // Both duties fit the quantization tolerance. Correct timestamp ownership,
  // rather than a gross-duty rejection, must distinguish these two targets.
  TEST_ASSERT_LESS_OR_EQUAL_FLOAT(
      (float)VP37_CURRENT_CONTROL_DUTY_TOLERANCE,
      fabsf((float)(commands.pendingPWM - commands.baselinePWM)));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, commands.oldTarget,
                           pump->currentControl.sampleTargetAmps);
}

static void runSupplyScanProfile(const SupplyScanFixture &fixture) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupSupplyScan(pump, fixture.startUs);
  const uint32_t blockUs =
      VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_FRAME_NS / 1000U;
  uint32_t nextBlockUs = blockUs;
  float referenceDrive = 0.0f;
  float minimumVolts = 100.0f;
  float maximumVolts = 0.0f;
  int32_t minimumPWM = PWM_RESOLUTION;
  int32_t maximumPWM = 0;
  for (uint32_t us = 0U; us <= 660000U; us += 5000U) {
    while (nextBlockUs <= us) {
      completeSupplyScan(fixture, nextBlockUs);
      nextBlockUs += blockUs;
    }
    hal_mock_set_micros(fixture.startUs + us);
    const float volts = scanRailVolts(fixture, us);
    injectAdjRegisterData(4600, (uint8_t)(volts * 10.0f + .5f), 49U,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    if (us < 100000U) {
      continue;
    }
    TEST_ASSERT_TRUE(pump->supply.cycleUsed);
    TEST_ASSERT_FALSE(pump->output.pwmLimited);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.terms.output);
    const float drive = (float)pump->output.finalPWM * volts;
    if (referenceDrive == 0.0f) {
      referenceDrive = drive;
    }
    const bool stepSettled = !fixture.steps || (us < 120000U) ||
                             ((us >= 150000U) && (us < 390000U)) ||
                             (us >= 420000U);
    if (stepSettled) {
      TEST_ASSERT_FLOAT_WITHIN(fixture.steps ? .01f : .02f, 1.0f,
                               drive / referenceDrive);
    }
    minimumVolts = fminf(minimumVolts, pump->supply.heldVolts);
    maximumVolts = fmaxf(maximumVolts, pump->supply.heldVolts);
    minimumPWM =
        minimumPWM < pump->output.finalPWM ? minimumPWM : pump->output.finalPWM;
    maximumPWM =
        maximumPWM > pump->output.finalPWM ? maximumPWM : pump->output.finalPWM;
  }
  if (fixture.ripple) {
    TEST_ASSERT_LESS_THAN_FLOAT(.05f, maximumVolts - minimumVolts);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(3, maximumPWM - minimumPWM);
  }
  if (fixture.clippedCurrent) {
    TEST_ASSERT_FALSE(pump->thermal.cycleValid);
    TEST_ASSERT_TRUE(pump->supply.cycleValid);
  }
}

void test_vp37_scanned_supply_preserves_drive_during_fast_bidirectional_ramps(
    void) {
  runSupplyScanProfile({100000U, false, false, false});
}

void test_vp37_scanned_supply_recovers_both_steps_within_30ms(void) {
  runSupplyScanProfile({100000U, true, false, false});
}

void test_vp37_scanned_supply_rejects_phase_ripple_with_clipped_current(void) {
  runSupplyScanProfile({100000U, false, true, true});
}

void test_vp37_scanned_supply_ramp_handles_microsecond_wrap(void) {
  runSupplyScanProfile({UINT32_MAX - 300000U, false, false, false});
}

void test_vp37_scanned_supply_expires_and_recovers_without_recalibration(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  const SupplyScanFixture fixture = {100000U, false, true, false};
  setupSupplyScan(pump, fixture.startUs);
  const uint32_t blockUs =
      VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_FRAME_NS / 1000U;
  uint32_t nextBlockUs = blockUs;
  for (uint32_t us = 0U; us <= 210000U; us += 5000U) {
    while (nextBlockUs <= us) {
      if ((nextBlockUs <= 120000U) || (nextBlockUs >= 180000U)) {
        completeSupplyScan(fixture, nextBlockUs);
      }
      nextBlockUs += blockUs;
    }
    hal_mock_set_micros(fixture.startUs + us);
    injectAdjRegisterData(4600, 140U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    if (us < 100000U) {
      continue;
    }
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pump->supply.localScale);
    if ((us >= 145000U) && (us < 180000U)) {
      TEST_ASSERT_FALSE(pump->supply.cycleUsed);
    } else if ((us <= 120000U) || (us >= 200000U)) {
      TEST_ASSERT_TRUE(pump->supply.cycleUsed);
    }
  }
  TEST_ASSERT_FLOAT_WITHIN(.03f, 14.0f, pump->supply.heldVolts);
}

static void runLatchedSupplyProfile(const SupplyScanFixture &fixture,
                                    uint32_t pwmPhaseUs = 0U) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupSupplyScan(pump, fixture.startUs);
  const uint32_t blockUs =
      VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_FRAME_NS / 1000U;
  const uint32_t pwmPeriodUs = 1000000U / VP37_PWM_FREQUENCY_HZ;
  uint32_t nextBlockUs = blockUs;
  uint32_t nextControlUs = 0U;
  uint32_t nextWrapUs = pwmPhaseUs;
  uint32_t nextMeasurementUs = 0U;
  uint32_t commandReadyUs = 0U;
  int32_t previousPWM = 0;
  int32_t pendingPWM = 0;
  int32_t latchedPWM = 0;
  float referenceDrive = 0.0f;
  float peakError = 0.0f;
  float steadyErrorSquared = 0.0f;
  uint32_t steadySamples = 0U;
  int32_t minimumPWM = PWM_RESOLUTION;
  int32_t maximumPWM = 0;
  while (nextMeasurementUs <= 660000U) {
    const uint32_t us = hal_min(hal_min(nextBlockUs, nextControlUs),
                                hal_min(nextWrapUs, nextMeasurementUs));
    // Hardware wrap precedes any software write at that instant. A write
    // made during this control step is not applied retroactively to a period.
    if (us == nextWrapUs) {
      latchedPWM = us >= commandReadyUs ? pendingPWM : previousPWM;
      nextWrapUs += pwmPeriodUs;
    }
    if (us == nextBlockUs) {
      completeSupplyScan(fixture, us);
      nextBlockUs += blockUs;
    }
    if (us == nextControlUs) {
      hal_mock_set_micros(fixture.startUs + us);
      const float volts = scanRailVolts(fixture, us);
      injectAdjRegisterData(4600, (uint8_t)(volts * 10.0f + .5f), 49U,
                            ADJ_STATUS_OK);
      previousPWM = pendingPWM;
      VP37_process(pump);
      pendingPWM = pump->output.finalPWM;
      commandReadyUs = us + (hal_micros() - (fixture.startUs + us));
      nextControlUs += 5000U;
    }
    if (us != nextMeasurementUs) {
      continue;
    }
    nextMeasurementUs += 500U;
    if (us < 100000U) {
      continue;
    }
    const float drive = (float)latchedPWM * scanRailVolts(fixture, us);
    if (referenceDrive == 0.0f) {
      referenceDrive = drive;
    }
    const bool stepSettled = !fixture.steps || (us < 120000U) ||
                             ((us >= 150000U) && (us < 390000U)) ||
                             (us >= 420000U);
    if (stepSettled) {
      peakError = fmaxf(peakError, fabsf(drive / referenceDrive - 1.0f));
    }
    if (!fixture.steps && !fixture.ripple &&
        (((us >= 160000U) && (us < 270000U)) ||
         ((us >= 430000U) && (us < 540000U)))) {
      const float error = drive / referenceDrive - 1.0f;
      steadyErrorSquared += error * error;
      steadySamples++;
    }
    minimumPWM = hal_min(minimumPWM, latchedPWM);
    maximumPWM = hal_max(maximumPWM, latchedPWM);
  }
  // PWM is held through a complete 130 Hz period; voltage is measured every
  // 0.5 ms, including after a write and before the next hardware latch.
  // The first 40 ms include slope acquisition. Bound that transient separately
  // from the established ramp, whose RMS also spans every latch phase.
  TEST_ASSERT_LESS_THAN_FLOAT(fixture.steps ? .01f : .032f, peakError);
  if (steadySamples > 0U) {
    TEST_ASSERT_LESS_THAN_FLOAT(
        .015f, sqrtf(steadyErrorSquared / (float)steadySamples));
  }
  if (fixture.ripple) {
    TEST_ASSERT_LESS_OR_EQUAL_INT32(3, maximumPWM - minimumPWM);
  }
}

void test_vp37_supply_ramp_preserves_drive_between_pwm_latches(void) {
  for (uint32_t phaseUs = 0U; phaseUs < 7600U; phaseUs += 1000U) {
    runLatchedSupplyProfile({100000U, false, false, false}, phaseUs);
    // Each phase is a separate acquisition and controller lifetime.
    tearDown();
  }
}

void test_vp37_supply_steps_settle_with_latched_pwm(void) {
  runLatchedSupplyProfile({100000U, true, false, false});
}

void test_vp37_supply_ripple_stays_quiet_with_latched_pwm(void) {
  runLatchedSupplyProfile({100000U, false, true, false});
}

void test_vp37_supply_prediction_handles_wrap_with_latched_pwm(void) {
  runLatchedSupplyProfile({UINT32_MAX - 300000U, false, false, false});
}

static void setupPredictionPump(VP37Pump *pump) {
  setupPumpForProcessTests(pump);
  pump->thermal.observationEnabled = true;
  pump->supply.cycleEnabled = true;
  pump->supply.cycleValid = true;
  pump->supply.localReady = true;
  pump->supply.localScale = 1.0f;
  VP37_setPositionDemandPercentage(pump, 50.0f);
}

static void processSupplyCapture(VP37Pump *pump, uint32_t nowUs,
                                 uint32_t sampleUs, float volts) {
  hal_mock_set_micros(nowUs);
  injectAdjRegisterData(4600, 144U, 49U, ADJ_STATUS_OK);
  pump->supply.cycleUs = sampleUs;
  pump->supply.cycleVolts = volts;
  VP37_process(pump);
}

void test_vp37_supply_prediction_stops_reverses_and_uses_each_sample_once(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPredictionPump(pump);
  processSupplyCapture(pump, 0U, 0U, 14.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->supply.predictionVolts);
  processSupplyCapture(pump, 5000U, 5000U, 14.1f);
  const float slope = pump->supply.voltageSlope;
  const float lead = pump->supply.predictionVolts;
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, slope);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, lead);
  processSupplyCapture(pump, 10000U, 5000U, 14.1f);
  TEST_ASSERT_EQUAL_FLOAT(slope, pump->supply.voltageSlope);
  TEST_ASSERT_GREATER_THAN_FLOAT(lead, pump->supply.predictionVolts);
  TEST_ASSERT_EQUAL_FLOAT(14.1f, pump->supply.inputVolts);
  processSupplyCapture(pump, 15000U, 15000U, 14.1f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->supply.predictionVolts);
  TEST_ASSERT_EQUAL_FLOAT(14.1f, pump->supply.heldVolts);
  processSupplyCapture(pump, 20000U, 20000U, 14.0f);
  TEST_ASSERT_LESS_THAN_FLOAT(0.0f, pump->supply.predictionVolts);
  processSupplyCapture(pump, 25000U, 25000U, 14.1f);
  TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(0.0f, pump->supply.predictionVolts);
}

void test_vp37_supply_prediction_resets_on_freeze_gap_fallback_and_rest(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPredictionPump(pump);
  processSupplyCapture(pump, 0U, 0U, 14.0f);
  processSupplyCapture(pump, 5000U, 5000U, 14.1f);
  const float held = pump->supply.heldVolts;
  pump->supply.frozen = true;
  processSupplyCapture(pump, 10000U, 10000U, 14.2f);
  TEST_ASSERT_FALSE(pump->supply.predictionReady);
  TEST_ASSERT_EQUAL_FLOAT(held, pump->supply.heldVolts);
  pump->supply.frozen = false;
  processSupplyCapture(pump, 15000U, 15000U, 14.3f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->supply.predictionVolts);
  processSupplyCapture(pump, 20000U, 20000U, 14.4f);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->supply.predictionVolts);
  pump->supply.cycleValid = false;
  processSupplyCapture(pump, 25000U, 25000U, 14.4f);
  TEST_ASSERT_FALSE(pump->supply.predictionReady);
  pump->supply.cycleValid = true;
  processSupplyCapture(pump, 30000U, 30000U, 14.5f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->supply.predictionVolts);
  processSupplyCapture(pump, 35000U, 35000U, 14.6f);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->supply.predictionVolts);
  processSupplyCapture(pump, 60000U, 60000U, 14.8f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->supply.predictionVolts);
  processSupplyCapture(pump, 65000U, 65000U, 14.9f);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->supply.predictionVolts);
  VP37_setPositionDemandPercentage(pump, 0.0f);
  pump->demand.desiredPosition = (float)pump->feedback.adjustMin;
  pump->demand.desired = pump->feedback.adjustMin;
  processSupplyCapture(pump, 70000U, 70000U, 15.0f);
  TEST_ASSERT_TRUE(pump->demand.atRest);
  TEST_ASSERT_FALSE(pump->supply.predictionReady);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
  VP37_setPositionDemandPercentage(pump, 50.0f);
  processSupplyCapture(pump, 75000U, 75000U, 15.1f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->supply.predictionVolts);
}

void test_vp37_supply_prediction_bounds_both_polarities_and_keeps_voltage_floor(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPredictionPump(pump);
  processSupplyCapture(pump, 0U, 0U, 14.0f);
  processSupplyCapture(pump, 5000U, 3000U, 17.0f);
  TEST_ASSERT_EQUAL_FLOAT(.5f, pump->supply.predictionVolts);
  TEST_ASSERT_EQUAL_FLOAT(17.0f, pump->supply.inputVolts);
  TEST_ASSERT_EQUAL_FLOAT(17.5f, pump->supply.heldVolts);
  processSupplyCapture(pump, 10000U, 10000U, 7.0f);
  TEST_ASSERT_EQUAL_FLOAT(-.5f, pump->supply.predictionVolts);
  TEST_ASSERT_EQUAL_FLOAT(7.0f, pump->supply.inputVolts);
  TEST_ASSERT_EQUAL_FLOAT(7.0f, pump->supply.heldVolts);
}

void test_vp37_period_skips_duplicate_updates_and_handles_wrap(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50.0f);
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
  VP37_setPositionDemandPercentage(pump, 1);
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

static void checkCurrentCorrectionAtLimit(bool upper, float correction) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->currentControl.enabled = true;
  pump->currentControl.correctionPwm = correction;
  VP37_setVP37PID(pump, 1.0f, .4f, 0.0f, false);
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setPositionDemandPercentage(pump, upper ? 100.0f : 1.0f));
  injectAdjRegisterData(upper ? 1000 : 9100, upper ? 70U : 144U,
                        upper ? 120U : 26U, ADJ_STATUS_OK);
  VP37_process(pump);

  // No new capture: the previous correction fades, but must still be
  // included in position anti-windup until it reaches zero.
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f,
                                 fabsf(pump->currentControl.correctionPwm));
  const float scale = pump->supply.correction * pump->thermal.scale;
  const int32_t boundary = upper ? VP37_PWM_MAX : VP37_PWM_MIN;
  TEST_ASSERT_INT32_WITHIN(1, boundary, pump->output.finalPWM);
  TEST_ASSERT_EQUAL(upper, pump->pid.terms.saturated_high);
  TEST_ASSERT_EQUAL(!upper, pump->pid.terms.saturated_low);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.01f,
                           (float)boundary / scale - pump->feedforward.pwm -
                               pump->currentControl.correctionPwm,
                           pump->pid.terms.output);
  tearDown();
}

void test_vp37_current_feedback_preserves_both_physical_limits(void) {
  const float corrections[] = {-40.0f, 40.0f};
  for (size_t i = 0U; i < COUNTOF(corrections); ++i) {
    checkCurrentCorrectionAtLimit(false, corrections[i]);
    checkCurrentCorrectionAtLimit(true, corrections[i]);
  }
}

void test_vp37_current_feedback_clears_at_rest_and_stop(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->currentControl.enabled = true;
  pump->currentControl.correctionPwm = 30.0f;
  pump->currentControl.requestedPwm = 40.0f;
  pump->currentControl.count = 1U;
  pump->currentControl.seen = true;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 0.0f));
  injectAdjRegisterData(100, 144U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->demand.atRest);
  TEST_ASSERT_TRUE(pump->currentControl.enabled);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->currentControl.correctionPwm);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->currentControl.requestedPwm);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->currentControl.count);
  TEST_ASSERT_FALSE(pump->currentControl.seen);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);

  // Starting again may record a new target but cannot reuse the old trim.
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 20.0f));
  hal_mock_set_millis(5U);
  injectAdjRegisterData(100, 144U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->demand.atRest);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->currentControl.correctionPwm);
  TEST_ASSERT_EQUAL_UINT32(1U, pump->currentControl.count);
  pump->currentControl.correctionPwm = -30.0f;
  pump->currentControl.requestedPwm = -40.0f;
  VP37_stop(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  TEST_ASSERT_TRUE(pump->currentControl.enabled);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->currentControl.correctionPwm);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->currentControl.requestedPwm);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->currentControl.count);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
}

void test_vp37_zero_demand_releases_drive_despite_feedback_offset(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 0);
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
  VP37_setPositionDemandPercentage(pump, 50);
  for (uint32_t ms = 5; ms <= 500; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4000, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pid.terms.integral);
  VP37_setPositionDemandPercentage(pump, 0);
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
  VP37_setPositionDemandPercentage(pump, 5);
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
  VP37_setPositionDemandPercentage(pump, .001f);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.desired);
  TEST_ASSERT_FALSE(pump->demand.atRest);
  TEST_ASSERT_GREATER_THAN_INT32(0, pump->output.finalPWM);
}

void test_vp37_feedback_fault_at_rest_still_latches_drive_off(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 0);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->demand.atRest);
  hal_mock_set_millis(5);
  injectAdjRegisterData(0, 144, 29, ADJ_STATUS_SIGNAL_LOST);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  VP37_setPositionDemandPercentage(pump, 50);
  hal_mock_set_millis(10);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
}

void test_vp37_target_ramp_uses_elapsed_time_and_preserves_overshoot(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 0);
  injectAdjRegisterData(100, 144, 26, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setPositionDemandPercentage(pump, 100);
  hal_mock_set_millis(10);
  injectAdjRegisterData(9500, 144, 26, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_INT32_WITHIN(1, 370, pump->demand.desired);
  TEST_ASSERT_EQUAL_INT32(pump->demand.desired - 9500, pump->pid.error);
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
void test_vp37_trace_preserves_consecutive_steps_until_drained(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50);
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

void test_vp37_integral_authority_is_independent_of_ki(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, .2f, 0, true);
  VP37_setPositionDemandPercentage(pump, 100);
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
  VP37_setPositionDemandPercentage(pump, 100);
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
  VP37_setPositionDemandPercentage(pump, 70);
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
  VP37_setPositionDemandPercentage(pump, 50);
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
  VP37_setPositionDemandPercentage(pump, 50);
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
  VP37_setPositionDemandPercentage(pump, 50);
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
  pump->thermal.driveObservedMs = hal_millis();
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
  VP37_setPositionDemandPercentage(pump, 50);
  injectAdjRegisterData(4500, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->demand.atRest);

  VP37_setPositionDemandPercentage(pump, 0);
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
  pump->thermal.driveObservedMs = hal_millis();
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
  VP37_setPositionDemandPercentage(pump, 100);
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
  for (unsigned range = 0; range < 2U; ++range) {
    pump->feedback.adjustMin = range == 0U ? 100 : 400;
    pump->feedback.adjustMax = range == 0U ? 9100 : 7400;
    for (size_t i = 0; i < COUNTOF(demand); ++i) {
      pump->demand.desired = -1;
      VP37_setPositionDemandPercentage(pump, demand[i]);
      hal_mock_set_millis(hal_millis() + 5U);
      injectAdjRegisterData((int16_t)pump->demand.target, 144, 49,
                            ADJ_STATUS_OK);
      VP37_process(pump);
      TEST_ASSERT_FLOAT_WITHIN(.05f, expectedHoldingFF(demand[i]),
                               pump->feedforward.pwm);
      TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
    }
  }
}

void test_vp37_climb_floor_does_not_bypass_target_ramp(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  VP37_setPositionDemandPercentage(pump, 0);
  injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setPositionDemandPercentage(pump, 100);
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
  VP37_setPositionDemandPercentage(pump, 50);
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
  VP37_setPositionDemandPercentage(pump, 50);
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
  VP37_setPositionDemandPercentage(pump, 50);

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
  VP37_setPositionDemandPercentage(pump, 50);

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
  VP37_setPositionDemandPercentage(pump, 50);
  injectAdjRegisterData(4600, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 120, pump->pid.integralLimit);
  VP37_setPositionDemandPercentage(pump, 100);
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
  VP37_setPositionDemandPercentage(pump, 0);
  injectAdjRegisterData(100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setPositionDemandPercentage(pump, 50);
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
  VP37_setPositionDemandPercentage(pump, 80);
  injectAdjRegisterData(7300, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setPositionDemandPercentage(pump, 100);
  hal_mock_set_millis(5);
  injectAdjRegisterData(7300, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_INT32_WITHIN(1, 7423, pump->demand.desired);
  VP37_setPositionDemandPercentage(pump, 0);
  hal_mock_set_millis(10);
  injectAdjRegisterData(7300, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_INT32_WITHIN(1, 7288, pump->demand.desired);
}

void test_vp37_downward_ramp_does_not_store_reverse_tracking_lag(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 90);
  injectAdjRegisterData(8200, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setPositionDemandPercentage(pump, 5);
  for (uint32_t ms = 5; ms <= 250; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(8200, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
  }
  // Reverse before the off state: the last descent must not bias the next rise.
  VP37_setPositionDemandPercentage(pump, 90);
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
  for (size_t i = 0; i < COUNTOF(demand); ++i) {
    pump->demand.desired = -1;
    VP37_setPositionDemandPercentage(pump, demand[i]);
    hal_mock_set_millis((uint32_t)(i + 1U) * 5U);
    injectAdjRegisterData((int16_t)pump->demand.target, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(.01f, expectedHoldingFF(demand[i]),
                             pump->feedforward.pwm);
  }
  // The top of the stroke holds flat: 95 % and 100 % share one command.
  TEST_ASSERT_EQUAL_FLOAT(expectedHoldingFF(95), expectedHoldingFF(100));
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.target);
}

void test_vp37_motion_feedforward_brakes_when_upper_ramp_stops(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  VP37_setPositionDemandPercentage(pump, 75);
  injectAdjRegisterData(6850, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedforward.motion);
  VP37_setPositionDemandPercentage(pump, 95);
  for (uint32_t ms = 5; ms <= 20; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  // The moving demand is about 80.5%: upper acceleration is already tapered.
  TEST_ASSERT_GREATER_THAN_FLOAT(15, pump->feedforward.motion);
  TEST_ASSERT_LESS_THAN_FLOAT(25, pump->feedforward.motion);
  for (uint32_t ms = 25; ms <= 500; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedforward.motion);
  TEST_ASSERT_FLOAT_WITHIN(.01f, expectedHoldingFF(95), pump->feedforward.pwm);
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  VP37_setPositionDemandPercentage(pump, 0);
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

void test_vp37_motion_taper_preserves_lower_drive_holding_and_descent(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  const float demands[] = {25, 50, 75, 80, 85, 90, 100};
  const float rising[] = {14.5f, 19.4f, 21.3f, 12.266667f, 0, 0, 0};
  const int32_t calibrations[][2] = {{100, 9100}, {500, 4500}};
  for (size_t c = 0U; c < COUNTOF(calibrations); ++c) {
    pump->feedback.adjustMin = calibrations[c][0];
    pump->feedback.adjustMax = calibrations[c][1];
    for (size_t i = 0U; i < COUNTOF(demands); ++i) {
      const int32_t position = pump->feedback.adjustMin +
                               (int32_t)((float)(pump->feedback.adjustMax -
                                                 pump->feedback.adjustMin) *
                                         demands[i] * .01f);
      pump->feedforward.riseBlend = 1.0f;
      pump->feedforward.fallBlend = 0.0f;
      const float upward = VP37_feedForward(pump, position);
      TEST_ASSERT_FLOAT_WITHIN(.001f, rising[i], pump->feedforward.motion);
      TEST_ASSERT_FLOAT_WITHIN(.001f, expectedHoldingFF(demands[i]),
                               upward - pump->feedforward.motion);
      pump->feedforward.riseBlend = 0.0f;
      pump->feedforward.fallBlend = .5f;
      const float downward = VP37_feedForward(pump, position);
      TEST_ASSERT_FLOAT_WITHIN(.001f, -14.5f, pump->feedforward.motion);
      TEST_ASSERT_FLOAT_WITHIN(.001f, expectedHoldingFF(demands[i]) - 14.5f,
                               downward);
    }
  }
}

void test_vp37_descent_feedforward_lowers_the_command_while_the_target_falls(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  pump->feedforward.motionBoostDown = 40.0f;
  VP37_setPositionDemandPercentage(pump, 90);
  injectAdjRegisterData(8200, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->feedforward.motion);
  const float holding = pump->feedforward.pwm;
  // A 300 %/s descent is 2.4 reference rates; the target counts as stationary
  // after 25 ms and the rate weight drops, so the term peaks around -77.
  VP37_setPositionDemandPercentage(pump, 10);
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
  VP37_setPositionDemandPercentage(pump, 5);
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
  VP37_setPositionDemandPercentage(pump, 90);
  for (uint32_t ms = 5; ms <= 500; ms += 5) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(8300, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  const float previous = pump->pid.terms.integral;
  TEST_ASSERT_LESS_THAN_FLOAT(-8, previous);
  VP37_setPositionDemandPercentage(pump, 95);
  hal_mock_set_millis(505);
  injectAdjRegisterData(8100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_GREATER_THAN_FLOAT(previous, pump->pid.terms.integral);
  TEST_ASSERT_LESS_THAN_FLOAT(0, pump->pid.terms.integral);
}

void test_vp37_stationary_target_uses_soft_approach(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 80);
  injectAdjRegisterData(7300, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setPositionDemandPercentage(pump, 100);
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
  pump->pid.topKd = .002f;
  VP37_setPositionDemandPercentage(pump, 75);
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
  VP37_setPositionDemandPercentage(pump, 73);

  injectAdjRegisterData(6100, 144U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  hal_mock_set_millis(5U);
  injectAdjRegisterData(7600, 144U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);

  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.kd);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.terms.derivative);
}

void test_vp37_upper_derivative_blends_only_above_85_percent(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0.0f, 0.0f, .001f, false);
  pump->pid.topKd = .002f;
  const float demands[] = {50.0f, 85.0f, 87.5f, 90.0f, 100.0f, 50.0f};
  const float gains[] = {.001f, .001f, .002f, .003f, .003f, .001f};
  uint32_t ms = 0U;

  for (size_t i = 0U; i < COUNTOF(demands); ++i) {
    VP37_setPositionDemandPercentage(pump, demands[i]);
    // Keep feedback in the middle: the gain follows demand, not measurement.
    runSupplyCycles(pump, ms, 120U, 4600, 144U, 0.0f);
    TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
    TEST_ASSERT_FLOAT_WITHIN(.000001f, gains[i], pump->pid.effectiveKd);
    TEST_ASSERT_FLOAT_WITHIN(.000001f, gains[i],
                             hal_pid_controller_get_kd(pump->pid.controller));
    float baseKd = 0.0f;
    VP37_getVP37PIDValues(pump, NULL, NULL, &baseKd);
    TEST_ASSERT_FLOAT_WITHIN(.000001f, .001f, baseKd);
  }
}

void test_vp37_upper_derivative_waits_for_stationary_target_and_slew(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->pid.topKd = .002f;
  VP37_setPositionDemandPercentage(pump, 90.0f);
  uint32_t ms = 0U;
  runSupplyCycles(pump, ms, 1U, 8200, 144U, 0.0f);
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.effectiveKd);
  runSupplyCycles(pump, ms, 6U, 8200, 144U, 0.0f);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->pid.effectiveKd);
  TEST_ASSERT_LESS_THAN_FLOAT(.001f, pump->pid.effectiveKd);
  runSupplyCycles(pump, ms, 100U, 8200, 144U, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .002f, pump->pid.effectiveKd);

  VP37_setPositionDemandPercentage(pump, 100.0f);
  uint32_t stationaryRampSteps = 0U;
  float previousGain = pump->pid.effectiveKd;
  for (uint32_t step = 0U; step < 40U; ++step) {
    runSupplyCycles(pump, ms, 1U, (int16_t)(8202U + (2U * step)), 144U, 0.0f);
    if (pump->demand.desired != pump->demand.target) {
      TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->pid.effectiveKd);
      TEST_ASSERT_LESS_THAN_FLOAT(previousGain, pump->pid.effectiveKd);
      TEST_ASSERT_LESS_THAN_FLOAT(.000183f,
                                  previousGain - pump->pid.effectiveKd);
      TEST_ASSERT_LESS_THAN_FLOAT(0.0f, pump->pid.terms.derivative);
      if (hal_millis_deadline_expired(pump->demand.targetChangedMs,
                                      VP37_TARGET_STABLE_MS)) {
        stationaryRampSteps++;
      }
    }
    previousGain = pump->pid.effectiveKd;
  }
  TEST_ASSERT_GREATER_THAN_UINT32(0U, stationaryRampSteps);
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  runSupplyCycles(pump, ms, 100U, 8280, 144U, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .002f, pump->pid.effectiveKd);
}

void test_vp37_upper_derivative_blend_uses_elapsed_control_time(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->pid.topKd = 0.0f;
  VP37_setPositionDemandPercentage(pump, 90.0f);
  uint32_t ms = 0U;
  runSupplyCycles(pump, ms, 10U, 8200, 144U, 0.0f);
  pump->pid.topKd = .002f;

  ms += 10U;
  hal_mock_set_millis(ms);
  injectAdjRegisterData(8200, 144U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_UINT32(10000U, pump->pidDtUs);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, 1.0f / 6.0f, pump->pid.topDBlend);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .002f / 6.0f, pump->pid.effectiveKd);

  VP37_setVP37PID(pump, VP37_PID_KP, VP37_PID_KI, 0.0f, true);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.topDBlend);
  runSupplyCycles(pump, ms, 1U, 8200, 144U, 0.0f);
  TEST_ASSERT_EQUAL_UINT32(5000U, pump->pidDtUs);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, 1.0f / 11.0f, pump->pid.topDBlend);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .002f / 11.0f, pump->pid.effectiveKd);
}

void test_vp37_upper_derivative_survives_62ms_target_updates_without_steps(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->pid.topKd = .004f;
  VP37_setPositionDemandPercentage(pump, 90.0f);
  uint32_t ms = 0U;
  runSupplyCycles(pump, ms, 120U, 8200, 144U, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .004f, pump->pid.effectiveKd);
  float previousGain = pump->pid.effectiveKd;
  uint32_t movingSteps = 0U;
  uint32_t settledSteps = 0U;
  for (uint32_t elapsed = 5U; elapsed <= 310U; elapsed += 5U) {
    // The previous ADC cadence held each integer target for about 62 ms.
    VP37_setPositionDemandPercentage(pump, 90.0f + (float)(elapsed / 62U));
    runSupplyCycles(pump, ms, 1U, (int16_t)(8200U + elapsed), 144U, 0.0f);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->pid.effectiveKd);
    TEST_ASSERT_LESS_THAN_FLOAT(.000365f,
                                fabsf(pump->pid.effectiveKd - previousGain));
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, pump->pid.terms.derivative);
    if (hal_millis_deadline_expired(pump->demand.targetChangedMs,
                                    VP37_TARGET_STABLE_MS)) {
      settledSteps++;
    } else {
      movingSteps++;
    }
    previousGain = pump->pid.effectiveKd;
  }
  TEST_ASSERT_GREATER_THAN_UINT32(10U, movingSteps);
  TEST_ASSERT_GREATER_THAN_UINT32(10U, settledSteps);
}

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

void test_vp37_upper_derivative_opposes_both_directions_of_motion(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0.0f, 0.0f, 0.0f, false);
  pump->pid.topKd = .002f;
  VP37_setPositionDemandPercentage(pump, 90.0f);
  uint32_t ms = 0U;
  runSupplyCycles(pump, ms, 8U, 8200, 144U, 0.0f);
  const float holdingCommand = pump->output.pwmValue;

  runSupplyCycles(pump, ms, 1U, 8290, 144U, 0.0f);
  TEST_ASSERT_LESS_THAN_FLOAT(0.0f, pump->pid.terms.derivative);
  TEST_ASSERT_LESS_THAN_FLOAT(holdingCommand, pump->output.pwmValue);
  runSupplyCycles(pump, ms, 1U, 8200, 144U, 0.0f);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->pid.terms.derivative);
  TEST_ASSERT_GREATER_THAN_FLOAT(holdingCommand, pump->output.pwmValue);
}

void test_vp37_upper_derivative_changes_preserve_integral_and_history(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0.0f, .2f, 0.0f, false);
  VP37_setPositionDemandPercentage(pump, 90.0f);
  uint32_t ms = 0U;
  runSupplyCycles(pump, ms, 8U, 8100, 144U, 0.0f);
  float integral = pump->pid.terms.integral;
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, integral);

  pump->pid.topKd = .002f;
  runSupplyCycles(pump, ms, 1U, 8130, 144U, 0.0f);
  TEST_ASSERT_GREATER_THAN_FLOAT(integral, pump->pid.terms.integral);
  TEST_ASSERT_LESS_THAN_FLOAT(0.0f, pump->pid.terms.derivative);
  integral = pump->pid.terms.integral;

  pump->pid.topKd = 0.0f;
  runSupplyCycles(pump, ms, 1U, 8130, 144U, 0.0f);
  TEST_ASSERT_GREATER_THAN_FLOAT(integral, pump->pid.terms.integral);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.derivative);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.topDBlend);
}

void test_vp37_upper_derivative_keeps_zero_release_and_fault_shutdown(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->pid.topKd = .002f;
  VP37_setPositionDemandPercentage(pump, 90.0f);
  uint32_t ms = 0U;
  runSupplyCycles(pump, ms, 8U, 8200, 144U, 0.0f);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->pid.effectiveKd);

  VP37_setPositionDemandPercentage(pump, 0.0f);
  runSupplyCycles(pump, ms, 80U, 1000, 144U, 0.0f);
  TEST_ASSERT_TRUE(pump->demand.atRest);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.effectiveKd);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.topDBlend);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.output);

  VP37_setPositionDemandPercentage(pump, 90.0f);
  runSupplyCycles(pump, ms, 200U, 8200, 144U, 0.0f);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->pid.effectiveKd);
  ms += 5U;
  hal_mock_set_millis(ms);
  injectAdjRegisterData(8200, 144U, 49U, ADJ_STATUS_SIGNAL_LOST);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
  runSupplyCycles(pump, ms, 2U, 8200, 144U, 0.0f);
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
}

void test_vp37_slew_tracks_a_250_percent_per_second_input(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  for (uint32_t ms = 0; ms <= 405; ++ms) {
    hal_mock_set_millis(ms);
    // A separately specified input ramp, sampled by the 5 ms controller.
    const float target = ms < 400U ? (float)(ms / 4U) : 100.0f;
    VP37_setPositionDemandPercentage(pump, target);
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
  VP37_setPositionDemandPercentage(pump, 50);
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
  VP37_setPositionDemandPercentage(pump, 50);
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
  VP37_setPositionDemandPercentage(pump, 50);
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

void test_vp37_measured_drive_consumes_each_capture_once(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setPositionDemandPercentage(pump, 50.0f);
  uint32_t ms = 0U;
  runDriveCycles(pump, ms, 30U, 1.32f, false);
  runDriveCycles(pump, ms, 1U, 1.32f, true);
  TEST_ASSERT_EQUAL_UINT32(1U, pump->thermal.driveSamples);
  const float resistance = pump->thermal.driveResistance;
  const uint32_t acceptedMs = pump->thermal.driveUpdatedMs;
  runDriveCycles(pump, ms, 15U, 1.32f, false);
  TEST_ASSERT_EQUAL_UINT32(1U, pump->thermal.driveSamples);
  TEST_ASSERT_EQUAL_UINT32(acceptedMs, pump->thermal.driveUpdatedMs);
  TEST_ASSERT_EQUAL_FLOAT(resistance, pump->thermal.driveResistance);
  TEST_ASSERT_FALSE(pump->thermal.driveResistanceReady);
  runDriveCycles(pump, ms, 1U, 1.32f, true);
  TEST_ASSERT_EQUAL_UINT32(2U, pump->thermal.driveSamples);
  TEST_ASSERT_GREATER_THAN_UINT32(acceptedMs, pump->thermal.driveUpdatedMs);
}

void test_vp37_measured_drive_holds_through_supply_transients(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setPositionDemandPercentage(pump, 50.0f);
  uint32_t ms = 0U;
  runDriveCycles(pump, ms, 1400U, 1.32f, true);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  const float resistance = pump->thermal.driveResistance;
  const uint32_t acceptedMs = pump->thermal.driveUpdatedMs;
  // The current is still settling after the rail changes. These observations
  // deliberately look like a hot coil; voltage feedforward must not teach it.
  for (uint32_t i = 0U; i < 20U; ++i) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4400, 120U, 29U, ADJ_STATUS_OK);
    feedDriveObservation(pump, 1.8f, 12.0f);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
    TEST_ASSERT_EQUAL_FLOAT(resistance, pump->thermal.driveResistance);
    TEST_ASSERT_EQUAL_UINT32(acceptedMs, pump->thermal.driveUpdatedMs);
  }
  // The last rejected capture remains young after the guard expires, but it
  // still describes the transient and must never be reconsidered.
  ms += 5U;
  hal_mock_set_millis(ms);
  injectAdjRegisterData(4400, 120U, 29U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_FLOAT(resistance, pump->thermal.driveResistance);
  TEST_ASSERT_EQUAL_UINT32(acceptedMs, pump->thermal.driveUpdatedMs);
  // A settled rail and a new observation resume learning without dropping
  // the retained compensation in between.
  for (uint32_t i = 0U; i < 100U; ++i) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4400, 120U, 29U, ADJ_STATUS_OK);
    feedDriveObservation(pump, 1.32f, 12.0f);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_GREATER_THAN_UINT32(acceptedMs, pump->thermal.driveUpdatedMs);
  TEST_ASSERT_FLOAT_WITHIN(.01f, resistance, pump->thermal.driveResistance);
}

void test_vp37_measured_drive_ignores_captures_of_the_coil_onset(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setPositionDemandPercentage(pump, 50.0f);
  uint32_t ms = 0U;
  runDriveCycles(pump, ms, 1400U, 1.32f, true);
  TEST_ASSERT_TRUE(pump->thermal.driveOnsetPassed);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);

  // Rest releases the coil.
  VP37_setPositionDemandPercentage(pump, 0.0f);
  for (uint32_t i = 0U; i < 200U; ++i) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(100, 144U, 29U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
  TEST_ASSERT_FALSE(pump->thermal.driveOnsetPassed);
  const float resistance = pump->thermal.driveResistance;
  const uint32_t learnedMs = pump->thermal.driveUpdatedMs;

  // The next approach: while the current is still building up, duty times
  // voltage over current reads far too much resistance. Those captures are
  // electrically healthy, keep the estimate alive, and teach it nothing.
  VP37_setPositionDemandPercentage(pump, 50.0f);
  const uint32_t onsetSteps = (VP37_DRIVE_ONSET_MS / 5U) - 1U;
  for (uint32_t i = 0U; i < onsetSteps; ++i) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(100, 144U, 29U, ADJ_STATUS_OK);
    feedDriveObservation(pump, 2.6f, 14.0f);
    VP37_process(pump);
    TEST_ASSERT_FALSE(pump->thermal.driveOnsetPassed);
    TEST_ASSERT_EQUAL_FLOAT(resistance, pump->thermal.driveResistance);
    TEST_ASSERT_EQUAL_UINT32(learnedMs, pump->thermal.driveUpdatedMs);
    TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  }

  // Once the onset time has passed, learning resumes.
  runDriveCycles(pump, ms, 60U, 1.32f, true);
  TEST_ASSERT_TRUE(pump->thermal.driveOnsetPassed);
  TEST_ASSERT_GREATER_THAN_UINT32(learnedMs, pump->thermal.driveUpdatedMs);
  TEST_ASSERT_FLOAT_WITHIN(.01f, resistance, pump->thermal.driveResistance);
}

void test_vp37_measured_drive_detects_accumulated_small_voltage_changes(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setPositionDemandPercentage(pump, 50.0f);
  uint32_t ms = 0U;
  runDriveCycles(pump, ms, 1400U, 1.32f, true);
  TEST_ASSERT_TRUE(pump->thermal.driveVoltageSettled);
  for (uint32_t i = 1U; i <= 40U; ++i) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4400, 144U, 29U, ADJ_STATUS_OK);
    injectLocalSupplyVoltage(14.4f - .05f * (float)i);
    feedDriveObservation(pump, 1.32f, 14.4f);
    VP37_process(pump);
  }
  // Every individual 5 ms delta is smaller than the stability threshold,
  // but a 2 V change over 200 ms is not a settled supply.
  TEST_ASSERT_FALSE(pump->thermal.driveVoltageSettled);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
}

void test_vp37_measured_drive_retains_estimate_through_a_long_supply_sweep(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setPositionDemandPercentage(pump, 50.0f);
  uint32_t ms = 0U;
  runDriveCycles(pump, ms, 1400U, 1.32f, true);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  const float resistance = pump->thermal.driveResistance;
  const uint32_t learnedMs = pump->thermal.driveUpdatedMs;
  const uint32_t count = (VP37_DRIVE_STALE_MS + 1000U) / 5U;
  for (uint32_t i = 0U; i < count; ++i) {
    // Every 50 ms the rail changes, so the learning guard stays engaged for
    // longer than the stale timeout. Captures remain new and electrically
    // valid.
    const float volts = ((i / 10U) % 2U) == 0U ? 12.0f : 14.4f;
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4400, (uint8_t)(volts * 10.0f + .5f), 29U,
                          ADJ_STATUS_OK);
    injectLocalSupplyVoltage(volts);
    feedDriveObservation(pump, 1.8f, volts);
    VP37_process(pump);
    TEST_ASSERT_FALSE(pump->thermal.driveVoltageSettled);
    TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
    TEST_ASSERT_EQUAL_FLOAT(resistance, pump->thermal.driveResistance);
    TEST_ASSERT_EQUAL_UINT32(learnedMs, pump->thermal.driveUpdatedMs);
    TEST_ASSERT_EQUAL_UINT32(ms, pump->thermal.driveObservedMs);
  }
}

void test_vp37_measured_drive_expires_when_fresh_captures_stop(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setPositionDemandPercentage(pump, 50.0f);
  uint32_t ms = 0U;
  runDriveCycles(pump, ms, 1400U, 1.32f, true);
  const uint32_t observedMs = pump->thermal.driveObservedMs;
  const uint32_t learnedMs = pump->thermal.driveUpdatedMs;
  const float resistance = pump->thermal.driveResistance;
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  // The cached capture is first a duplicate, then stale. Repeated processing
  // must not renew its health timestamp or hold compensation indefinitely.
  runDriveCycles(pump, ms, (VP37_DRIVE_STALE_MS / 5U) - 1U, 1.32f, false);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_EQUAL_UINT32(observedMs, pump->thermal.driveObservedMs);
  runDriveCycles(pump, ms, 2U, 1.32f, false);
  TEST_ASSERT_FALSE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_EQUAL_UINT32(observedMs, pump->thermal.driveObservedMs);
  TEST_ASSERT_EQUAL_UINT32(learnedMs, pump->thermal.driveUpdatedMs);
  TEST_ASSERT_EQUAL_FLOAT(resistance, pump->thermal.driveResistance);
}

void test_vp37_measured_drive_does_not_apply_silence_as_heating_time(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setPositionDemandPercentage(pump, 50.0f);
  uint32_t ms = 0U;
  runDriveCycles(pump, ms, 1400U, 1.2f, true);
  const float resistance = pump->thermal.driveResistance;
  pump->thermal.cycleValid = false;
  runDriveCycles(pump, ms, 2200U, 1.2f, false);
  TEST_ASSERT_FALSE(pump->thermal.driveCompensationUsed);
  // A single dubious observation after eleven seconds without captures must
  // remain a small filter update, not stand in for eleven seconds of heating.
  runDriveCycles(pump, ms, 1U, 1.8f, true);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_GREATER_THAN_FLOAT(resistance, pump->thermal.driveResistance);
  TEST_ASSERT_LESS_THAN_FLOAT(.02f, pump->thermal.driveResistance - resistance);
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
  VP37_setPositionDemandPercentage(pump, 50);
  runDriveCycles(pump, ms, 400U, VP37_DRIVE_REFERENCE_OHMS, false);
  TEST_ASSERT_EQUAL_FLOAT((float)VP37_PID_DEADBAND,
                          pump->pid.integralDeadbandHz);

  // At full stroke the dead zone reaches the configured top.
  VP37_setPositionDemandPercentage(pump, 100);
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

// ── Proportional path of the upper stroke ────────────────────────────────────

static void placeDemandAt(VP37Pump *pump, float percent) {
  const float travel =
      (float)(pump->feedback.adjustMax - pump->feedback.adjustMin);
  pump->demand.desiredPosition =
      (float)pump->feedback.adjustMin + travel * percent * 0.01f;
}

static const float kTopGain =
    VP37_PROPORTIONAL_GAIN_MAP[VP37_STROKE_TAPER_KNOTS - 1U]
                              [VP37_TAPER_COL_VALUE];
static const float kTopGainStart =
    VP37_PROPORTIONAL_GAIN_MAP[VP37_STROKE_TAPER_KNOTS - 2U]
                              [VP37_TAPER_COL_PERCENT];
static const float kTopGainEnd =
    VP37_PROPORTIONAL_GAIN_MAP[VP37_STROKE_TAPER_KNOTS - 1U]
                              [VP37_TAPER_COL_PERCENT];
static const float kTopErrorLimit =
    VP37_PROPORTIONAL_ERROR_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS - 1U]
                                     [VP37_TAPER_COL_VALUE];

void test_vp37_proportional_gain_rises_only_at_the_top_of_the_stroke(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->pid.standingBlend = 1.0f;

  placeDemandAt(pump, 50.0f);
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KP, VP37_proportionalGain(pump));
  placeDemandAt(pump, kTopGainStart);
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KP, VP37_proportionalGain(pump));

  placeDemandAt(pump, 0.5f * (kTopGainStart + kTopGainEnd));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, VP37_PID_KP * 0.5f * (1.0f + kTopGain),
                           VP37_proportionalGain(pump));

  placeDemandAt(pump, kTopGainEnd);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, VP37_PID_KP * kTopGain,
                           VP37_proportionalGain(pump));
  placeDemandAt(pump, 100.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, VP37_PID_KP * kTopGain,
                           VP37_proportionalGain(pump));

  // A moving target keeps the base gain; the handover is gradual.
  pump->pid.standingBlend = 0.0f;
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KP, VP37_proportionalGain(pump));
  pump->pid.standingBlend = 0.5f;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, VP37_PID_KP * 0.5f * (1.0f + kTopGain),
                           VP37_proportionalGain(pump));

  // A bench gain keeps the same shape.
  pump->pid.standingBlend = 1.0f;
  VP37_setVP37PID(pump, 0.04f, VP37_PID_KI, VP37_PID_KD, false);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.04f * kTopGain,
                           VP37_proportionalGain(pump));
}

void test_vp37_error_bound_binds_only_in_the_upper_stroke(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->pid.standingBlend = 1.0f;

  placeDemandAt(pump, 50.0f);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, VP37_boundedError(pump, 1500.0f));
  TEST_ASSERT_EQUAL_FLOAT(-1500.0f, VP37_boundedError(pump, -1500.0f));

  placeDemandAt(pump, 95.0f);
  TEST_ASSERT_EQUAL_FLOAT(kTopErrorLimit, VP37_boundedError(pump, 1500.0f));
  TEST_ASSERT_EQUAL_FLOAT(-kTopErrorLimit, VP37_boundedError(pump, -1500.0f));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, VP37_boundedError(pump, 100.0f));

  // A moving target is tracked with the whole error.
  pump->pid.standingBlend = 0.0f;
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, VP37_boundedError(pump, 1500.0f));
}

void test_vp37_feedback_lead_acts_on_the_newest_sample_inside_the_bound(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->pid.standingBlend = 1.0f;
  pump->pid.effectiveKp = VP37_PID_KP;
  pump->pid.error = 100;
  pump->feedback.leadHz = 40;

  // The lower stroke keeps the filtered position, and so does a moving target.
  placeDemandAt(pump, 50.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_feedbackLead(pump));
  placeDemandAt(pump, 88.0f);
  pump->pid.standingBlend = 0.0f;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_feedbackLead(pump));
  pump->pid.standingBlend = 1.0f;

  // The unfiltered position runs 40 Hz ahead: less command, by Kp times that.
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, -VP37_PID_KP * 40.0f,
                           VP37_feedbackLead(pump));
  pump->feedback.leadHz = -40;
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, VP37_PID_KP * 40.0f, VP37_feedbackLead(pump));

  pump->feedback.leadWeight = 0.5f;
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, VP37_PID_KP * 20.0f, VP37_feedbackLead(pump));
  pump->feedback.leadWeight = 0.0f;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_feedbackLead(pump));

  // Upper stroke: a saturated error leaves nothing for the newest sample to
  // add, and one near the bound is completed up to it, never past it.
  pump->feedback.leadWeight = 1.0f;
  placeDemandAt(pump, 95.0f);
  pump->pid.error = (int32_t)kTopErrorLimit + 700;
  pump->feedback.leadHz = 50;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_feedbackLead(pump));
  pump->pid.error = (int32_t)kTopErrorLimit - 20;
  pump->feedback.leadHz = -50;
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, VP37_PID_KP * 20.0f, VP37_feedbackLead(pump));
}

static void processFastSample(VP37Pump *pump, adjustometer_feedback_t &sample,
                              uint32_t &us, int16_t position,
                              int32_t rawAheadHz) {
  us += 5000U;
  hal_mock_set_micros(us);
  hal_mock_set_millis(us / 1000U);
  sample.number++;
  sample.measuredUs = us;
  sample.pulseHz = position;
  sample.baselineHz = 34000U;
  sample.filteredHz = 34000U - (uint32_t)position;
  // The frequency falls as the position rises.
  sample.rawHz = (uint32_t)((int32_t)sample.filteredHz - rawAheadHz);
  injectFastAdjustometer(sample);
  VP37_process(pump);
}

void test_vp37_process_puts_the_filter_lag_into_the_command_and_its_limits(
    void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  setVP37AdjustometerFastFeedback(true);
  adjustometer_feedback_t sample = {};
  sample.voltage = 144;
  sample.fuelTemp = 50;
  uint32_t us = 100000U;
  VP37_setPositionDemandPercentage(pump, 88);
  for (uint32_t i = 0U; i < 300U; i++) {
    processFastSample(pump, sample, us, 8000, 0);
  }
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  TEST_ASSERT_EQUAL_INT32(0, pump->feedback.leadHz);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.feedbackLead);
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KP, pump->pid.effectiveKp);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, pump->pid.standingBlend);

  // The ripple of a standing actuator stays inside the dead zone.
  processFastSample(pump, sample, us, 8000,
                    (int32_t)VP37_FEEDBACK_LEAD_DEADBAND_HZ - 10);
  TEST_ASSERT_EQUAL_INT32(0, pump->feedback.leadHz);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.feedbackLead);

  // The newest sample is 60 Hz further ahead than the dead zone reaches.
  processFastSample(pump, sample, us, 8000,
                    (int32_t)VP37_FEEDBACK_LEAD_DEADBAND_HZ + 60);
  TEST_ASSERT_EQUAL_INT32(60, pump->feedback.leadHz);
  TEST_ASSERT_EQUAL_INT32(8000, pump->feedback.position);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -VP37_PID_KP * 60.0f, pump->pid.feedbackLead);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f,
                           pump->feedforward.pwm + pump->pid.correction +
                               pump->pid.feedbackLead,
                           pump->output.pwmValue);
  // The correction gives up exactly the authority the lead term uses.
  const float scale = pump->supply.correction * pump->thermal.scale;
  const float upperCommand = (float)VP37_PWM_MAX / scale;
  TEST_ASSERT_FLOAT_WITHIN(
      1e-2f,
      fminf(pump->pid.positiveLimit, upperCommand - pump->feedforward.pwm) -
          pump->currentControl.correctionPwm - pump->pid.feedbackLead,
      pump->pid.upperLimit);

  // A zero-held reading carries no lag to act on.
  VP37_setPositionDemandPercentage(pump, 30);
  processFastSample(pump, sample, us, 0, 60);
  TEST_ASSERT_EQUAL_INT32(0, pump->feedback.leadHz);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.feedbackLead);
}

void test_vp37_loop_acts_on_the_bounded_error_in_the_upper_stroke(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  uint32_t ms = 0U;
  VP37_setPositionDemandPercentage(pump, 100);
  // The actuator stays far below while the demand reaches the top.
  for (uint32_t i = 0U; i < 400U; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData(5000, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.desired);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax - 5000, pump->pid.error);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, VP37_PID_KP * kTopGain * kTopErrorLimit,
                           pump->pid.terms.proportional);
}

void test_vp37_moving_target_keeps_the_plain_loop_at_the_top(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  uint32_t ms = 0U;
  // The target changes every other step, as a cyclic or a pedal demand does,
  // and never stands for VP37_TARGET_STABLE_MS.
  for (uint32_t i = 0U; i < 400U; i++) {
    ms += 5U;
    hal_mock_set_millis(ms);
    VP37_setPositionDemandPercentage(pump, ((i / 2U) % 2U) ? 97.0f : 99.0f);
    injectAdjRegisterData(5000, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.standingBlend);
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KP, pump->pid.effectiveKp);
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, VP37_PID_KP * (float)pump->pid.error,
                           pump->pid.terms.proportional);
  TEST_ASSERT_GREATER_THAN_INT32((int32_t)kTopErrorLimit, pump->pid.error);
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
  VP37_setPositionDemandPercentage(pump, 100);
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
  VP37_setPositionDemandPercentage(pump, 50);
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
  VP37_setPositionDemandPercentage(pump, 50);
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
  VP37_setPositionDemandPercentage(pump, 0);
  holdAt(pump, ms, 1200U, 120);
  TEST_ASSERT_TRUE(pump->demand.atRest);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.terms.integral);
  VP37_setPositionDemandPercentage(pump, 50);
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

void test_vp37_fresh_supply_scales_the_command_with_bounded_prediction(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  pump->thermal.observationEnabled = true;
  pump->supply.cycleEnabled = true;
  pump->supply.cycleValid = true;
  pump->supply.cycleVolts = 14.5f;
  pump->supply.cycleUs = 0U;
  pump->supply.localReady = true;
  pump->supply.localScale = 1.0f;
  VP37_setPositionDemandPercentage(pump, 50);
  hal_mock_set_micros(0U);
  injectAdjRegisterData(4500, 145, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->supply.cycleUsed);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 14.5f, pump->supply.heldVolts);

  // A cranking-sized drop in the captured mean reaches the scale in the very
  // next control step; the bounded lead acts without the fallback filter.
  pump->supply.cycleVolts = 8.0f;
  pump->supply.cycleUs = 5000U;
  hal_mock_set_micros(5000U);
  injectAdjRegisterData(4500, 145, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->supply.cycleUsed);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 8.0f, pump->supply.inputVolts);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 7.5f, pump->supply.heldVolts);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 1.6f, pump->supply.correction);

  // V2 holds the scale where it is while the input keeps moving.
  pump->supply.frozen = true;
  pump->supply.cycleVolts = 14.5f;
  pump->supply.cycleUs = 10000U;
  hal_mock_set_micros(10000U);
  injectAdjRegisterData(4500, 145, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 14.5f, pump->supply.inputVolts);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 7.5f, pump->supply.heldVolts);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_vp37_current_scan_increases_drive_for_a_current_deficit);
  RUN_TEST(test_vp37_current_scan_reduces_drive_for_a_current_excess);
  RUN_TEST(test_vp37_current_scan_matches_target_before_active_low_latch);
  RUN_TEST(test_vp37_current_feedback_preserves_both_physical_limits);
  RUN_TEST(test_vp37_current_feedback_clears_at_rest_and_stop);
  RUN_TEST(test_vp37_derivative_opposes_rebound_within_one_control_step);
  RUN_TEST(test_vp37_default_derivative_ignores_large_feedback_step);
  RUN_TEST(test_vp37_upper_derivative_blends_only_above_85_percent);
  RUN_TEST(test_vp37_upper_derivative_waits_for_stationary_target_and_slew);
  RUN_TEST(test_vp37_upper_derivative_blend_uses_elapsed_control_time);
  RUN_TEST(
      test_vp37_upper_derivative_survives_62ms_target_updates_without_steps);
  RUN_TEST(test_vp37_sensor_demand_uses_shared_ramp_and_motion_feedforward);
  RUN_TEST(test_vp37_upper_derivative_opposes_both_directions_of_motion);
  RUN_TEST(test_vp37_upper_derivative_changes_preserve_integral_and_history);
  RUN_TEST(test_vp37_upper_derivative_keeps_zero_release_and_fault_shutdown);
  RUN_TEST(test_vp37_stationary_target_uses_soft_approach);
  RUN_TEST(test_vp37_reinitialization_does_not_exhaust_pwm_channels);
  RUN_TEST(test_vp37_slew_tracks_a_250_percent_per_second_input);
  RUN_TEST(test_vp37_ramp_unwinds_existing_negative_trim);
  RUN_TEST(test_vp37_motion_feedforward_brakes_when_upper_ramp_stops);
  RUN_TEST(test_vp37_motion_taper_preserves_lower_drive_holding_and_descent);
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
  RUN_TEST(test_vp37_position_demand_reaches_calibrated_maximum);
  RUN_TEST(test_vp37_position_demand_clamps_negative_input);
  RUN_TEST(test_vp37_position_demand_validates_input_and_calibration);
  RUN_TEST(test_vp37_position_demand_preserves_fractional_targets_and_hold_age);
  RUN_TEST(test_vp37_position_demand_in_counts_matches_the_percentage_entry);
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
  RUN_TEST(
      test_vp37_scanned_supply_preserves_drive_during_fast_bidirectional_ramps);
  RUN_TEST(test_vp37_scanned_supply_recovers_both_steps_within_30ms);
  RUN_TEST(test_vp37_scanned_supply_rejects_phase_ripple_with_clipped_current);
  RUN_TEST(test_vp37_scanned_supply_ramp_handles_microsecond_wrap);
  RUN_TEST(test_vp37_scanned_supply_expires_and_recovers_without_recalibration);
  RUN_TEST(test_vp37_supply_ramp_preserves_drive_between_pwm_latches);
  RUN_TEST(test_vp37_supply_steps_settle_with_latched_pwm);
  RUN_TEST(test_vp37_supply_ripple_stays_quiet_with_latched_pwm);
  RUN_TEST(test_vp37_supply_prediction_handles_wrap_with_latched_pwm);
  RUN_TEST(
      test_vp37_supply_prediction_stops_reverses_and_uses_each_sample_once);
  RUN_TEST(test_vp37_supply_prediction_resets_on_freeze_gap_fallback_and_rest);
  RUN_TEST(
      test_vp37_supply_prediction_bounds_both_polarities_and_keeps_voltage_floor);

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
  RUN_TEST(test_vp37_measured_drive_consumes_each_capture_once);
  RUN_TEST(test_vp37_measured_drive_holds_through_supply_transients);
  RUN_TEST(test_vp37_measured_drive_ignores_captures_of_the_coil_onset);
  RUN_TEST(test_vp37_measured_drive_detects_accumulated_small_voltage_changes);
  RUN_TEST(
      test_vp37_measured_drive_retains_estimate_through_a_long_supply_sweep);
  RUN_TEST(test_vp37_measured_drive_expires_when_fresh_captures_stop);
  RUN_TEST(test_vp37_measured_drive_does_not_apply_silence_as_heating_time);
  RUN_TEST(test_vp37_stroke_taper_holds_ends_flat_and_walks_the_knots);
  RUN_TEST(test_vp37_integral_deadband_widens_only_in_the_upper_stroke);
  RUN_TEST(test_vp37_proportional_gain_rises_only_at_the_top_of_the_stroke);
  RUN_TEST(test_vp37_error_bound_binds_only_in_the_upper_stroke);
  RUN_TEST(test_vp37_feedback_lead_acts_on_the_newest_sample_inside_the_bound);
  RUN_TEST(
      test_vp37_process_puts_the_filter_lag_into_the_command_and_its_limits);
  RUN_TEST(test_vp37_loop_acts_on_the_bounded_error_in_the_upper_stroke);
  RUN_TEST(test_vp37_moving_target_keeps_the_plain_loop_at_the_top);
  RUN_TEST(
      test_vp37_integral_hold_bands_stay_fixed_under_the_scheduled_dead_zone);
  RUN_TEST(test_vp37_map_trim_absorbs_the_settled_integral_without_a_bump);
  RUN_TEST(test_vp37_fresh_supply_scales_the_command_with_bounded_prediction);
  return UNITY_END();
}

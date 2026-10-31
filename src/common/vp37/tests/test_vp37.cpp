// The VP37 module's own suite: demand, feedforward, PID rules, supply and
// thermal compensation, the current loop, the snapshot trace and the tuning
// API, all through the module fixture's fake board services. The ECU-side
// integration cases live in src/ECU/tests/test_vp37_adapter.cpp.
#include "unity.h"
#include "vp37_module_fixture.h"
#include "vp37_testable.h"
#include <math.h>

// ── Helpers ──────────────────────────────────────────────────────────────────

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

// ── Lifecycle ────────────────────────────────────────────────────────────────

void setUp(void) { setUpVp37Fixture(); }

void tearDown(void) {
  (void)VP37_currentScanStop();
  vp37AdjustometerTransferBusy(false);
  VP37Pump *pump = vp37TestPump();
  if (pump->pid.controller != NULL) {
    hal_pid_controller_destroy(pump->pid.controller);
    pump->pid.controller = NULL;
  }
}

// ── VP37 PID API tests ───────────────────────────────────────────────────────

void test_vp37_pid_setter_updates_controller_gains(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
  memset(pump, 0, sizeof(*pump));
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 9100;
  pump->demand.physicalLimitPercent = (float)VP37_PERCENT_MAX;

  VP37_setPositionDemandPercentage(pump, 100.0f);

  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.target);
}

void test_vp37_position_demand_clamps_negative_input(void) {
  VP37Pump *pump = vp37TestPump();
  memset(pump, 0, sizeof(*pump));
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 200;
  pump->feedback.adjustMax = 9200;
  pump->demand.physicalLimitPercent = (float)VP37_PERCENT_MAX;

  VP37_setPositionDemandPercentage(pump, -10.0f);

  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.target);
}

void test_vp37_position_demand_validates_input_and_calibration(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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

// Calibrated position that a share of the travel reaches.
static int32_t strokeAt(const VP37Pump *pump, float share) {
  const float travel =
      (float)(pump->feedback.adjustMax - pump->feedback.adjustMin);
  return pump->feedback.adjustMin + (int32_t)lroundf(travel * share);
}

void test_vp37_percent_demand_ends_at_the_physical_limit(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->demand.physicalLimitPercent = (float)VP37_PHYSICAL_LIMIT_PERCENT;
  const int32_t top = strokeAt(pump, VP37_PHYSICAL_LIMIT_PERCENT * .01f);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(top, pump->demand.target);
  // The demand scale shrinks with the limit; the bottom stays where it was.
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 50.0f));
  TEST_ASSERT_INT32_WITHIN(1,
                           strokeAt(pump, VP37_PHYSICAL_LIMIT_PERCENT * .005f),
                           pump->demand.target);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 0.0f));
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.target);
}

void test_vp37_counts_demand_is_capped_at_the_physical_limit(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->demand.physicalLimitPercent = (float)VP37_PHYSICAL_LIMIT_PERCENT;
  const int32_t top = strokeAt(pump, VP37_PHYSICAL_LIMIT_PERCENT * .01f);
  TEST_ASSERT_EQUAL_INT32(top, VP37_getPositionDemandMaxValue(pump));
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setPositionDemandValue(pump, pump->feedback.adjustMax));
  TEST_ASSERT_EQUAL_INT32(top, pump->demand.target);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, pump->demand.requestedPercent);
  const int32_t middle =
      pump->feedback.adjustMin + ((top - pump->feedback.adjustMin) / 2);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandValue(pump, middle));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 50.0f, pump->demand.requestedPercent);
}

void test_vp37_full_physical_limit_keeps_the_calibrated_top(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->demand.physicalLimitPercent = (float)VP37_PERCENT_MAX;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 100.0f));
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.target);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax,
                          VP37_getPositionDemandMaxValue(pump));
}

// A limit is a share of the stroke: the full travel and anything below it
// down to, but not including, zero. The same rule gates the bench knob.
void test_vp37_physical_limit_is_a_share_of_the_stroke(void) {
  const float accepted[] = {(float)VP37_PERCENT_MAX, 50.5f, 0.1f,
                            (float)VP37_PHYSICAL_LIMIT_PERCENT};
  for (size_t i = 0U; i < COUNTOF(accepted); ++i) {
    TEST_ASSERT_TRUE(VP37_physicalLimitValid(accepted[i]));
  }
  const float rejected[] = {0.0f, -5.0f, 100.5f, 150.0f, NAN, INFINITY};
  for (size_t i = 0U; i < COUNTOF(rejected); ++i) {
    TEST_ASSERT_FALSE(VP37_physicalLimitValid(rejected[i]));
  }
}

void test_vp37_pid_time_update_setter(void) {
  VP37Pump *pump = vp37TestPump();
  pump->pidTimeUpdate = 45.0f;

  pump->pidTimeUpdate = 60.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, VP37_getVP37PIDTimeUpdate(pump));
}

void test_vp37_init_returns_already_initialized(void) {
  VP37Pump *pump = vp37TestPump();
  memset(pump, 0, sizeof(*pump));
  pump->vp37Initialized = true;

  VP37InitStatus status = VP37_init(pump);

  TEST_ASSERT_EQUAL_INT(VP37_INIT_ALREADY_INITIALIZED, status);
}

void test_vp37_init_returns_ok_when_baseline_ready(void) {
  VP37Pump *pump = vp37TestPump();
  memset(pump, 0, sizeof(*pump));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setCallbacks(pump, vp37ModuleCallbacks()));

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
  TEST_ASSERT_EQUAL_FLOAT((float)VP37_PHYSICAL_LIMIT_PERCENT,
                          pump->demand.physicalLimitPercent);
  TEST_ASSERT_EQUAL_INT32(100, pump->feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT32(8200, pump->feedback.adjustMax);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2U * VP37_CALIBRATION_MIN_SETTLE_MS,
                                      hal_millis());
}

void test_vp37_init_rejects_insufficient_calibration_travel(void) {
  VP37Pump *pump = vp37TestPump();
  memset(pump, 0, sizeof(*pump));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setCallbacks(pump, vp37ModuleCallbacks()));

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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  TEST_ASSERT_EQUAL_INT32(VP37_PWM_RESOLUTION, VP37_PWM_MAX);
}

void test_vp37_process_disables_after_adj_comm_cutoff_timeout(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);

  injectAdjRegisterData(4000, 144, 50, ADJ_STATUS_OK);
  hal_mock_set_millis(95);
  VP37_process(pump);
  vp37AdjustometerTransferBusy(true);

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

void test_vp37_init_without_board_services_leaves_the_pump_stopped(void) {
  VP37Pump *pump = vp37TestPump();
  memset(pump, 0, sizeof(*pump));
  injectAdjRegisterData(100, 132, 40, ADJ_STATUS_OK);

  TEST_ASSERT_EQUAL_INT(VP37_INIT_OUTPUT_UNAVAILABLE, VP37_init(pump));
  TEST_ASSERT_FALSE(pump->vp37Initialized);
  // The hardware entry points stay safe on a pump without services.
  VP37_stop(pump);
  VP37_setInjectionTiming(pump, 50);
  TEST_ASSERT_FALSE(VP37_isVP37Enabled(pump));
}

static void noQuantity(int32_t command) { (void)command; }
static void noTiming(int32_t command) { (void)command; }
static void noEnable(bool enabled) { (void)enabled; }
static bool notEnabled(void) { return false; }
static hal_status_t noAdjustometer(uint8_t reg, uint8_t *data, size_t len) {
  (void)reg;
  (void)data;
  (void)len;
  return HAL_EBUS;
}

void test_vp37_set_callbacks_checks_required_entries_and_fills_defaults(void) {
  VP37Pump *pump = vp37TestPump();
  memset(pump, 0, sizeof(*pump));
  VP37Callbacks table = {};
  table.writeQuantityPwm = noQuantity;
  table.writeTimingPwm = noTiming;
  table.setDriveEnabled = noEnable;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_setCallbacks(pump, &table));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_setCallbacks(pump, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_setCallbacks(NULL, &table));

  table.driveEnabled = notEnabled;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_setCallbacks(pump, &table));
  table.adjustometerTransfer = noAdjustometer;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setCallbacks(pump, &table));
  TEST_ASSERT_TRUE(pump->callbacks.driveAllowed());
  pump->callbacks.feedWatchdog();
  TEST_ASSERT_NULL(pump->callbacks.activeTestName());
  TEST_ASSERT_EQUAL_UINT32(0U, pump->callbacks.cyclicDelayMs());

  pump->vp37Initialized = true;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        VP37_setCallbacks(pump, vp37ModuleCallbacks()));
  TEST_ASSERT_TRUE(pump->callbacks.writeQuantityPwm == noQuantity);
}

void test_vp37_status_is_published_by_the_step_and_a_stop_once(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  injectAdjRegisterData(4000, 138, 41, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37Status status;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readStatus(pump, &status));
  TEST_ASSERT_TRUE(status.initialized);
  TEST_ASSERT_EQUAL_UINT32(1U, status.readCount);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 13.8f, status.supplyVolts);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 41.0f, status.fuelTempC);

  // A stop outside the step reaches the status with the next pass, once.
  VP37_stop(pump);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readStatus(pump, &status));
  TEST_ASSERT_TRUE(status.initialized);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readStatus(pump, &status));
  TEST_ASSERT_FALSE(status.initialized);
  const uint32_t sequence = pump->publishedSequence;
  VP37_process(pump);
  TEST_ASSERT_EQUAL_UINT32(sequence, pump->publishedSequence);
}

typedef struct {
  VP37TuningParam param;
  float min, max; /* max < 0: no upper bound below FLT_MAX */
  bool whole;
} TuningCase;

static const TuningCase kTuningCases[] = {
    {VP37_TUNING_KP, 0.0f, -1.0f, false},
    {VP37_TUNING_KI, 0.0f, -1.0f, false},
    {VP37_TUNING_KD, 0.0f, -1.0f, false},
    {VP37_TUNING_TOP_KD, 0.0f, VP37_PID_TOP_KD_MAX, false},
    {VP37_TUNING_PERIOD_MS, VP37_PID_TIME_UPDATE_MIN, VP37_PID_TIME_UPDATE_MAX,
     false},
    {VP37_TUNING_DERIVATIVE_FILTER, 0.0f, -1.0f, false},
    {VP37_TUNING_INTEGRAL_CAP, 0.0f, VP37_BENCH_INTEGRAL_LIMIT_MAX, false},
    {VP37_TUNING_TEMPERATURE_WEIGHT, 0.0f, 1.0f, false},
    {VP37_TUNING_HOLD_CONFIRM_MS, 0.0f,
     (float)VP37_INTEGRAL_HOLD_CONFIRM_MAX_MS, true},
    {VP37_TUNING_DEADBAND_TOP_HZ, 0.0f, VP37_PID_DEADBAND_TOP_MAX_HZ, false},
    {VP37_TUNING_MOTION_BOOST_UP, 0.0f, VP37_PWM_FF_MOTION_BOOST_MAX, false},
    {VP37_TUNING_MOTION_BOOST_DOWN, 0.0f, VP37_PWM_FF_MOTION_BOOST_MAX, false},
    {VP37_TUNING_TOP_ARRIVAL_DECEL, 0.0f,
     VP37_TOP_ARRIVAL_DECEL_MAX_PERCENT_PER_S2, false},
    {VP37_TUNING_MOTION_RATE_CAP, 0.0f,
     VP37_PWM_FF_MOTION_RATE_CAP_MAX_PERCENT_PER_S, false},
    {VP37_TUNING_CURRENT_OBSERVATION, 0.0f, 1.0f, true},
    {VP37_TUNING_CURRENT_FEEDBACK, 0.0f, 1.0f, true},
    {VP37_TUNING_MAP_TRIM, 0.0f, 1.0f, true},
    {VP37_TUNING_DRIVE_COMPENSATION, 0.0f, 1.0f, true},
    {VP37_TUNING_CYCLE_VOLTAGE, 0.0f, 1.0f, true},
    {VP37_TUNING_SUPPLY_FROZEN, 0.0f, 1.0f, true},
    {VP37_TUNING_HOLD_OFFSET_MS, 0.0f, (float)VP37_INTEGRAL_HOLD_OFFSET_MAX_MS,
     true},
    {VP37_TUNING_THERMAL_HOLD, 0.0f, 1.0f, true},
    {VP37_TUNING_HOLD_CURRENT, 0.0f, 1.0f, true},
    {VP37_TUNING_HOLD_MEAN_ENTER_HZ, VP37_INTEGRAL_HOLD_MEAN_ENTER_MIN_HZ,
     (float)VP37_INTEGRAL_HOLD_ENTER_HZ, false},
    {VP37_TUNING_RESEARCH_FREEZE, 0.0f, 3.0f, true},
    {VP37_TUNING_RESEARCH_MOTION_INTEGRAL, 0.0f, 1.0f, true},
    {VP37_TUNING_RESEARCH_SLOW_DAMPING, 0.0f, 1.0f, true},
    {VP37_TUNING_RESEARCH_REFERENCE_WEIGHT, 0.0f, 1.0f, false},
    {VP37_TUNING_RESEARCH_RAMP_DAMPING, 0.0f, 1.0f, true},
    {VP37_TUNING_RESEARCH_RAMP_BRAKING, 0.0f, 1.0f, true},
    {VP37_TUNING_RESEARCH_UPPER_DEADBAND, 0.0f, 1.0f, true},
    {VP37_TUNING_RESEARCH_DESCENT_UNWIND, 0.0f, 1.0f, true},
    {VP37_TUNING_RESEARCH_DESCENT_SHARE, .25f, .75f, false},
};

static void assertTuningRefused(VP37Pump *pump, VP37TuningParam param,
                                float value) {
  static VP37Pump before;
  before = *pump;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_setTuning(pump, param, value));
  TEST_ASSERT_EQUAL_MEMORY(&before, pump, sizeof(*pump));
}

void test_vp37_tuning_accepts_each_range_and_refuses_the_rest(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  TEST_ASSERT_EQUAL_size_t(VP37_TUNING_COUNT, COUNTOF(kTuningCases));
  for (size_t i = 0U; i < COUNTOF(kTuningCases); i++) {
    const TuningCase *c = &kTuningCases[i];
    if ((c->param == VP37_TUNING_RESEARCH_FREEZE) ||
        (c->param == VP37_TUNING_RESEARCH_MOTION_INTEGRAL)) {
      pump->pid.fullRangePrototype = true;
      pump->thermal.driveCompensationEnabled = true;
      pump->thermal.driveCompensationUsed = true;
      pump->thermal.driveResistanceReady = true;
      pump->thermal.scaleReady = true;
    }
    if ((c->param == VP37_TUNING_RESEARCH_SLOW_DAMPING) ||
        (c->param == VP37_TUNING_RESEARCH_RAMP_DAMPING) ||
        (c->param == VP37_TUNING_RESEARCH_UPPER_DEADBAND) ||
        (c->param == VP37_TUNING_RESEARCH_RAMP_BRAKING) ||
        (c->param == VP37_TUNING_RESEARCH_DESCENT_UNWIND) ||
        (c->param == VP37_TUNING_RESEARCH_DESCENT_SHARE) ||
        (c->param == VP37_TUNING_RESEARCH_REFERENCE_WEIGHT)) {
      (void)VP37_setTuning(pump, VP37_TUNING_KI, VP37_PID_KI);
      pump->pid.fullRangePrototype = true;
      pump->demand.atRest = true;
      pump->demand.requestedPercent = 0.0f;
      pump->output.finalPWM = 0;
    }
    const float top = (c->max < 0.0f) ? 1000.0f : c->max;
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setTuning(pump, c->param, top));
    TEST_ASSERT_EQUAL_FLOAT(top, VP37_getTuning(pump, c->param));
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setTuning(pump, c->param, c->min));
    TEST_ASSERT_EQUAL_FLOAT(c->min, VP37_getTuning(pump, c->param));
    assertTuningRefused(pump, c->param, c->min - 0.001f);
    assertTuningRefused(pump, c->param, NAN);
    assertTuningRefused(pump, c->param, INFINITY);
    if (c->max >= 0.0f) {
      assertTuningRefused(pump, c->param, c->max * 1.001f + 0.0001f);
    }
    if (c->whole) {
      assertTuningRefused(pump, c->param, 0.5f);
    }
  }
  assertTuningRefused(pump, VP37_TUNING_COUNT, 0.0f);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_setTuning(NULL, VP37_TUNING_KP, 0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_getTuning(NULL, VP37_TUNING_KP));
}

void test_vp37_tuning_side_effects_follow_the_setting(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  // One gain changes, the other two stay.
  VP37_setVP37PID(pump, .3f, .2f, .001f, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setTuning(pump, VP37_TUNING_KI, .4f));
  float kp = 0.0f;
  float ki = 0.0f;
  float kd = 0.0f;
  VP37_getVP37PIDValues(pump, &kp, &ki, &kd);
  TEST_ASSERT_EQUAL_FLOAT(.3f, kp);
  TEST_ASSERT_EQUAL_FLOAT(.4f, ki);
  TEST_ASSERT_EQUAL_FLOAT(.001f, kd);
  // Either map-trim switch clears what was learned.
  pump->feedforward.mapTrim[3] = 12.0f;
  pump->feedforward.mapTrimTransfers = 4U;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setTuning(pump, VP37_TUNING_MAP_TRIM, 1));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->feedforward.mapTrim[3]);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->feedforward.mapTrimTransfers);
  // Drive compensation off forgets the learned resistance.
  pump->thermal.driveSamples = 9U;
  pump->thermal.driveResistanceReady = true;
  pump->thermal.driveCorrection = 1.2f;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_DRIVE_COMPENSATION, 0));
  TEST_ASSERT_EQUAL_UINT32(0U, pump->thermal.driveSamples);
  TEST_ASSERT_FALSE(pump->thermal.driveResistanceReady);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pump->thermal.driveCorrection);
  // A new confirmation time drops a pending hold entry.
  pump->pid.integralHoldEnterPending = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_HOLD_CONFIRM_MS, 250));
  TEST_ASSERT_FALSE(pump->pid.integralHoldEnterPending);
  // A new offset time drops a pending offset release.
  pump->pid.integralHoldOffsetPending = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_HOLD_OFFSET_MS, 800));
  TEST_ASSERT_FALSE(pump->pid.integralHoldOffsetPending);
}

void test_vp37_tuning_reset_restores_defaults_and_keeps_switches(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  for (size_t i = 0U; i < COUNTOF(kTuningCases); i++) {
    const TuningCase *c = &kTuningCases[i];
    (void)VP37_setTuning(pump, c->param, (c->max < 0.0f) ? 7.0f : c->max);
  }
  // The widest mean entry band and an enabled switch are possible defaults.
  (void)VP37_setTuning(pump, VP37_TUNING_HOLD_MEAN_ENTER_HZ,
                       VP37_INTEGRAL_HOLD_MEAN_ENTER_MIN_HZ);
  (void)VP37_setTuning(pump, VP37_TUNING_HOLD_CURRENT,
                       VP37_THERMAL_HOLD_CURRENT_DEFAULT ? 0.0f : 1.0f);
  VP37_resetTuning(pump);
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KP, VP37_getTuning(pump, VP37_TUNING_KP));
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KI, VP37_getTuning(pump, VP37_TUNING_KI));
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KD, VP37_getTuning(pump, VP37_TUNING_KD));
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_TOP_KD,
                          VP37_getTuning(pump, VP37_TUNING_TOP_KD));
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_TIME_UPDATE,
                          VP37_getTuning(pump, VP37_TUNING_PERIOD_MS));
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_TF,
                          VP37_getTuning(pump, VP37_TUNING_DERIVATIVE_FILTER));
  TEST_ASSERT_EQUAL_FLOAT(VP37_BENCH_INTEGRAL_CAP_PWM,
                          VP37_getTuning(pump, VP37_TUNING_INTEGRAL_CAP));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,
                          VP37_getTuning(pump, VP37_TUNING_TEMPERATURE_WEIGHT));
  TEST_ASSERT_EQUAL_FLOAT((float)VP37_INTEGRAL_HOLD_CONFIRM_MS,
                          VP37_getTuning(pump, VP37_TUNING_HOLD_CONFIRM_MS));
  TEST_ASSERT_EQUAL_FLOAT((float)VP37_INTEGRAL_HOLD_OFFSET_MS,
                          VP37_getTuning(pump, VP37_TUNING_HOLD_OFFSET_MS));
  TEST_ASSERT_EQUAL_FLOAT(VP37_THERMAL_HOLD_DEFAULT ? 1.0f : 0.0f,
                          VP37_getTuning(pump, VP37_TUNING_THERMAL_HOLD));
  TEST_ASSERT_EQUAL_FLOAT(VP37_INTEGRAL_HOLD_MEAN_ENTER_HZ,
                          VP37_getTuning(pump, VP37_TUNING_HOLD_MEAN_ENTER_HZ));
  TEST_ASSERT_EQUAL_FLOAT(VP37_THERMAL_HOLD_CURRENT_DEFAULT ? 1.0f : 0.0f,
                          VP37_getTuning(pump, VP37_TUNING_HOLD_CURRENT));
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_DEADBAND_TOP_HZ,
                          VP37_getTuning(pump, VP37_TUNING_DEADBAND_TOP_HZ));
  TEST_ASSERT_EQUAL_FLOAT(VP37_PWM_FF_MOTION_BOOST_DEFAULT,
                          VP37_getTuning(pump, VP37_TUNING_MOTION_BOOST_UP));
  TEST_ASSERT_EQUAL_FLOAT(VP37_PWM_FF_DESCENT_BOOST,
                          VP37_getTuning(pump, VP37_TUNING_MOTION_BOOST_DOWN));
  TEST_ASSERT_EQUAL_FLOAT(VP37_TOP_ARRIVAL_DECEL_PERCENT_PER_S2,
                          VP37_getTuning(pump, VP37_TUNING_TOP_ARRIVAL_DECEL));
  TEST_ASSERT_EQUAL_FLOAT(VP37_PWM_FF_MOTION_RATE_CAP_PERCENT_PER_S,
                          VP37_getTuning(pump, VP37_TUNING_MOTION_RATE_CAP));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,
                          VP37_getTuning(pump, VP37_TUNING_CURRENT_FEEDBACK));
  TEST_ASSERT_EQUAL_FLOAT(
      1.0f, VP37_getTuning(pump, VP37_TUNING_CURRENT_OBSERVATION));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, VP37_getTuning(pump, VP37_TUNING_MAP_TRIM));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,
                          VP37_getTuning(pump, VP37_TUNING_SUPPLY_FROZEN));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,
                          VP37_getTuning(pump, VP37_TUNING_CYCLE_VOLTAGE));
  TEST_ASSERT_EQUAL_FLOAT(1.0f,
                          VP37_getTuning(pump, VP37_TUNING_DRIVE_COMPENSATION));
}

void test_vp37_is_ready_needs_a_running_calibrated_pump(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  TEST_ASSERT_TRUE(VP37_isReady(pump));
  pump->feedback.calibrationDone = false;
  TEST_ASSERT_FALSE(VP37_isReady(pump));
  pump->feedback.calibrationDone = true;
  VP37_stop(pump);
  TEST_ASSERT_FALSE(VP37_isReady(pump));
}

void test_vp37_board_services_stay_fixed_after_init_and_stop(void) {
  VP37Pump *pump = vp37TestPump();
  memset(pump, 0, sizeof(*pump));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setCallbacks(pump, vp37ModuleCallbacks()));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setAdjustometerFastFeedback(pump, true));
  // No Adjustometer answers: the start fails, but it has taken the services.
  vp37AdjustometerTransferBusy(true);
  TEST_ASSERT_EQUAL_INT(VP37_INIT_BASELINE_NOT_READY, VP37_init(pump));
  vp37AdjustometerTransferBusy(false);
  VP37_stop(pump);
  TEST_ASSERT_FALSE(pump->vp37Initialized);

  VP37Callbacks table = *vp37ModuleCallbacks();
  table.writeQuantityPwm = noQuantity;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, VP37_setCallbacks(pump, &table));
  TEST_ASSERT_TRUE(pump->callbacks.writeQuantityPwm ==
                   vp37ModuleCallbacks()->writeQuantityPwm);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        VP37_setAdjustometerFastFeedback(pump, false));
  TEST_ASSERT_TRUE(pump->adjustometer.fastFeedback);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        VP37_setAdjustometerFastFeedback(NULL, true));
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
    TEST_ASSERT_FLOAT_WITHIN(.001f,
                             VP37_NOMINAL_VOLTAGE / pump->supply.heldVolts,
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
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50.0f);

  injectAdjRegisterData(4600, 150U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  for (uint32_t ms = 5U; ms <= 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 80U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(.001f,
                             VP37_NOMINAL_VOLTAGE / pump->supply.heldVolts,
                             pump->supply.correction);
  }

  TEST_ASSERT_FLOAT_WITHIN(.03f, 8.0f, pump->supply.heldVolts);
  TEST_ASSERT_FLOAT_WITHIN(.001f, VP37_NOMINAL_VOLTAGE / pump->supply.heldVolts,
                           pump->supply.correction);
}

void test_vp37_voltage_filter_settles_at_center_of_local_ripple(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  TEST_ASSERT_LESS_THAN_FLOAT(VP37_NOMINAL_VOLTAGE / 17.0f,
                              pump->supply.correction);

  // Back inside the range the ordinary path resumes without a re-seed.
  runSupplyCycles(pump, ms, 40U, 4600, 145U, 14.5f);
  TEST_ASSERT_FALSE(pump->supply.overRange);
  TEST_ASSERT_FLOAT_WITHIN(.1f, 14.5f, pump->supply.heldVolts);
}

void test_vp37_voltage_scale_follows_gradual_supply_changes_closely(void) {
  // A 2 V/s ramp through the 50 ms filter lags by about 0.1 V; the old dead
  // band left up to 0.5 V to the integrator for seconds.
  VP37Pump *pump = vp37TestPump();
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
    TEST_ASSERT_FLOAT_WITHIN(.001f,
                             VP37_NOMINAL_VOLTAGE / pump->supply.heldVolts,
                             pump->supply.correction);
  }

  for (uint8_t voltage = 81U; voltage <= 150U; ++voltage) {
    runSupplyCycles(pump, ms, 10U, 4600, voltage, 0.0f);
    const float trueVolts = (float)voltage * .1f;
    TEST_ASSERT_FLOAT_WITHIN(.15f, trueVolts, pump->supply.heldVolts);
    TEST_ASSERT_FLOAT_WITHIN(.001f,
                             VP37_NOMINAL_VOLTAGE / pump->supply.heldVolts,
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
  bool constant = false;
  uint32_t pwmPhaseUs = 0U;
};

static uint16_t supplyScanRaw(float volts) {
  const float voltsPerCode =
      3.3f *
      ((float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_HIGH_OHMS +
       (float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_LOW_OHMS) /
      ((float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_LOW_OHMS * 4095.0f);
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
  if (fixture.constant) {
    return 12.0f;
  }
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
    const uint32_t phase =
        (sampleUs + periodUs - fixture.pwmPhaseUs) % periodUs;
    const uint32_t onTimeUs =
        pwm >= 0 ? (periodUs * (uint32_t)pwm) / VP37_PWM_RESOLUTION
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
  // The core-1 loop retains DMA blocks between the 5 ms position steps.
  TEST_ASSERT_EQUAL(HAL_OK, VP37_acquireCurrentScan(vp37TestPump()));
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
  hal_mock_adc_inject(JH_HW_NODE_VP37_PIN_SHUNT_ADC, 0);
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

void test_vp37_acquires_scan_between_position_steps(void) {
  VP37Pump *pump = vp37TestPump();
  const uint32_t start = 100000U;
  setupSupplyScan(pump, start);
  hal_mock_set_micros(start);
  injectAdjRegisterData(4600, 120U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  const uint32_t step = pump->controlSequence;
  static uint16_t
      frames[VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_PINS];
  const uint32_t blockUs =
      VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_FRAME_NS / 1000U;
  for (uint32_t i = 1U; i <= 2U; ++i) {
    hal_mock_set_micros(start + i * blockUs);
    TEST_ASSERT_EQUAL(HAL_OK, hal_mock_adc_scan_complete(
                                  frames, VP37_CURRENT_SCAN_BLOCK_FRAMES));
    VP37_process(pump);
    TEST_ASSERT_EQUAL_UINT32(i, pump->scan.blocks);
    TEST_ASSERT_EQUAL_UINT32(step, pump->controlSequence);
    TEST_ASSERT_EQUAL_UINT32(0U, pump->scan.cycleResultSequence);
  }
  hal_mock_set_micros(start + 5000U);
  injectAdjRegisterData(4600, 120U, 49U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_UINT32(step + 1U, pump->controlSequence);
  TEST_ASSERT_EQUAL_UINT32(1U, pump->scan.cycleResultSequence);
  TEST_ASSERT_EQUAL_UINT32(2U, pump->scan.blocks);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->scan.gaps);
}

static CurrentScanCommands runCurrentScan(float measuredAmps,
                                          bool changeDuringOff = false,
                                          bool resistanceCompensation = false) {
  VP37Pump *pump = vp37TestPump();
  // At200Hz, move PWM wraps2.5ms away from the5ms control clock. This
  // fixture specifically tests a10ms write during OFF, before the old ON.
  const bool shifted200 = changeDuringOff && (VP37_PWM_FREQUENCY_HZ == 200U);
  const SupplyScanFixture fixture = {100000U, false, false,
                                     false,   false, shifted200 ? 2500U : 0U};
  setupSupplyScan(pump, fixture.startUs);
  pump->currentControl.enabled = true;
  pump->thermal.driveCompensationEnabled = resistanceCompensation;
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
  // Observe the first completed ON after the OFF-phase write. The next ON
  // already belongs to that newer write with the earlier fall publication.
  const uint32_t lastStepUs =
      changeDuringOff ? (shifted200 ? 15000U : 20000U) : 25000U;
  const uint32_t periodUs = 1000000U / VP37_PWM_FREQUENCY_HZ;
  const uint32_t writePhaseUs =
      (10000U + periodUs - fixture.pwmPhaseUs) % periodUs;
  if (changeDuringOff) {
    const uint32_t onTimeUs =
        periodUs * (uint32_t)commands.baselinePWM / VP37_PWM_RESOLUTION;
    TEST_ASSERT_LESS_THAN_UINT32(periodUs - onTimeUs, writePhaseUs);
  }
  for (uint32_t us = 5000U; us <= lastStepUs; us += 5000U) {
    while (nextBlockUs <= us) {
      completeSupplyScan(fixture, nextBlockUs, measuredAmps,
                         commands.baselinePWM, true);
      nextBlockUs += blockUs;
    }
    hal_mock_set_micros(fixture.startUs + us);
    if (changeDuringOff && (us == 10000U)) {
      // The fixture verifies that this instant precedes the ON rise.
      // This request arrives during OFF and applies at the following wrap.
      // Keep the new duty within the matcher's unchanged24count tolerance,
      // so timestamp ownership must distinguish the two current targets.
      TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(
                                    pump, shifted200 ? 50.5f : 51.0f));
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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

void test_vp37_resistance_scan_uses_the_command_that_produced_the_current(
    void) {
  const CurrentScanCommands commands = runCurrentScan(3.5f, true, true);
  const VP37Pump *pump = vp37TestPump();
  TEST_ASSERT_TRUE(pump->thermal.cycleValid);
  TEST_ASSERT_EQUAL_INT32(commands.baselinePWM, pump->thermal.cycleDrive);
  TEST_ASSERT_NOT_EQUAL(commands.pendingPWM, pump->thermal.cycleDrive);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->thermal.driveSamples);
}

static void advanceCachedScan(VP37Pump *pump, const SupplyScanFixture &fixture,
                              uint32_t *completedUs, uint32_t untilUs) {
  const uint32_t blockUs =
      VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_FRAME_NS / 1000U;
  while ((*completedUs + blockUs) <= untilUs) {
    *completedUs += blockUs;
    completeSupplyScan(fixture, *completedUs, 3.5f, 700, true);
    (void)VP37_serviceCurrentScan(pump);
  }
}

static uint32_t cacheCurrentScan(VP37Pump *pump, uint32_t writtenUs) {
  const SupplyScanFixture fixture = {100000U, false, false, false, true};
  setupSupplyScan(pump, fixture.startUs);
  pump->output.finalPWM = 700;
  VP37_recordCurrentCommand(pump, 820.0f, 700, writtenUs);
  uint32_t completedUs = 0U;
  advanceCachedScan(pump, fixture, &completedUs, 24000U);
  TEST_ASSERT_TRUE(pump->currentControl.matchCached);
  TEST_ASSERT_TRUE(pump->currentControl.matchFound);
  TEST_ASSERT_EQUAL_PTR(&pump->currentControl.matchedCommand,
                        VP37_matchCurrentSample(pump, hal_micros()));
  TEST_ASSERT_EQUAL_UINT32(writtenUs,
                           pump->currentControl.matchedCommand.writtenUs);
  return completedUs;
}

void test_vp37_current_scan_cache_rechecks_capture_age_and_quality(void) {
  VP37Pump *pump = vp37TestPump();
  // Keep the command newer than the capture-age boundary. The separate
  // historical-command test below checks the opposite expiration order.
  cacheCurrentScan(pump, VP37_PWM_FREQUENCY_HZ == 200U ? 112500U : 106000U);
  VP37CurrentPulseResult &sample = pump->scan.cycleResult;
  const uint32_t expiresUs = sample.cycleStartUs + sample.onTimeUs / 2U +
                             VP37_CURRENT_CONTROL_MAX_AGE_US;
  TEST_ASSERT_LESS_THAN_UINT32(pump->currentControl.matchedCommand.writtenUs +
                                   VP37_CURRENT_CONTROL_MAX_AGE_US +
                                   2U * sample.latchPeriodUs,
                               expiresUs);
  TEST_ASSERT_NOT_NULL(VP37_matchCurrentSample(pump, expiresUs - 1U));
  TEST_ASSERT_NULL(VP37_matchCurrentSample(pump, expiresUs));
  sample.waveformValid = false;
  TEST_ASSERT_NULL(VP37_matchCurrentSample(pump, hal_micros()));
  sample.waveformValid = true;
  sample.latchedPwm = pump->currentControl.matchedCommand.pwm +
                      VP37_CURRENT_CONTROL_DUTY_TOLERANCE + 1;
  TEST_ASSERT_NULL(VP37_matchCurrentSample(pump, hal_micros()));
}

void test_vp37_current_scan_cache_rechecks_historical_command_age(void) {
  VP37Pump *pump = vp37TestPump();
  cacheCurrentScan(pump, 98000U);
  const VP37CurrentPulseResult &sample = pump->scan.cycleResult;
  const uint32_t expiresUs = pump->currentControl.matchedCommand.writtenUs +
                             VP37_CURRENT_CONTROL_MAX_AGE_US +
                             2U * sample.latchPeriodUs;
  const uint32_t sampleAge =
      expiresUs - (sample.cycleStartUs + sample.onTimeUs / 2U);
  TEST_ASSERT_LESS_THAN_UINT32(VP37_CURRENT_CONTROL_MAX_AGE_US, sampleAge);
  TEST_ASSERT_NOT_NULL(VP37_matchCurrentSample(pump, expiresUs - 1U));
  TEST_ASSERT_NULL(VP37_matchCurrentSample(pump, expiresUs));
}

void test_vp37_current_scan_cache_follows_sequence_command_and_reset(void) {
  VP37Pump *pump = vp37TestPump();
  uint32_t completedUs = cacheCurrentScan(pump, 100000U);
  const SupplyScanFixture fixture = {100000U, false, false, false, true};
  const uint32_t oldSequence = pump->scan.cycleResultSequence;
  VP37_recordCurrentCommand(pump, 900.0f, 700, hal_micros());
  TEST_ASSERT_FALSE(pump->currentControl.matchCached);
  const VP37CurrentCommand *matched =
      VP37_matchCurrentSample(pump, hal_micros());
  TEST_ASSERT_NOT_NULL(matched);
  TEST_ASSERT_EQUAL_UINT32(100000U, matched->writtenUs);

  // Publication trails the PWM latch. Later blocks must refresh the cache
  // even when no new command has been recorded since the previous block.
  advanceCachedScan(pump, fixture, &completedUs, 30000U);
  TEST_ASSERT_TRUE(pump->currentControl.matchCached);
  TEST_ASSERT_EQUAL_FLOAT(820.0f,
                          pump->currentControl.matchedCommand.nominalPwm);
  advanceCachedScan(pump, fixture, &completedUs, 36000U);
  TEST_ASSERT_EQUAL_FLOAT(900.0f,
                          pump->currentControl.matchedCommand.nominalPwm);
  // A command inconsistent with the observed duty must cache a miss.
  VP37_recordCurrentCommand(pump, 1000.0f, 1000, hal_micros());
  advanceCachedScan(pump, fixture, &completedUs, 48000U);
  TEST_ASSERT_TRUE(pump->currentControl.matchCached);
  TEST_ASSERT_FALSE(pump->currentControl.matchFound);
  TEST_ASSERT_NULL(VP37_matchCurrentSample(pump, hal_micros()));
  // A later valid period must replace that cached miss.
  VP37_recordCurrentCommand(pump, 900.0f, 700, hal_micros());
  advanceCachedScan(pump, fixture, &completedUs, 64000U);
  TEST_ASSERT_GREATER_THAN_UINT32(oldSequence, pump->scan.cycleResultSequence);
  matched = VP37_matchCurrentSample(pump, hal_micros());
  TEST_ASSERT_NOT_NULL(matched);
  TEST_ASSERT_EQUAL_FLOAT(900.0f, matched->nominalPwm);
  TEST_ASSERT_EQUAL_PTR(&pump->currentControl.matchedCommand, matched);
  VP37_resetCurrentControl(pump);
  TEST_ASSERT_FALSE(pump->currentControl.matchCached);
  TEST_ASSERT_NULL(VP37_matchCurrentSample(pump, hal_micros()));
}

void test_vp37_resistance_scan_keeps_onset_capture_rejected_after_settling(
    void) {
  VP37Pump *pump = vp37TestPump();
  const SupplyScanFixture fixture = {100000U, false, false, false, true};
  setupSupplyScan(pump, fixture.startUs);
  pump->thermal.driveCompensationEnabled = true;
  const uint32_t blockUs =
      VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_FRAME_NS / 1000U;
  uint32_t nextBlockUs = blockUs;
  int32_t pwm = 0;
  bool becameSettled = false;
  for (uint32_t us = 0U; us <= 450000U; us += 5000U) {
    while (nextBlockUs <= us) {
      const float amps =
          (float)pwm * 12.0f /
          ((float)VP37_PWM_RESOLUTION * VP37_DRIVE_REFERENCE_OHMS);
      completeSupplyScan(fixture, nextBlockUs, amps, pwm, true);
      nextBlockUs += blockUs;
    }
    hal_mock_set_micros(fixture.startUs + us);
    injectAdjRegisterData(4600, 120U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    pwm = pump->output.finalPWM;
    if (!becameSettled && pump->currentControl.driveSettled) {
      becameSettled = true;
      // This DMA result predates the end of the quiet interval. Processing
      // it after that interval must not retroactively make it a hold sample.
      TEST_ASSERT_TRUE(pump->thermal.cycleValid);
      TEST_ASSERT_FALSE(pump->thermal.cycleSettled);
      TEST_ASSERT_FALSE(pump->thermal.driveLearning);
      TEST_ASSERT_EQUAL_UINT32(0U, pump->thermal.driveSamples);
    }
  }
  TEST_ASSERT_TRUE(becameSettled);
  TEST_ASSERT_TRUE(pump->thermal.cycleSettled);
  TEST_ASSERT_GREATER_THAN_UINT32(0U, pump->thermal.driveSamples);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, VP37_DRIVE_REFERENCE_OHMS,
                           pump->thermal.driveResistance);
}

static void runSupplyScanProfile(const SupplyScanFixture &fixture) {
  VP37Pump *pump = vp37TestPump();
  setupSupplyScan(pump, fixture.startUs);
  const uint32_t blockUs =
      VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_FRAME_NS / 1000U;
  uint32_t nextBlockUs = blockUs;
  float referenceDrive = 0.0f;
  float minimumVolts = 100.0f;
  float maximumVolts = 0.0f;
  int32_t minimumPWM = VP37_PWM_RESOLUTION;
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  int32_t minimumPWM = VP37_PWM_RESOLUTION;
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, .001f);
  injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.desired);
  TEST_ASSERT_FALSE(pump->demand.atRest);
  TEST_ASSERT_GREATER_THAN_INT32(0, pump->output.finalPWM);
}

void test_vp37_feedback_fault_at_rest_still_latches_drive_off(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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

#if ECU_FUNCTIONAL_TESTS_ENABLED
void test_vp37_trace_preserves_consecutive_steps_until_drained(void) {
  static_assert(sizeof(VP37TraceSample) <= 148U,
                "Trace padding must not consume the RAM function budget");
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50);
  VP37TraceSample sample = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_startTrace(pump));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, VP37_startTrace(pump));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_readTrace(&sample));
  for (uint32_t i = 1U; i <= VP37_TRACE_SAMPLES + 4U; i++) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData((int16_t)(4000U + i), 144, 26, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FALSE(VP37_traceCapturing());
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, VP37_startTrace(pump));
  for (uint32_t i = 1U; i <= VP37_TRACE_SAMPLES; i++) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(&sample));
    TEST_ASSERT_EQUAL_UINT32(i, sample.sequence);
    TEST_ASSERT_EQUAL_UINT32(i * 5000U, sample.us);
    TEST_ASSERT_EQUAL_UINT32(5000U, sample.dt);
    TEST_ASSERT_EQUAL_INT32(4000U + i, sample.measured);
  }
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readTrace(&sample));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_startTrace(pump));
  hal_mock_set_millis((VP37_TRACE_SAMPLES + 5U) * 5U);
  pump->supply.ready = true;
  pump->supply.heldVolts = 15.0f;
  pump->supply.correction = .8f;
  injectAdjRegisterData(4700, 80, 26, ADJ_STATUS_OK);
  VP37_process(pump);
  const float expectedVoltageCorrection = pump->supply.correction;
  pump->vp37Initialized = false;
  // The control core completes the partial capture of a stopped pump.
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_readTrace(&sample));
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(&sample));
  TEST_ASSERT_EQUAL_INT32(4700, sample.measured);
  TEST_ASSERT_FLOAT_WITHIN(.0001f, expectedVoltageCorrection,
                           sample.voltageCorrection);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readTrace(&sample));
}

void test_vp37_trace_stopped_before_a_sample_frees_the_buffer(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37TraceSample sample = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_startTrace(pump));
  VP37_stop(pump);
  VP37_process(pump);
  TEST_ASSERT_FALSE(VP37_traceCapturing());
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readTrace(&sample));

  pump->vp37Initialized = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_startTrace(pump));
  VP37_stop(pump);
  VP37_process(pump);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readTrace(&sample));
}
#endif

void test_vp37_integral_authority_is_independent_of_ki(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump, true);
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
  vp37AdjustometerTransferBusy(true);
  hal_mock_set_micros(105000);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_FLOAT(integral, pump->pid.terms.integral);
  vp37AdjustometerTransferBusy(false);
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
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(&trace));
  TEST_ASSERT_EQUAL_UINT32(2U, trace.sequence);
  TEST_ASSERT_FALSE(trace.fresh);
  TEST_ASSERT_NOT_EQUAL(HAL_OK, trace.readStatus);
  TEST_ASSERT_EQUAL_UINT32(0U, trace.pidDtUs);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(&trace));
  TEST_ASSERT_EQUAL_UINT32(3U, trace.sequence);
  TEST_ASSERT_TRUE(trace.fresh);
  TEST_ASSERT_EQUAL_UINT32(10000U, trace.pidDtUs);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readTrace(&trace));
  TEST_ASSERT_EQUAL_UINT32(4U, trace.sequence);
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_SIGNAL_LOST, trace.status);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, VP37_readTrace(&trace));
#endif
}

void test_vp37_temperature_scales_unsaturated_ff_and_pid_together(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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

static float learnNegativeHoldingTrim(VP37Pump *pump, uint32_t &ms) {
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0.05f, 0.2f, 0.0f, true);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 75.0f));
  const int16_t target = (int16_t)pump->demand.target;
  ms = 0U;
  runSupplyCycles(pump, ms, 585U, target + 200, 144U, 0.0f);
  const float learned = pump->pid.terms.integral;
  TEST_ASSERT_LESS_THAN_FLOAT(-100.0f, learned);
  TEST_ASSERT_GREATER_THAN_FLOAT(-120.0f, learned);
  return learned;
}

void test_vp37_climb_floor_preserves_learned_negative_trim_at_target(void) {
  VP37Pump *pump = vp37TestPump();
  uint32_t ms;
  const float learned = learnNegativeHoldingTrim(pump, ms);
  const int16_t target = (int16_t)pump->demand.target;

  const float currentCorrections[] = {0.0f, -35.0f};
  for (size_t i = 0U; i < COUNTOF(currentCorrections); ++i) {
    float minimum = 10000.0f;
    float maximum = -10000.0f;
    for (int16_t offset = -2; offset <= 2; ++offset) {
      // The same current trim starts each sample; without a new capture its
      // normal decay is identical on both sides of the position target.
      pump->currentControl.correctionPwm = currentCorrections[i];
      runSupplyCycles(pump, ms, 1U, target + offset, 144U, 0.0f);
      TEST_ASSERT_FLOAT_WITHIN(0.01f, learned, pump->pid.terms.integral);
      TEST_ASSERT_FALSE(pump->pid.terms.saturated_low);
      TEST_ASSERT_FALSE(pump->pid.softFloorActive);
      TEST_ASSERT_LESS_THAN_FLOAT(-100.0f, pump->pid.correction);
      const float command =
          pump->output.pwmValue + pump->currentControl.correctionPwm;
      minimum = fminf(minimum, command);
      maximum = fmaxf(maximum, command);
    }
    TEST_ASSERT_LESS_THAN_FLOAT(0.3f, maximum - minimum);
  }
}

void test_vp37_climb_floor_keeps_acceleration_reserve_for_moving_demand(void) {
  VP37Pump *pump = vp37TestPump();
  uint32_t ms;
  (void)learnNegativeHoldingTrim(pump, ms);
  for (uint32_t step = 0U; step < 6U; ++step) {
    TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(
                                  pump, 80.0f + 3.0f * (float)step));
    const int16_t position = (int16_t)(pump->demand.desired - 2);
    pump->currentControl.correctionPwm = -35.0f;
    runSupplyCycles(pump, ms, 1U, position, 144U, 0.0f);
    TEST_ASSERT_LESS_THAN_INT32(pump->demand.desired, pump->feedback.position);
    TEST_ASSERT_LESS_THAN_INT32(pump->demand.target, pump->demand.desired);
    TEST_ASSERT_FALSE(hal_millis_deadline_expired(pump->demand.targetChangedMs,
                                                  VP37_TARGET_STABLE_MS));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -(float)VP37_PWM_FF_SOFT_FLOOR_MARGIN,
                             pump->pid.negativeLimit +
                                 pump->currentControl.correctionPwm);
    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(
        pump->feedforward.pwm - (float)VP37_PWM_FF_SOFT_FLOOR_MARGIN - 0.001f,
        pump->output.pwmValue + pump->currentControl.correctionPwm);
  }
}

void test_vp37_integral_hold_rejects_bias_and_releases_on_persistent_error(
    void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  // The large-error release alone; the offset release has its own tests.
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_HOLD_OFFSET_MS, 0));
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
  // Recovery into the entry band cancels the pending release.
  injectAdjRegisterData(4588, 144, 49, ADJ_STATUS_OK);
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

  // Back inside the entry band (10 Hz short) the hold engages again after the
  // confirmation time and keeps the integral it had.
  for (uint32_t ms = 3110U; ms <= 3210U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4590, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_EQUAL(ms >= 3210U, pump->pid.integralHold);
  }
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  const float heldIntegral = pump->pid.terms.integral;
  for (uint32_t ms = 3215U; ms <= 3400U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4590, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, heldIntegral, pump->pid.terms.integral);
}

static void checkIntegralHoldReleaseWithExitBandNoise(int32_t direction) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  // The large-error release alone; the offset release has its own tests.
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_HOLD_OFFSET_MS, 0));
  VP37_setPositionDemandPercentage(pump, 50);
  for (uint32_t ms = 5U; ms <= 200U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->pid.integralHold);

  // A biased measurement occasionally dips below the release threshold,
  // but never recovers into the settled-position band.
  for (uint32_t ms = 205U; ms <= 700U; ms += 5U) {
    const int32_t error = direction * ((ms % 20U == 0U) ? 39 : 50);
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)(4600 - error), 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->pid.integralHold);
    TEST_ASSERT_TRUE(pump->pid.integralHoldReleasePending);
    TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
  }
  hal_mock_set_millis(705U);
  injectAdjRegisterData((int16_t)(4600 - direction * 39), 144, 49,
                        ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  TEST_ASSERT_FALSE(pump->pid.integralHoldReleasePending);
  TEST_ASSERT_GREATER_THAN_FLOAT(0, direction * pump->pid.terms.integral);
}

void test_vp37_integral_hold_releases_positive_drift_with_exit_band_noise(
    void) {
  checkIntegralHoldReleaseWithExitBandNoise(1);
}

void test_vp37_integral_hold_releases_negative_drift_with_exit_band_noise(
    void) {
  checkIntegralHoldReleaseWithExitBandNoise(-1);
}

void test_vp37_integral_hold_requires_continuous_time_near_target(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  // The instantaneous band alone; the mean entry has its own test.
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_HOLD_OFFSET_MS, 0));
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

/** @brief A process-test pump; helpers that set it up more than once per test
 * release the previous controller first, tearDown releases the last one. */
static VP37Pump *freshProcessPump(void) {
  VP37Pump *pump = vp37TestPump();
  if (pump->pid.controller != NULL) {
    hal_pid_controller_destroy(pump->pid.controller);
    pump->pid.controller = NULL;
  }
  setupPumpForProcessTests(pump);
  return pump;
}

/** @brief Settle at 50 % (4600 Hz) into the integral hold. */
static VP37Pump *settleIntoIntegralHold(uint32_t offsetReleaseMs,
                                        uint32_t &ms) {
  VP37Pump *pump = freshProcessPump();
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setTuning(pump, VP37_TUNING_HOLD_OFFSET_MS,
                                               (float)offsetReleaseMs));
  VP37_setPositionDemandPercentage(pump, 50);
  for (ms = 5U; ms <= 500U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  return pump;
}

void test_vp37_integral_hold_offset_release_removes_standing_offset(void) {
  uint32_t ms = 0U;
  VP37Pump *pump = settleIntoIntegralHold(1000U, ms);
  // 25 Hz short sits inside the large-error band; only the mean releases it.
  // The 0.3 s mean leaves the entry band about 200 ms after the step.
  const uint32_t start = ms;
  for (; ms <= start + 1500U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4575, 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    if (ms <= start + 1150U) {
      TEST_ASSERT_TRUE(pump->pid.integralHold);
      TEST_ASSERT_FALSE(pump->pid.integralHoldReleasePending);
      TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
    }
    if (ms >= start + 1250U) {
      TEST_ASSERT_FALSE(pump->pid.integralHold);
    }
  }
  // Short of the target, the resumed integral pushes the actuator up.
  TEST_ASSERT_GREATER_THAN_FLOAT(0, pump->pid.terms.integral);
}

void test_vp37_integral_hold_keeps_standing_offset_without_offset_release(
    void) {
  uint32_t ms = 0U;
  VP37Pump *pump = settleIntoIntegralHold(0U, ms);
  const uint32_t start = ms;
  for (; ms <= start + 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4575, 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->pid.integralHold);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0, pump->pid.terms.integral);
}

void test_vp37_integral_hold_offset_release_needs_a_persistent_mean(void) {
  uint32_t ms = 0U;
  VP37Pump *pump = settleIntoIntegralHold(1000U, ms);
  // Noise around the target, up to 20 Hz either way, averages out.
  const uint32_t start = ms;
  for (; ms <= start + 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    const int16_t noise = ((ms / 5U) % 2U == 0U) ? 20 : -20;
    injectAdjRegisterData((int16_t)(4600 + noise), 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->pid.integralHold);
    TEST_ASSERT_FALSE(pump->pid.integralHoldOffsetPending);
  }
}

void test_vp37_integral_hold_offset_release_stays_off_on_the_upper_stroke(
    void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_HOLD_OFFSET_MS, 1000));
  pump->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  VP37_setPositionDemandPercentage(pump, 100);
  const int16_t target = (int16_t)pump->demand.target;
  uint32_t ms = 5U;
  for (; ms <= 2000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(target, 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  TEST_ASSERT_GREATER_THAN_FLOAT((float)VP37_PID_DEADBAND,
                                 pump->pid.integralDeadbandHz);
  // The widened dead zone keeps a 25 Hz offset held: integration there walks
  // the mechanism into stick-slip.
  const float held = pump->pid.terms.integral;
  for (const uint32_t start = ms; ms <= start + 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)(target - 25), 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->pid.integralHold);
    TEST_ASSERT_FALSE(pump->pid.integralHoldOffsetPending);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, held, pump->pid.terms.integral);
}

static uint32_t holdEntryAfterArrival(uint32_t offsetReleaseMs) {
  VP37Pump *pump = freshProcessPump();
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setTuning(pump, VP37_TUNING_HOLD_OFFSET_MS,
                                               (float)offsetReleaseMs));
  VP37_setPositionDemandPercentage(pump, 50);
  uint32_t ms = 5U;
  // The settled target is still 300 Hz away for half a second...
  for (; ms <= 500U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4300, 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FALSE(pump->pid.integralHold);
  }
  // ...then the actuator passes the target, as the end of an arrival does.
  const uint32_t arrival = ms;
  for (; ms <= arrival + 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
    if (pump->pid.integralHold) {
      break;
    }
  }
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  return ms - arrival;
}

/** @brief Follow the demand @p shortHz short of it for @p forMs. */
static void standBelowTarget(VP37Pump *pump, int16_t shortHz, uint32_t forMs,
                             uint32_t &ms) {
  for (const uint32_t start = ms; ms <= start + forMs; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)(pump->demand.desired - shortHz), 144U, 49U,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  TEST_ASSERT_EQUAL_INT32(shortHz, pump->pid.error);
}

/** @brief Settle at 40 %, then move the target to 50 % and stand 9 Hz short
 * of it for 2 s, with the offset release on. */
static VP37Pump *standShortAfterAMove(float meanEnterHz, uint32_t &ms) {
  VP37Pump *pump = freshProcessPump();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_HOLD_OFFSET_MS, 1000));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      VP37_setTuning(pump, VP37_TUNING_HOLD_MEAN_ENTER_HZ, meanEnterHz));
  VP37_setPositionDemandPercentage(pump, 40);
  standBelowTarget(pump, 0, 500U, ms);
  VP37_setPositionDemandPercentage(pump, 50);
  standBelowTarget(pump, 9, 2000U, ms);
  return pump;
}

void test_vp37_integral_hold_mean_entry_band_waits_for_a_closer_mean(void) {
  uint32_t ms = 5U;
  // 9 Hz short is inside the full entry band: the hold engages...
  VP37Pump *pump = standShortAfterAMove((float)VP37_INTEGRAL_HOLD_ENTER_HZ, ms);
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  // ...but not with a 6 Hz mean entry band, until the mean comes closer.
  pump = standShortAfterAMove(6.0f, ms);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  standBelowTarget(pump, 3, 2000U, ms);
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  // Back at 9 Hz the mean is still inside the full band: the hold stays.
  standBelowTarget(pump, 9, 3000U, ms);
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  TEST_ASSERT_FALSE(pump->pid.integralHoldOffsetPending);
}

void test_vp37_integral_hold_mean_entry_band_stays_off_on_the_upper_stroke(
    void) {
  VP37Pump *pump = freshProcessPump();
  pump->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_HOLD_OFFSET_MS, 1000));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_HOLD_MEAN_ENTER_HZ,
                                       VP37_INTEGRAL_HOLD_MEAN_ENTER_MIN_HZ));
  VP37_setPositionDemandPercentage(pump, 100);
  const int16_t target = (int16_t)pump->demand.target;
  for (uint32_t ms = 5U; ms <= 2000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)(target - 9), 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  // Above the taper start the full entry band decides.
  TEST_ASSERT_GREATER_THAN_FLOAT((float)VP37_PID_DEADBAND,
                                 pump->pid.integralDeadbandHz);
  TEST_ASSERT_TRUE(pump->pid.integralHold);
}

void test_vp37_integral_hold_waits_for_the_mean_after_an_arrival(void) {
  // Alone, the instantaneous band holds after the confirmation time.
  TEST_ASSERT_UINT32_WITHIN(10U, VP37_INTEGRAL_HOLD_CONFIRM_MS,
                            holdEntryAfterArrival(0U));
  // With the offset release, the mean of the arrival must reach the mean
  // entry band first: it decays from about 245 Hz with the filter time
  // constant, then the dwell follows.
  const float reachMs = VP37_INTEGRAL_HOLD_MEAN_FILTER_S * 1000.0f *
                        logf(245.0f / VP37_INTEGRAL_HOLD_MEAN_ENTER_HZ);
  TEST_ASSERT_UINT32_WITHIN(150U,
                            (uint32_t)reachMs + VP37_INTEGRAL_HOLD_CONFIRM_MS,
                            holdEntryAfterArrival(1000U));
}

/** @brief Multiplier change over 3 s settled at another fuel temperature. */
static float thermalScaleChangeInHold(bool thermalHold, uint8_t fuelTempC) {
  uint32_t ms = 0U;
  VP37Pump *pump = settleIntoIntegralHold(0U, ms);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setTuning(pump, VP37_TUNING_THERMAL_HOLD,
                                               thermalHold ? 1.0f : 0.0f));
  const float before = pump->thermal.scale;
  for (const uint32_t start = ms; ms <= start + 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 144U, fuelTempC, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_EQUAL(thermalHold, pump->thermal.scaleHeld);
  }
  return pump->thermal.scale - before;
}

void test_vp37_thermal_hold_keeps_the_multiplier_from_falling(void) {
  // Cold fuel lowers the source the way the estimate settles after a rising
  // approach.
  TEST_ASSERT_LESS_THAN_FLOAT(-0.005f, thermalScaleChangeInHold(false, 20U));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, thermalScaleChangeInHold(true, 20U));
}

void test_vp37_thermal_hold_follows_a_rise_above_the_lowest_value(void) {
  uint32_t ms = 0U;
  VP37Pump *pump = settleIntoIntegralHold(0U, ms);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_THERMAL_HOLD, 1));
  const float arrived = pump->thermal.scale;
  float lowest = pump->thermal.temperatureCorrection;
  // The source settles down first: the multiplier stays where it arrived...
  for (const uint32_t start = ms; ms <= start + 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 144U, 20U, ADJ_STATUS_OK);
    VP37_process(pump);
    lowest = fminf(lowest, pump->thermal.temperatureCorrection);
    TEST_ASSERT_TRUE(pump->thermal.scaleHeld);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, arrived, pump->thermal.scale);
  // ...then the coil heats: the multiplier rises by as much as the source has
  // risen above its lowest value.
  for (const uint32_t start = ms; ms <= start + 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 144U, 90U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->thermal.scaleHeld);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(arrived + 0.005f, pump->thermal.scale);
  TEST_ASSERT_FLOAT_WITHIN(
      1e-6f, arrived + (pump->thermal.temperatureCorrection - lowest),
      pump->thermal.scale);
}

void test_vp37_thermal_hold_waits_for_the_position_to_arrive(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_THERMAL_HOLD, 1));
  VP37_setPositionDemandPercentage(pump, 50);
  uint32_t ms = 5U;
  for (; ms <= 300U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4300, 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  // The target is settled, but the actuator is still 300 Hz short: the
  // multiplier keeps following the cooling fuel down.
  const float approaching = pump->thermal.scale;
  for (const uint32_t start = ms; ms <= start + 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4300, 144U, 20U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FALSE(pump->thermal.scaleArrived);
  }
  TEST_ASSERT_LESS_THAN_FLOAT(approaching, pump->thermal.scale);
  // Within the large-error band the position has arrived; the multiplier
  // stops falling from then on, even when the position leaves the band again.
  hal_mock_set_millis(ms);
  injectAdjRegisterData(4570, 144U, 20U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->thermal.scaleArrived);
  const float arrived = pump->thermal.scale;
  for (const uint32_t start = ms += 5U; ms <= start + 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4500, 144U, 20U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->thermal.scaleArrived);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, arrived, pump->thermal.scale);
  // A new target starts over.
  VP37_setPositionDemandPercentage(pump, 60);
  hal_mock_set_millis(ms);
  injectAdjRegisterData(4500, 144U, 20U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->thermal.scaleArrived);
}

void test_vp37_thermal_hold_stays_off_on_the_upper_stroke(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_THERMAL_HOLD, 1));
  VP37_setPositionDemandPercentage(pump, 100);
  const int16_t target = (int16_t)pump->demand.target;
  uint32_t ms = 5U;
  for (; ms <= 2000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(target, 144U, 49U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  // Settled, but above the taper start: the multiplier follows cooling fuel.
  const float before = pump->thermal.scale;
  for (const uint32_t start = ms; ms <= start + 3000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(target, 144U, 20U, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FALSE(pump->thermal.scaleHeld);
  }
  TEST_ASSERT_LESS_THAN_FLOAT(before - 0.005f, pump->thermal.scale);
}

/** @brief Current target per unscaled command count of one more settled
 * step with the switch set. */
static float currentTargetWith(VP37Pump *pump, bool holdCurrent, uint32_t &ms) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setTuning(pump, VP37_TUNING_HOLD_CURRENT,
                                               holdCurrent ? 1.0f : 0.0f));
  hal_mock_set_millis(ms += 5U);
  injectAdjRegisterData(4600, 144U, 20U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->output.pwmValue);
  return pump->currentControl.targetAmps / pump->output.pwmValue;
}

void test_vp37_thermal_hold_current_target_keeps_the_held_excess(void) {
  uint32_t ms = 0U;
  VP37Pump *pump = settleIntoIntegralHold(0U, ms);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_THERMAL_HOLD, 1));
  // Cooling fuel lowers the source while the held multiplier stays.
  for (const uint32_t start = ms; ms <= start + 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 144U, 20U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->thermal.scaleHeld);
  const float plain = currentTargetWith(pump, false, ms);
  const float kept = currentTargetWith(pump, true, ms);
  const float excess =
      pump->thermal.scale / pump->thermal.temperatureCorrection;
  TEST_ASSERT_GREATER_THAN_FLOAT(1.005f, excess);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, excess, kept / plain);
}

void test_vp37_thermal_hold_current_target_is_plain_without_the_hold(void) {
  uint32_t ms = 0U;
  VP37Pump *pump = settleIntoIntegralHold(0U, ms);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_THERMAL_HOLD, 0));
  for (const uint32_t start = ms; ms <= start + 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 144U, 20U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FALSE(pump->thermal.scaleHeld);
  const float plain = currentTargetWith(pump, false, ms);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, plain, currentTargetWith(pump, true, ms));
}

void test_vp37_thermal_hold_resumes_when_the_target_moves(void) {
  uint32_t ms = 0U;
  VP37Pump *pump = settleIntoIntegralHold(0U, ms);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_THERMAL_HOLD, 1));
  for (const uint32_t start = ms; ms <= start + 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 144U, 20U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  const float held = pump->thermal.scale;
  TEST_ASSERT_TRUE(pump->thermal.scaleHeld);
  // The cooled source sits well below the held multiplier...
  TEST_ASSERT_LESS_THAN_FLOAT(held - 0.005f,
                              pump->thermal.temperatureCorrection);
  // ...and a new target ends the hold: the multiplier takes the source at
  // once, then follows it again.
  VP37_setPositionDemandPercentage(pump, 55);
  hal_mock_set_millis(ms += 5U);
  injectAdjRegisterData(4600, 144U, 20U, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->thermal.scaleArrived);
  TEST_ASSERT_FALSE(pump->thermal.scaleHeld);
  TEST_ASSERT_EQUAL_FLOAT(pump->thermal.temperatureCorrection,
                          pump->thermal.scale);
  const float resumed = pump->thermal.scale;
  for (const uint32_t start = ms; ms <= start + 1500U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData(4600, 144U, 20U, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  // The cooling source falls on; the multiplier follows within its slew.
  TEST_ASSERT_FALSE(pump->thermal.scaleHeld);
  TEST_ASSERT_LESS_THAN_FLOAT(resumed, pump->thermal.scale);
  TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(pump->thermal.temperatureCorrection,
                                     pump->thermal.scale);
  TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(
      resumed - (VP37_THERMAL_SCALE_SLEW_PER_S * 1.5f) - 1e-4f,
      pump->thermal.scale);
}

void test_vp37_supply_change_does_not_freeze_integral(void) {
  // The supply is scaled out of the command before the actuator, so a drop
  // is no reason to stop integrating a real position error.
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  // The top wall has its own tests; here only the upper slew is measured.
  pump->demand.topArrivalDecel = 0.0f;
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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

void test_upper_reference_increase_keeps_positive_incremental_drive(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->feedback.adjustMin = 0;
  pump->feedback.adjustMax = 10000;
  pump->feedforward.riseBlend = 0.0f;
  pump->feedforward.fallBlend = 0.0f;
  float previous = -INFINITY;
  for (int32_t position = 7500; position <= 9000; ++position) {
    const float drive =
        VP37_feedForward(pump, position) + .05f * (float)(position - 7500);
    TEST_ASSERT_TRUE(drive >= previous);
    previous = drive;
  }
}

void test_vp37_motion_feedforward_brakes_when_upper_ramp_stops(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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

// First control step after a demand step 20 % -> `percent` from a settled
// 20 %. `tracked` changes the target 5 ms earlier, as a ramp that keeps moving
// does.
static void firstStepAfterDemandStep(float percent, bool tracked,
                                     float *advance, float *riseBlend) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  // Every control step below comes 5 ms after the previous one.
  uint32_t ms = 0U;
  hal_mock_set_millis(ms);
  VP37_setPositionDemandPercentage(pump, 20.0f);
  for (; ms < 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  if (tracked) {
    VP37_setPositionDemandPercentage(pump, 21.0f);
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    ms += 5U;
  }
  const float before = pump->demand.desiredPosition;
  VP37_setPositionDemandPercentage(pump, percent);
  hal_mock_set_millis(ms);
  injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  *advance = pump->demand.desiredPosition - before;
  *riseBlend = pump->feedforward.riseBlend;
}

void test_vp37_single_demand_step_is_a_standing_target_from_its_first_cycle(
    void) {
  const float travel = 9000.0f;
  const float dt = .005f;
  const float blend = dt / (VP37_PWM_FF_MOTION_FILTER_S + dt);
  float advance = 0.0f;
  float rise = 0.0f;
  // A lone step ramps at the standing rate with the standing share of assist;
  // rev90 gave its first 25 ms the moving rate and the full assist. The step
  // is long enough that the arrival brake does not bind yet.
  firstStepAfterDemandStep(80.0f, false, &advance, &rise);
  TEST_ASSERT_FLOAT_WITHIN(
      1.0f, travel * VP37_STATIONARY_SLEW_PERCENT_PER_SECOND * .01f * dt,
      advance);
  TEST_ASSERT_FLOAT_WITHIN(.01f,
                           VP37_STATIONARY_RISE_WEIGHT *
                               (VP37_STATIONARY_SLEW_PERCENT_PER_SECOND /
                                VP37_PWM_FF_MOTION_REFERENCE_RATE) *
                               blend,
                           rise);
}

// Calibration as the init test scripts it: baseline, stable MIN, stable MAX.
static void initCalibratedPump(VP37Pump *pump) {
  memset(pump, 0, sizeof(*pump));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setCallbacks(pump, vp37ModuleCallbacks()));
  AdjustometerScript script = {};
  appendAdjRegisterData(&script, 100, 132, 40, ADJ_STATUS_OK);
  appendAdjRegisterDataRepeated(&script, 10, 100, 132, 40, ADJ_STATUS_OK);
  appendAdjRegisterDataRepeated(&script, 10, 8200, 132, 40, ADJ_STATUS_OK);
  appendAdjRegisterData(&script, 8200, 132, 40, ADJ_STATUS_OK);
  injectAdjustometerScript(&script);
  TEST_ASSERT_EQUAL_INT(VP37_INIT_OK, VP37_init(pump));
  VP37_setVP37PID(pump, 0, 0, 0, true);
}

void test_vp37_first_demand_after_init_ramps_from_the_bottom(void) {
  VP37Pump *pump = vp37TestPump();
  initCalibratedPump(pump);
  // The first step is measured far from its target, so the target sits on the
  // full travel whatever the default limit; the limit has its own tests.
  pump->demand.physicalLimitPercent = (float)VP37_PERCENT_MAX;
  const float travel = 8100.0f;
  const float standingStep =
      travel * VP37_STATIONARY_SLEW_PERCENT_PER_SECOND * .01f * .005f;
  // A pedal already pressed at start: the first demand is a lone step from
  // the calibrated bottom, where calibration left the actuator, not a jump
  // to its target.
  VP37_setPositionDemandPercentage(pump, 40.0f);
  injectAdjRegisterData(100, 132, 40, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FALSE(pump->demand.targetMoving);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f + standingStep,
                           pump->demand.desiredPosition);
  TEST_ASSERT_INT32_WITHIN(1, (int32_t)standingStep, pump->pid.error);
  TEST_ASSERT_TRUE(pump->demand.desired < pump->demand.target);
  TEST_ASSERT_FLOAT_WITHIN(1.0f,
                           expectedHoldingFF(standingStep / travel * 100.0f),
                           pump->feedforward.pwm - pump->feedforward.motion);
}

void test_vp37_zero_first_demand_after_init_rests_at_once(void) {
  VP37Pump *pump = vp37TestPump();
  initCalibratedPump(pump);
  // Zero demand at start releases the drive in its first cycle even while
  // the feedback still shows the actuator settling above the bottom.
  VP37_setPositionDemandPercentage(pump, 0.0f);
  injectAdjRegisterData(700, 132, 40, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_TRUE(pump->demand.atRest);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
}

void test_vp37_tracked_demand_keeps_the_moving_rate_and_assist(void) {
  const float travel = 9000.0f;
  const float dt = .005f;
  float advance = 0.0f;
  float rise = 0.0f;
  // 10 % from the target the arrival brake would allow less than half of this.
  firstStepAfterDemandStep(30.0f, true, &advance, &rise);
  TEST_ASSERT_FLOAT_WITHIN(
      1.0f, travel * VP37_DESIRED_SLEW_PERCENT_PER_SECOND * .01f * dt, advance);
  TEST_ASSERT_GREATER_THAN_FLOAT(.5f, rise);
}

/** @brief Settled rise blend of a tracked ramp running at the full slew,
 * after 200 ms of a target that keeps moving. */
static float trackedRiseBlendAtFullSlew(VP37Pump *pump) {
  uint32_t ms = 0U;
  hal_mock_set_millis(ms);
  VP37_setPositionDemandPercentage(pump, 10.0f);
  for (; ms < 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  // A demand far above the ramp, refreshed every cycle: the ramp runs at
  // 300 %/s below the upper stroke and the target counts as tracked.
  for (uint32_t i = 0U; i < 40U; i++) {
    VP37_setPositionDemandPercentage(pump, ((i % 2U) == 0U) ? 70.0f : 69.9f);
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    ms += 5U;
  }
  TEST_ASSERT_TRUE(pump->demand.targetMoving);
  return pump->feedforward.riseBlend;
}

void test_vp37_upward_assist_saturates_above_the_rate_cap(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  // At the 300 %/s slew the assist stops at the 250 %/s level, below the
  // slew's own share.
  const float capped = trackedRiseBlendAtFullSlew(pump);
  TEST_ASSERT_FLOAT_WITHIN(.02f,
                           VP37_PWM_FF_MOTION_RATE_CAP_PERCENT_PER_S /
                               VP37_PWM_FF_MOTION_REFERENCE_RATE,
                           capped);
  TEST_ASSERT_TRUE(capped < VP37_DESIRED_SLEW_PERCENT_PER_SECOND /
                                    VP37_PWM_FF_MOTION_REFERENCE_RATE -
                                .1f);
  // Off: the assist follows the full slew, as before the cap.
  hal_pid_controller_destroy(pump->pid.controller);
  pump->pid.controller = NULL;
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  pump->feedforward.motionRateCap = 0.0f;
  TEST_ASSERT_FLOAT_WITHIN(.02f,
                           VP37_DESIRED_SLEW_PERCENT_PER_SECOND /
                               VP37_PWM_FF_MOTION_REFERENCE_RATE,
                           trackedRiseBlendAtFullSlew(pump));
}

void test_vp37_standing_ramp_brakes_to_rest_at_its_target(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  uint32_t ms = 0U;
  hal_mock_set_millis(ms);
  VP37_setPositionDemandPercentage(pump, 20.0f);
  for (; ms < 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  const float travel =
      (float)(pump->feedback.adjustMax - pump->feedback.adjustMin);
  const float decel = travel * VP37_ARRIVAL_DECEL_PERCENT_PER_S2 * .01f;
  const float slewStep =
      travel * VP37_STATIONARY_SLEW_PERCENT_PER_SECOND * .01f * .005f;
  VP37_setPositionDemandPercentage(pump, 80.0f);
  uint32_t braked = 0U;
  float lastAdvance = 0.0f;
  for (uint32_t i = 0U;
       (i < 400U) && (pump->demand.desired != pump->demand.target); i++) {
    const float before = pump->demand.desiredPosition;
    const float limit =
        sqrtf(2.0f * decel * ((float)pump->demand.target - before)) * .005f;
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    ms += 5U;
    lastAdvance = pump->demand.desiredPosition - before;
    TEST_ASSERT_TRUE(lastAdvance <= slewStep + .01f);
    if (limit < slewStep) {
      // The brake binds: exactly the speed that still stops at the target.
      TEST_ASSERT_FLOAT_WITHIN(
          .01f, fminf(limit, (float)pump->demand.target - before), lastAdvance);
      braked++;
    }
  }
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  TEST_ASSERT_GREATER_THAN_UINT32(10U, braked);
  TEST_ASSERT_TRUE(lastAdvance < .1f * slewStep);
}

/** @brief Drive a tracked target at the top of the range: it changes by one
 * count every cycle, so the ramp never sees a standing target. Returns the
 * number of cycles in which the top brake bound. */
static uint32_t trackedRampToTop(VP37Pump *pump, uint32_t *ms,
                                 float *lastAdvance) {
  const float travel =
      (float)(pump->feedback.adjustMax - pump->feedback.adjustMin);
  const int32_t top = VP37_getPositionDemandMaxValue(pump);
  const float decel = travel * pump->demand.topArrivalDecel * .01f;
  uint32_t braked = 0U;
  // The first change after a long hold is a lone step; a second change
  // within the settle time makes the target tracked for the loop below.
  (void)VP37_setPositionDemandValue(pump, top - 1);
  hal_mock_set_millis(*ms);
  injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  *ms += 5U;
  for (uint32_t i = 0U; (i < 400U) && (pump->demand.desired < top - 1); i++) {
    // Alternate between the top and one count below it: a moving target.
    (void)VP37_setPositionDemandValue(pump, ((i % 2U) == 0U) ? top : top - 1);
    const float before = pump->demand.desiredPosition;
    const float upperStart =
        (float)pump->feedback.adjustMin +
        travel * (VP37_DESIRED_UPPER_SLEW_START_PERCENT * .01f);
    const float rate = (before >= upperStart)
                           ? VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND
                           : VP37_DESIRED_SLEW_PERCENT_PER_SECOND;
    const float slewStep = travel * rate * .01f * .005f;
    const float limit =
        (decel > 0.0f) ? sqrtf(2.0f * decel * ((float)top - before)) * .005f
                       : slewStep;
    hal_mock_set_millis(*ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    *ms += 5U;
    TEST_ASSERT_TRUE(pump->demand.targetMoving);
    *lastAdvance = pump->demand.desiredPosition - before;
    TEST_ASSERT_TRUE(*lastAdvance <= slewStep + .01f);
    if (limit < slewStep) {
      // Exactly the speed that still stops at the top, target or not.
      TEST_ASSERT_FLOAT_WITHIN(
          .01f, fminf(limit, (float)pump->demand.target - before),
          *lastAdvance);
      braked++;
    } else {
      TEST_ASSERT_FLOAT_WITHIN(
          .01f, fminf(slewStep, (float)pump->demand.target - before),
          *lastAdvance);
    }
  }
  return braked;
}

void test_vp37_tracked_ramp_brakes_into_the_top_of_the_range(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->demand.topArrivalDecel = 750.0f;
  VP37_setVP37PID(pump, 0, 0, 0, true);
  uint32_t ms = 0U;
  hal_mock_set_millis(ms);
  VP37_setPositionDemandPercentage(pump, 20.0f);
  for (; ms < 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  float lastAdvance = 0.0f;
  const uint32_t braked = trackedRampToTop(pump, &ms, &lastAdvance);
  // The wall binds over the upper part of the travel and the ramp creeps in.
  TEST_ASSERT_GREATER_THAN_UINT32(20U, braked);
  TEST_ASSERT_TRUE(lastAdvance < 10.0f);
  TEST_ASSERT_TRUE(pump->demand.desired >=
                   VP37_getPositionDemandMaxValue(pump) - 1);
}

void test_vp37_top_brake_off_keeps_the_moving_rate_to_the_top(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  pump->demand.topArrivalDecel = 0.0f;
  uint32_t ms = 0U;
  hal_mock_set_millis(ms);
  VP37_setPositionDemandPercentage(pump, 20.0f);
  for (; ms < 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  float lastAdvance = 0.0f;
  const uint32_t braked = trackedRampToTop(pump, &ms, &lastAdvance);
  TEST_ASSERT_EQUAL_UINT32(0U, braked);
  TEST_ASSERT_TRUE(pump->demand.desired >=
                   VP37_getPositionDemandMaxValue(pump) - 1);
}

// The upper-stroke rules at 80..100 % of the travel under the given limit:
// upward assist at 80 %, integral authority and dead zone at 100 %, settled D
// at 90 %. Every site is checked: a rule gated by the limit shows up (rev98
// gated them at limits below 90 % and a fast ramp then overshot the 86 % cap
// by a tenth of the stroke).
static void checkUpperRules(float limit) {
  VP37Pump *pump = vp37TestPump();
  // Called several times per test: release the previous fixture's controller.
  if (pump->pid.controller != NULL) {
    hal_pid_controller_destroy(pump->pid.controller);
    pump->pid.controller = NULL;
  }
  setupPumpForProcessTests(pump);
  pump->demand.physicalLimitPercent = limit;
  const float travel =
      (float)(pump->feedback.adjustMax - pump->feedback.adjustMin);
  pump->feedforward.riseBlend = 1.0f;
  pump->feedforward.fallBlend = 0.0f;
  (void)VP37_feedForward(pump,
                         pump->feedback.adjustMin + (int32_t)(travel * .8f));
  // The map's motion column at 80 %, halved by the taper when it applies.
  TEST_ASSERT_FLOAT_WITHIN(.001f, 12.266667f, pump->feedforward.motion);
  pump->demand.desiredPosition = (float)pump->feedback.adjustMax;
  TEST_ASSERT_FLOAT_WITHIN(.01f,
                           VP37_INTEGRAL_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS - 1U]
                                                  [VP37_TAPER_COL_VALUE],
                           VP37_integralLimit(pump));
  pump->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  TEST_ASSERT_FLOAT_WITHIN(.01f, VP37_PID_DEADBAND_TOP_HZ,
                           VP37_integralDeadband(pump));
  pump->pid.topKd = .002f;
  pump->pidDtUs = 5000U;
  pump->demand.atRest = false;
  pump->demand.desiredPosition =
      (float)pump->feedback.adjustMin + (travel * .9f);
  for (uint32_t i = 0U; i < 200U; ++i) {
    VP37_updateDerivativeGain(pump, true);
  }
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .002f, pump->pid.effectiveKd);
}

void test_vp37_upper_stroke_rules_apply_under_any_physical_limit(void) {
  checkUpperRules(85.0f);
  checkUpperRules(86.0f);
  checkUpperRules(89.9f);
  checkUpperRules((float)VP37_PHYSICAL_LIMIT_PERCENT);
  checkUpperRules((float)VP37_PERCENT_MAX);
}

// First step of a tracked rise from a settled 80 % of the travel; a demand in
// counts stays below any limit's top.
static float firstTrackedStepFrom80Percent(float limit) {
  VP37Pump *pump = vp37TestPump();
  if (pump->pid.controller != NULL) {
    hal_pid_controller_destroy(pump->pid.controller);
    pump->pid.controller = NULL;
  }
  setupPumpForProcessTests(pump);
  // The top wall has its own tests; here only the upper slew is measured.
  pump->demand.topArrivalDecel = 0.0f;
  VP37_setVP37PID(pump, 0, 0, 0, true);
  pump->demand.physicalLimitPercent = limit;
  uint32_t ms = 0U;
  hal_mock_set_millis(ms);
  VP37_setPositionDemandValue(pump, 7300);
  for (; ms < 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(7300, pump->demand.desired);
  VP37_setPositionDemandValue(pump, 7400);
  hal_mock_set_millis(ms);
  injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  ms += 5U;
  const float before = pump->demand.desiredPosition;
  VP37_setPositionDemandValue(pump, 7800);
  hal_mock_set_millis(ms);
  injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  return pump->demand.desiredPosition - before;
}

void test_vp37_upper_slew_applies_under_any_physical_limit(void) {
  const float travel = 9000.0f;
  const float dt = .005f;
  const float upperStep =
      travel * VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND * .01f * dt;
  TEST_ASSERT_FLOAT_WITHIN(.5f, upperStep,
                           firstTrackedStepFrom80Percent(86.0f));
  TEST_ASSERT_FLOAT_WITHIN(.5f, upperStep,
                           firstTrackedStepFrom80Percent(90.0f));
}

void test_vp37_lone_falling_step_keeps_the_moving_assist_for_the_settle_time(
    void) {
  const float dt = .005f;
  const float blend = dt / (VP37_PWM_FF_MOTION_FILTER_S + dt);
  const float fullFall =
      VP37_DESIRED_SLEW_PERCENT_PER_SECOND / VP37_PWM_FF_MOTION_REFERENCE_RATE;
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  uint32_t ms = 0U;
  hal_mock_set_millis(ms);
  VP37_setPositionDemandPercentage(pump, 90.0f);
  for (; ms < 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  // A lone falling step: the full descent assist from its first cycle, as
  // rev90 gave every step; a standing step would blend only the standing
  // share (0.3) of it.
  VP37_setPositionDemandPercentage(pump, 20.0f);
  hal_mock_set_millis(ms);
  injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(.01f, fullFall * blend, pump->feedforward.fallBlend);
  // After the settle time the step stands and the assist fades to the
  // standing share, well before the 233 ms descent ends.
  for (ms += 5U; ms < 1100U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_NOT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
  TEST_ASSERT_FLOAT_WITHIN(.05f, VP37_STATIONARY_FALL_WEIGHT * fullFall,
                           pump->feedforward.fallBlend);
}

void test_vp37_falling_standing_ramp_is_not_braked(void) {
  const float travel = 9000.0f;
  const float dt = .005f;
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0, 0, 0, true);
  uint32_t ms = 0U;
  hal_mock_set_millis(ms);
  VP37_setPositionDemandPercentage(pump, 30.0f);
  for (; ms < 1000U; ms += 5U) {
    hal_mock_set_millis(ms);
    injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49,
                          ADJ_STATUS_OK);
    VP37_process(pump);
  }
  const float before = pump->demand.desiredPosition;
  VP37_setPositionDemandPercentage(pump, 20.0f);
  hal_mock_set_millis(ms);
  injectAdjRegisterData((int16_t)pump->demand.desired, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  // A braked descent from 10 % above would allow less than half of this.
  TEST_ASSERT_FLOAT_WITHIN(
      1.0f, -travel * VP37_DESIRED_SLEW_PERCENT_PER_SECOND * .01f * dt,
      pump->demand.desiredPosition - before);
}

void test_vp37_descent_feedforward_lowers_the_command_while_the_target_falls(
    void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 80);
  injectAdjRegisterData(7300, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setPositionDemandPercentage(pump, 100);
  const float travel =
      (float)(pump->feedback.adjustMax - pump->feedback.adjustMin);
  const float decel = travel * VP37_ARRIVAL_DECEL_PERCENT_PER_S2 * .01f;
  for (uint32_t ms = 5; ms <= 250; ms += 5) {
    hal_mock_set_millis(ms);
    const int32_t previous = pump->demand.desired;
    const float brakedStep =
        sqrtf(2.0f * decel *
              ((float)pump->demand.target - pump->demand.desiredPosition)) *
        .005f;
    injectAdjRegisterData((int16_t)previous, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    const int32_t step = pump->demand.desired - previous;
    if (ms == 5) {
      // A moving target keeps the upper slew rate until it becomes stationary.
      TEST_ASSERT_INT32_WITHIN(
          1,
          (int32_t)(travel * VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND * .01f *
                    .005f),
          step);
    }
    if (ms == 30) {
      // Standing now, and close enough that the arrival brake binds.
      TEST_ASSERT_INT32_WITHIN(1, (int32_t)brakedStep, step);
      TEST_ASSERT_LESS_THAN_INT32(
          (int32_t)(travel * VP37_STATIONARY_UPPER_SLEW_PERCENT_PER_SECOND *
                    .01f * .005f),
          step);
    }
  }
  TEST_ASSERT_EQUAL_INT32(pump->demand.target, pump->demand.desired);
}

void test_vp37_derivative_opposes_rebound_within_one_control_step(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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

void test_vp37_upper_derivative_requires_an_explicit_gain(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.topKd);
  pump->demand.atRest = false;
  pump->demand.desiredPosition =
      (float)pump->feedback.adjustMin +
      .9f * (float)(pump->feedback.adjustMax - pump->feedback.adjustMin);
  pump->pidDtUs = 5000U;
  for (unsigned i = 0U; i < 200U; ++i) {
    VP37_updateDerivativeGain(pump, true);
  }
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.effectiveKd);
  pump->pid.topKd = .001f;
  for (unsigned i = 0U; i < 200U; ++i) {
    VP37_updateDerivativeGain(pump, true);
  }
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .001f, pump->pid.effectiveKd);
}

void test_vp37_upper_derivative_survives_62ms_target_updates_without_steps(
    void) {
  VP37Pump *pump = vp37TestPump();
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

void test_vp37_upper_derivative_opposes_both_directions_of_motion(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->demand.topArrivalDecel);
  uint32_t reachedMs = 0U;
  for (uint32_t ms = 0; ms <= 900; ++ms) {
    hal_mock_set_millis(ms);
    // A separately specified input ramp, sampled by the 5 ms controller.
    const float target = ms < 400U ? (float)(ms / 4U) : 100.0f;
    VP37_setPositionDemandPercentage(pump, target);
    injectAdjRegisterData((int16_t)pump->demand.target, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    if (ms % 5U == 0U) {
      TEST_ASSERT_TRUE(pump->demand.desired <= pump->demand.target + 1);
      TEST_ASSERT_INT32_WITHIN(90, pump->demand.target, pump->demand.desired);
    }
    if ((reachedMs == 0U) &&
        (pump->demand.desired == pump->feedback.adjustMax)) {
      reachedMs = ms;
    }
  }
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.desired);
  TEST_ASSERT_UINT32_WITHIN(5U, 400U, reachedMs);
}

// ── Measured drive-path compensation ─────────────────────────────────────────

static void feedDriveObservation(VP37Pump *pump, float ohms, float volts) {
  const int32_t drive = pump->output.finalPWM;
  pump->thermal.cycleDrive = drive;
  pump->thermal.cyclePwm = drive;
  pump->thermal.cycleVolts = volts;
  pump->thermal.cycleAmps =
      ((float)drive / (float)VP37_PWM_RESOLUTION) * volts / ohms;
  pump->thermal.cycleUs = hal_micros();
  pump->thermal.cycleValid = true;
  pump->thermal.cycleSettled = VP37_driveIsSettled(pump, hal_micros());
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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

void test_vp37_measured_drive_learns_while_position_moves_once_ready(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 50.0f));
  uint32_t ms = 0U;
  runDriveCycles(pump, ms, 1400U, 1.32f, true);
  TEST_ASSERT_TRUE(pump->thermal.driveResistanceReady);
  const float learned = pump->thermal.driveResistance;
  const uint32_t acceptedMs = pump->thermal.driveUpdatedMs;
  const uint32_t acceptedCount = pump->thermal.driveLearnedSamples;
  // A ready estimate follows matched captures taken in motion at the motion
  // filter rate; the feedback read lags the injected position by one step, so
  // at most the first capture still counts as quiet.
  const float expected =
      learned +
      ((1.8f - learned) * (1.0f - expf(-0.5f / VP37_DRIVE_FILTER_MOTION_S)));
  for (uint32_t i = 0U; i < 100U; ++i) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData((i % 2U) == 0U ? 4600 : 4200, 144U, 29U,
                          ADJ_STATUS_OK);
    feedDriveObservation(pump, 1.8f, 14.0f);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->thermal.driveLearning);
    TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  }
  TEST_ASSERT_FALSE(pump->thermal.cycleSettled);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, expected, pump->thermal.driveResistance);
  TEST_ASSERT_GREATER_THAN_UINT32(acceptedMs, pump->thermal.driveUpdatedMs);
  TEST_ASSERT_EQUAL_UINT32(acceptedCount + 100U,
                           pump->thermal.driveLearnedSamples);
  // Quiet captures pull it back to the coil's value at the quiet filter.
  runDriveCycles(pump, ms, 1600U, 1.32f, true);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.32f, pump->thermal.driveResistance);
}

void test_vp37_measured_drive_does_not_learn_in_motion_before_ready(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->thermal.driveCompensationEnabled = true;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 50.0f));
  uint32_t ms = 0U;
  // A first estimate must come from quiet captures: motion alone never builds
  // one, however long it lasts and however healthy the matched observations.
  for (uint32_t i = 0U; i < 1400U; ++i) {
    ms += 5U;
    hal_mock_set_millis(ms);
    injectAdjRegisterData((i % 2U) == 0U ? 4600 : 4200, 144U, 29U,
                          ADJ_STATUS_OK);
    feedDriveObservation(pump, 1.32f, 14.0f);
    VP37_process(pump);
    TEST_ASSERT_FALSE(pump->thermal.driveLearning);
  }
  TEST_ASSERT_EQUAL_UINT32(0U, pump->thermal.driveSamples);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->thermal.driveLearnedSamples);
  TEST_ASSERT_FALSE(pump->thermal.driveResistanceReady);
  TEST_ASSERT_FALSE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_EQUAL_FLOAT(VP37_DRIVE_REFERENCE_OHMS,
                          pump->thermal.driveResistance);
}

void test_vp37_measured_drive_rejects_stale_and_mismatched_captures(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setVP37PID(pump, 0.0f, 0.0f, 0.0f, false);
  pump->thermal.driveCompensationEnabled = true;
  VP37_setPositionDemandPercentage(pump, 50.0f);
  uint32_t ms = 0U;
  // Qualify the quiet rail and command before testing duplicate handling.
  runDriveCycles(pump, ms, 80U, 1.32f, false);
  TEST_ASSERT_TRUE(VP37_driveIsSettled(pump, hal_micros()));
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
  VP37Pump *pump = vp37TestPump();
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

void test_vp37_measured_drive_detects_accumulated_small_voltage_changes(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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

// The dead zone bends at the map's third knot: a gentle slope from the taper
// start to the bend, a steep one from the bend to the top. The position loop
// walks the same table the knots come from, so the test reads them there and
// checks the slopes between them, not any one tuning value. The extra knot
// must not move the integral authority off its straight taper.
void test_vp37_dead_zone_bends_at_the_map_knot(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  const float travel =
      (float)(pump->feedback.adjustMax - pump->feedback.adjustMin);
  const auto at = [&](float percent) {
    pump->demand.desiredPosition =
        (float)pump->feedback.adjustMin + (travel * percent / 100.0f);
  };
  const auto knot = [](uint32_t row, uint32_t column) {
    return VP37_INTEGRAL_DEADBAND_MAP[row][column];
  };
  const float start = knot(1U, VP37_TAPER_COL_PERCENT);
  const float bend = knot(2U, VP37_TAPER_COL_PERCENT);
  const float top = knot(3U, VP37_TAPER_COL_PERCENT);
  const auto gentle = [&](float percent) {
    return knot(1U, VP37_TAPER_COL_VALUE) +
           ((percent - start) / (bend - start)) *
               (knot(2U, VP37_TAPER_COL_VALUE) -
                knot(1U, VP37_TAPER_COL_VALUE));
  };
  const auto steep = [&](float percent) {
    return knot(2U, VP37_TAPER_COL_VALUE) +
           ((percent - bend) / (top - bend)) * (knot(3U, VP37_TAPER_COL_VALUE) -
                                                knot(2U, VP37_TAPER_COL_VALUE));
  };
  TEST_ASSERT_TRUE(start < bend);
  TEST_ASSERT_TRUE(bend < top);

  at(start);
  TEST_ASSERT_EQUAL_FLOAT((float)VP37_PID_DEADBAND,
                          VP37_integralDeadband(pump));
  const float belowBend = (start + bend) / 2.0f;
  at(belowBend);
  TEST_ASSERT_FLOAT_WITHIN(.5f, gentle(belowBend), VP37_integralDeadband(pump));
  at(bend);
  TEST_ASSERT_FLOAT_WITHIN(.5f, gentle(bend), VP37_integralDeadband(pump));
  const float aboveBend = (bend + top) / 2.0f;
  at(aboveBend);
  TEST_ASSERT_FLOAT_WITHIN(.5f, steep(aboveBend), VP37_integralDeadband(pump));
  // Steep means steeper: the straight line through the gentle slope falls
  // short of the zone above the bend.
  TEST_ASSERT_TRUE(gentle(aboveBend) < steep(aboveBend));
  at(top);
  TEST_ASSERT_FLOAT_WITHIN(.5f, VP37_PID_DEADBAND_TOP_HZ,
                           VP37_integralDeadband(pump));

  // Integral authority: 120 to the taper start, then 3 PWM per percent.
  for (uint32_t step = 0U; step <= 25U; step++) {
    at(75.0f + (float)step);
    TEST_ASSERT_FLOAT_WITHIN(.01f, 120.0f - (3.0f * (float)step),
                             VP37_integralLimit(pump));
  }
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
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  uint32_t ms = 0U;
  VP37_setPositionDemandPercentage(pump, 100);
  // Ramp to the top, then sit 100 Hz short of it: inside the widened dead
  // zone, which already stops integration, but outside the fixed hold entry
  // band. A hold here would only let that error stand until twice the zone.
  holdAt(pump, ms, 400U, (int16_t)(pump->feedback.adjustMax - 100));
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMax, pump->demand.desired);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, VP37_PID_DEADBAND_TOP_HZ,
                           pump->pid.integralDeadbandHz);
  holdAt(pump, ms, 60U, (int16_t)(pump->feedback.adjustMax - 10));
  TEST_ASSERT_TRUE(pump->pid.integralHold);

  // Below the taper the same 12 Hz entry applies: 15 Hz short never engages
  // the hold (a 20 Hz entry did and froze braked arrivals short), 10 Hz does.
  VP37_setPositionDemandPercentage(pump, 50);
  const int32_t target = pump->demand.target;
  holdAt(pump, ms, 600U, (int16_t)(target - 15));
  TEST_ASSERT_EQUAL_INT32(target, pump->demand.desired);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  holdAt(pump, ms, 60U, (int16_t)(target - 10));
  TEST_ASSERT_TRUE(pump->pid.integralHold);
}

void test_vp37_map_trim_absorbs_the_settled_integral_without_a_bump(void) {
  VP37Pump *pump = vp37TestPump();
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
  VP37Pump *pump = vp37TestPump();
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

void test_vp37_init_status_names_every_result(void) {
  // The names go on the bench screen; each result reads as itself.
  TEST_ASSERT_EQUAL_STRING("ok", VP37_initStatusName(VP37_INIT_OK));
  TEST_ASSERT_EQUAL_STRING("already running",
                           VP37_initStatusName(VP37_INIT_ALREADY_INITIALIZED));
  TEST_ASSERT_EQUAL_STRING("no baseline",
                           VP37_initStatusName(VP37_INIT_BASELINE_NOT_READY));
  TEST_ASSERT_EQUAL_STRING("pid failed",
                           VP37_initStatusName(VP37_INIT_PID_CREATE_FAILED));
  TEST_ASSERT_EQUAL_STRING("cal failed",
                           VP37_initStatusName(VP37_INIT_CALIBRATION_FAILED));
  TEST_ASSERT_EQUAL_STRING("no output",
                           VP37_initStatusName(VP37_INIT_OUTPUT_UNAVAILABLE));
  /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
  const auto invalid = (VP37InitStatus)(VP37_INIT_OUTPUT_UNAVAILABLE + 1);
  TEST_ASSERT_EQUAL_STRING("unknown", VP37_initStatusName(invalid));
}

void test_september_reference_keeps_motion_at_95_and_small_deadband(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.septemberReference = true;
  pump->feedforward.motionBoostUp = 32.0f;
  pump->feedforward.motionBoostDown = 0.0f;
  pump->feedforward.riseBlend = 1.0f;
  pump->pid.integralDeadbandTopHz = 380.0f;
  pump->demand.desiredPosition = 8650.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, VP37_integralDeadband(pump));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 926.24f, VP37_feedForward(pump, 8650));
  pump->pid.integralHold = true;
  VP37_updateIntegralHold(pump, true);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
}

void test_september_reference_ramp_has_no_arrival_brake_and_uses_adj_supply(
    void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.septemberReference = true;
  pump->demand.topArrivalDecel = 0.0f;
  pump->feedforward.motionBoostDown = 0.0f;
  VP37_setPositionDemandPercentage(pump, 90.0f);
  injectAdjRegisterData(8200, 120, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  const float before = pump->demand.desiredPosition;
  VP37_setPositionDemandPercentage(pump, 91.0f);
  hal_mock_set_millis(100);
  injectAdjRegisterData(8200, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f / 14.4f, pump->supply.correction);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, pump->thermal.scale);
  TEST_ASSERT_GREATER_THAN_FLOAT(before, pump->demand.desiredPosition);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
}

void test_full_range_prototype_has_bidirectional_integral_authority(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.fullRangePrototype = true;
  pump->pid.integralOverride = 0.0f;
  pump->pid.integralDeadbandTopHz = 380.0f;
  pump->demand.desiredPosition = 9100.0f;
  TEST_ASSERT_EQUAL_FLOAT(360.0f, VP37_integralLimit(pump));
  TEST_ASSERT_EQUAL_FLOAT(12.0f, VP37_integralDeadband(pump));
  pump->pid.integralOverride = 90.0f;
  TEST_ASSERT_EQUAL_FLOAT(90.0f, VP37_integralLimit(pump));
  pump->pid.integralOverride = 120.0f;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(pump, 95.0f));
  for (uint32_t i = 1U; i <= 400U; ++i) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData(9100, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_FLOAT(120.0f, pump->pid.integralLimit);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -120.0f, pump->pid.terms.integral);
  TEST_ASSERT_TRUE(pump->pid.correction < -120.0f);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_setTuning(pump, VP37_TUNING_INTEGRAL_CAP, 90.0f));
  hal_mock_set_millis(2005U);
  injectAdjRegisterData(9100, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -90.0f, pump->pid.terms.integral);
}

void test_full_range_uses_base_hold_offset_release_and_current_policy(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.fullRangePrototype = true;
  pump->demand.desiredPosition = 8650.0f;
  pump->pid.integralDeadbandHz = VP37_integralDeadband(pump);
  pump->pid.integralHold = true;
  pump->pid.integralHoldMeanError = 25.0f;
  pump->pid.integralHoldOffsetMs = 1000U;
  pump->pid.error = 25;
  pump->pidDtUs = 5000U;
  hal_mock_set_millis(1U);
  VP37_updateIntegralHold(pump, true);
  hal_mock_set_millis(1002U);
  VP37_updateIntegralHold(pump, true);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE, VP37_setTuning(pump, VP37_TUNING_CURRENT_FEEDBACK, 1.0f));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_DRIVE_COMPENSATION, 1.0f));
  VP37_resetTuning(pump);
  TEST_ASSERT_FALSE(pump->pid.fullRangePrototype);
}

void test_full_range_integral_recovers_above_old_cap_and_respects_output(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.fullRangePrototype = true;
  pump->pid.integralOverride = 0.0f;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(pump, 100.0f));
  // A two-percent position deficit at14.4V needs correction beyond the old cap.
  for (uint32_t i = 1U; i <= 4000U; ++i) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData(8920, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(pump->pid.terms.integral > 120.0f);
  TEST_ASSERT_TRUE(pump->pid.terms.integral <= 360.0f);
  TEST_ASSERT_TRUE(pump->pid.terms.unconstrained <=
                   pump->pid.upperLimit + .01f);
  TEST_ASSERT_TRUE(pump->output.finalPWM <= VP37_PWM_MAX);
  TEST_ASSERT_FALSE(pump->output.pwmLimited);
  VP37_stop(pump);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
  TEST_ASSERT_FALSE(VP37_isReady(pump));
}

static void verifyResearchIntegralHold(bool fullRange) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.fullRangePrototype = fullRange;
  pump->pid.integralOverride = 120.0f;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(pump, 50.0f));
  for (uint32_t i = 1U; i <= 200U; ++i) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData(4400, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  const float bias = pump->pid.terms.integral;
  TEST_ASSERT_GREATER_THAN_FLOAT(1.0f, bias);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_FREEZE, 1.0f));
  for (uint32_t i = 201U; i <= 400U; ++i) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData(4800, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_EQUAL_FLOAT(bias, pump->pid.terms.integral);
    TEST_ASSERT_TRUE(pump->pid.terms.proportional < 0.0f);
    TEST_ASSERT_TRUE(pump->output.finalPWM <= VP37_PWM_MAX);
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(pump, 0.0f));
  for (uint32_t i = 401U; i <= 500U; ++i) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.integral);
  VP37_resetTuning(pump);
  TEST_ASSERT_EQUAL_UINT8(0U, pump->pid.researchFreezeMask);
}

void test_research_integral_hold_keeps_bias_and_zero_still_releases(void) {
  verifyResearchIntegralHold(true);
}

void test_normal_integral_hold_keeps_bias_and_zero_still_releases(void) {
  verifyResearchIntegralHold(false);
}

static void verifyResearchDriveHold(bool fullRange) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.fullRangePrototype = fullRange;
  pump->pid.integralOverride = 120.0f;
  pump->thermal.driveCompensationEnabled = true;
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE, VP37_setTuning(pump, VP37_TUNING_RESEARCH_FREEZE, 2.0f));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(pump, 50.0f));
  uint32_t ms = 0U;
  runDriveCycles(pump, ms, 1600U, VP37_DRIVE_REFERENCE_OHMS, true);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  const float resistance = pump->thermal.driveResistance;
  const float scale = pump->thermal.scale;
  const uint32_t learned = pump->thermal.driveLearnedSamples;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_FREEZE, 2.0f));
  const float warm = VP37_DRIVE_REFERENCE_OHMS * 1.1f;
  runDriveCycles(pump, ms, 400U, warm, true);
  TEST_ASSERT_EQUAL_FLOAT(resistance, pump->thermal.driveResistance);
  TEST_ASSERT_EQUAL_FLOAT(scale, pump->thermal.scale);
  TEST_ASSERT_EQUAL_UINT32(learned, pump->thermal.driveLearnedSamples);
  TEST_ASSERT_FALSE(pump->thermal.driveLearning);
  TEST_ASSERT_TRUE(pump->thermal.driveCompensationUsed);
  TEST_ASSERT_FLOAT_WITHIN(.001f, warm, pump->thermal.driveObservationOhms);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_FREEZE, 0.0f));
  runDriveCycles(pump, ms, 400U, warm, true);
  TEST_ASSERT_TRUE(pump->thermal.driveResistance > resistance);
  TEST_ASSERT_TRUE(pump->thermal.scale > scale);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_FREEZE, 3.0f));
  VP37_stop(pump);
  TEST_ASSERT_EQUAL_UINT8(0U, pump->pid.researchFreezeMask);
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
}

void test_research_drive_hold_observes_without_changing_estimate_or_scale(
    void) {
  verifyResearchDriveHold(true);
}

void test_normal_drive_hold_observes_without_changing_estimate_or_scale(void) {
  verifyResearchDriveHold(false);
}

static void verifyMotionIntegralApproach(int32_t direction,
                                         bool unwind = false) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.fullRangePrototype = true;
  pump->pid.integralOverride = 120.0f;
  pump->pid.researchDescentIntegralUnwind = unwind;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_MOTION_INTEGRAL, 1.0f));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setTuning(pump, VP37_TUNING_KD, 0.001f));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(pump, 50.0f));
  const int32_t target = pump->demand.target;
  const int32_t initial = target - direction * 200;
  for (uint32_t i = 1U; i <= 200U; ++i) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData((uint16_t)initial, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(1.0f,
                                 (float)direction * pump->pid.terms.integral);
  float held = 0.0f;
  for (uint32_t i = 1U; i <= 100U; ++i) {
    hal_mock_set_millis((200U + i) * 5U);
    injectAdjRegisterData((uint16_t)(initial + direction * (int32_t)i), 144, 29,
                          ADJ_STATUS_OK);
    VP37_process(pump);
    if (i == 20U) {
      held = pump->pid.terms.integral;
    }
    if (i > 20U) {
      TEST_ASSERT_TRUE(pump->pid.motionIntegralHold);
      if (unwind &&
          pump->pid.descentUnwindPhase == VP37_DESCENT_UNWIND_ACTIVE) {
        TEST_ASSERT_TRUE(pump->pid.terms.integral > held);
        TEST_ASSERT_TRUE(pump->pid.terms.integral <=
                         pump->pid.descentUnwindTarget);
      } else {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, held, pump->pid.terms.integral);
      }
      TEST_ASSERT_TRUE((float)direction * pump->pid.terms.derivative < 0.0f);
    }
  }
  for (uint32_t i = 1U; i <= 30U; ++i) {
    hal_mock_set_millis((300U + i) * 5U);
    injectAdjRegisterData((uint16_t)(target + direction * (100 + (int32_t)i)),
                          144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_FALSE(pump->pid.motionIntegralHold);
  }
  TEST_ASSERT_TRUE((float)direction * (pump->pid.terms.integral - held) < 0.0f);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(pump, 0.0f));
  for (uint32_t i = 331U; i <= 500U; ++i) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData(100, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_EQUAL_INT32(0, pump->output.finalPWM);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.integral);
  TEST_ASSERT_FALSE(pump->pid.motionIntegralReady);
  TEST_ASSERT_FALSE(pump->pid.motionIntegralHold);
}

void test_motion_integral_retains_bias_and_D_during_upward_approach(void) {
  verifyMotionIntegralApproach(1);
}
void test_motion_integral_retains_bias_and_D_during_downward_approach(void) {
  verifyMotionIntegralApproach(-1);
}
void test_motion_integral_switch_is_profile_gated_and_resettable(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_MOTION_INTEGRAL, 1.0f));
  pump->pid.fullRangePrototype = true;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_MOTION_INTEGRAL, 1.0f));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_MOTION_INTEGRAL, 0.5f));
  TEST_ASSERT_TRUE(pump->pid.researchMotionIntegralGate);
  VP37_stop(pump);
  TEST_ASSERT_FALSE(pump->pid.researchMotionIntegralGate);
  VP37_resetTuning(pump);
  TEST_ASSERT_EQUAL_FLOAT(
      0.0f, VP37_getTuning(pump, VP37_TUNING_RESEARCH_MOTION_INTEGRAL));
}

#if VP37_TELEMETRY_ENABLED
static VP37Pump *prepareCalibrationProbe(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->demand.atRest = true;
  pump->demand.requestedPercent = 0.0f;
  pump->demand.target = pump->feedback.adjustMin;
  pump->demand.desired = pump->feedback.adjustMin;
  pump->output.finalPWM = 0;
  pump->callbacks.setDriveEnabled(true);
  return pump;
}

void test_calibration_probe_preserves_calibration_pid_and_zeroes_output(void) {
  VP37Pump *pump = prepareCalibrationProbe();
  const VP37Feedback feedback = pump->feedback;
  const float kp = pump->pid.kp;
  const float ki = pump->pid.ki;
  AdjustometerScript script = {};
  appendAdjRegisterDataRepeated(&script, 10, 8300, 132, 40, ADJ_STATUS_OK);
  injectAdjustometerScript(&script);
  int32_t maximum = -1;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_probeCalibrationMaximum(pump, &maximum));
  TEST_ASSERT_EQUAL_INT32(8300, maximum);
  TEST_ASSERT_EQUAL_INT32(0, s_vp37Board.lastQuantityPwm);
  TEST_ASSERT_EQUAL_MEMORY(&feedback, &pump->feedback, sizeof(feedback));
  TEST_ASSERT_EQUAL_FLOAT(kp, pump->pid.kp);
  TEST_ASSERT_EQUAL_FLOAT(ki, pump->pid.ki);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.integral);
  TEST_ASSERT_EQUAL_UINT32(200U, hal_millis());
}

void test_calibration_probe_refuses_active_or_unreleased_state(void) {
  VP37Pump *pump = prepareCalibrationProbe();
  int32_t maximum = -1;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        VP37_probeCalibrationMaximum(NULL, &maximum));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_probeCalibrationMaximum(pump, NULL));
  pump->demand.requestedPercent = 95.0f;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        VP37_probeCalibrationMaximum(pump, &maximum));
  pump->demand.requestedPercent = 0.0f;
  pump->pid.terms.integral = 1.0f;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        VP37_probeCalibrationMaximum(pump, &maximum));
  pump->pid.terms.integral = 0.0f;
  pump->callbacks.setDriveEnabled(false);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        VP37_probeCalibrationMaximum(pump, &maximum));
  TEST_ASSERT_EQUAL_INT32(-1, s_vp37Board.lastQuantityPwm);
  TEST_ASSERT_EQUAL_INT32(-1, maximum);
}

void test_calibration_probe_invalid_feedback_aborts_and_retains_result(void) {
  VP37Pump *pump = prepareCalibrationProbe();
  injectAdjRegisterData(8300, 132, 40, ADJ_STATUS_SIGNAL_LOST);
  int32_t maximum = -1;
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        VP37_probeCalibrationMaximum(pump, &maximum));
  TEST_ASSERT_EQUAL_INT32(-1, maximum);
  TEST_ASSERT_EQUAL_INT32(0, s_vp37Board.lastQuantityPwm);
  TEST_ASSERT_EQUAL_UINT32(20U, hal_millis());
}

void test_calibration_probe_unsettled_feedback_times_out_at_one_second(void) {
  VP37Pump *pump = prepareCalibrationProbe();
  AdjustometerScript script = {};
  for (int i = 0; i < 50; ++i) {
    appendAdjRegisterData(&script, (i % 2) ? 8000 : 8300, 132, 40,
                          ADJ_STATUS_OK);
  }
  injectAdjustometerScript(&script);
  int32_t maximum = -1;
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        VP37_probeCalibrationMaximum(pump, &maximum));
  TEST_ASSERT_EQUAL_INT32(-1, maximum);
  TEST_ASSERT_EQUAL_INT32(0, s_vp37Board.lastQuantityPwm);
  TEST_ASSERT_EQUAL_UINT32(1000U, hal_millis());
}

void test_calibration_probe_bus_failure_and_clock_wrap_release_output(void) {
  VP37Pump *pump = prepareCalibrationProbe();
  hal_mock_set_millis(UINT32_MAX - 100U);
  // A failed transfer must release the output even across clock wrap.
  vp37AdjustometerTransferBusy(true);
  int32_t maximum = -1;
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        VP37_probeCalibrationMaximum(pump, &maximum));
  TEST_ASSERT_EQUAL_INT32(-1, maximum);
  TEST_ASSERT_EQUAL_INT32(0, s_vp37Board.lastQuantityPwm);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 80U, hal_millis());
}
#endif

static VP37Pump *prepareSlowDamping(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->feedback.adjustMin = 0;
  pump->feedback.adjustMax = 10000;
  pump->feedback.position = 9500;
  pump->demand.desiredPosition = 9500.0f;
  pump->pid.fullRangePrototype = true;
  pump->pid.researchSlowDamping = true;
  pump->pid.topDBlend = 1.0f;
  VP37_resetSlowDamping(pump);
  return pump;
}

void test_slow_damping_preserves_dc_and_opposes_velocity(void) {
  VP37Pump *pump = prepareSlowDamping();
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_updateSlowDamping(pump, .005f));
  for (uint32_t i = 0U; i < 400U; ++i) {
    pump->feedback.position += 5;
    (void)VP37_updateSlowDamping(pump, .005f);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, -6.0f, pump->pid.slowDampingOutput);
  for (uint32_t i = 0U; i < 400U; ++i) {
    pump->feedback.position -= 5;
    (void)VP37_updateSlowDamping(pump, .005f);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, 6.0f, pump->pid.slowDampingOutput);
  for (uint32_t i = 0U; i < 400U; ++i) {
    (void)VP37_updateSlowDamping(pump, .005f);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->pid.slowDampingOutput);
}

static float slowDampingAmplitude(VP37Pump *pump, float frequency) {
  VP37_resetSlowDamping(pump);
  float sine = 0.0f;
  float cosine = 0.0f;
  const float twoPi = 6.283185307179586f;
  for (uint32_t i = 0U; i < 1200U; ++i) {
    const float phase = twoPi * frequency * (float)i * .005f;
    pump->feedback.position = 9500 + (int32_t)lroundf(100.0f * sinf(phase));
    const float result = VP37_updateSlowDamping(pump, .005f);
    if (i >= 400U) {
      sine += result * sinf(phase);
      cosine += result * cosf(phase);
    }
  }
  return 2.0f * sqrtf((sine * sine) + (cosine * cosine)) / 800.0f;
}

void test_slow_damping_rejects_fast_feedback_without_removing_native_D(void) {
  VP37Pump *pump = prepareSlowDamping();
  const float nativeGain = pump->pid.topKd;
  TEST_ASSERT_FLOAT_WITHIN(.15f, 3.400f, slowDampingAmplitude(pump, 1.0f));
  TEST_ASSERT_LESS_THAN_FLOAT(1.1f, slowDampingAmplitude(pump, 40.0f));
  TEST_ASSERT_EQUAL_FLOAT(nativeGain, pump->pid.topKd);
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KP, pump->pid.kp);
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KI, pump->pid.ki);
}

void test_slow_damping_resets_on_disable_invalid_dt_and_reentry(void) {
  VP37Pump *pump = prepareSlowDamping();
  (void)VP37_updateSlowDamping(pump, .005f);
  pump->feedback.position += 500;
  (void)VP37_updateSlowDamping(pump, .005f);
  TEST_ASSERT_TRUE(fabsf(pump->pid.slowDampingOutput) > .001f);
  pump->pid.researchSlowDamping = false;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_updateSlowDamping(pump, .005f));
  TEST_ASSERT_FALSE(pump->pid.slowDampingReady);
  pump->pid.researchSlowDamping = true;
  pump->feedback.position += 2000;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_updateSlowDamping(pump, .005f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_updateSlowDamping(pump, NAN));
  TEST_ASSERT_FALSE(pump->pid.slowDampingReady);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_updateSlowDamping(pump, .2f));
  pump->demand.desiredPosition = 7500.0f;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_updateSlowDamping(pump, .005f));
  pump->feedback.position += 100;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_updateSlowDamping(pump, .005f));
}

static void verifySlowDampingAuthority(float direction) {
  VP37Pump *pump = prepareSlowDamping();
  pump->pid.slowDampingReady = true;
  pump->pid.slowDampingPosition = pump->feedback.position;
  for (size_t i = 0U; i < COUNTOF(pump->pid.slowDampingVelocity); ++i) {
    pump->pid.slowDampingVelocity[i] = -direction * 10000.0f;
  }
  // P plus the bounded D must still exceed either total-output limit.
  pump->pid.error = (int32_t)(direction * 1200.0f);
  pump->pid.negativeLimit = -60.0f;
  pump->pid.upperLimit = 60.0f;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_stepDampedCorrection(pump, .005f, 0.0f,
                                                      pump->demand.desired));
  TEST_ASSERT_FLOAT_WITHIN(.001f, direction * 60.0f, pump->pid.terms.output);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.integral);
  TEST_ASSERT_EQUAL(direction > 0.0f, pump->pid.terms.saturated_high);
  TEST_ASSERT_EQUAL(direction < 0.0f, pump->pid.terms.saturated_low);
  TEST_ASSERT_FLOAT_WITHIN(.001f,
                           pump->pid.terms.proportional +
                               pump->pid.terms.integral +
                               pump->pid.terms.derivative,
                           pump->pid.terms.unconstrained);
}

void test_slow_damping_keeps_complete_command_inside_both_antiwindup_limits(
    void) {
  verifySlowDampingAuthority(1.0f);
  tearDown();
  setUp();
  verifySlowDampingAuthority(-1.0f);
}

void test_slow_damping_switch_requires_released_zero_and_default_is_off(void) {
  VP37Pump *pump = prepareSlowDamping();
  pump->pid.researchSlowDamping = false;
  pump->demand.atRest = true;
  pump->demand.requestedPercent = 0.0f;
  pump->output.finalPWM = 0;
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_SLOW_DAMPING, 1.0f));
  TEST_ASSERT_EQUAL_FLOAT(
      1.0f, VP37_getTuning(pump, VP37_TUNING_RESEARCH_SLOW_DAMPING));
  pump->output.finalPWM = 1;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_SLOW_DAMPING, 0.0f));
  pump->output.finalPWM = 0;
  VP37_resetTuning(pump);
  TEST_ASSERT_FALSE(pump->pid.researchSlowDamping);
  TEST_ASSERT_FALSE(pump->pid.slowDampingReady);
}

#if VP37_TELEMETRY_ENABLED
void test_slow_damping_telemetry_publishes_the_flag_and_complete_extra(void) {
  VP37Pump *pump = prepareSlowDamping();
  pump->pid.slowDampingOutput = 12.5f;
  hal_mock_set_millis(2000U);
  VP37_publish(pump);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_readSnapshot(pump, &snapshot));
  TEST_ASSERT_TRUE(snapshot.telemetry.pid.researchSlowDamping);
  TEST_ASSERT_EQUAL_FLOAT(12.5f, snapshot.telemetry.pid.slowDampingOutput);
  VP37_showDebug(pump, &snapshot);
}
#endif

static void prepareReferencePump(VP37Pump *pump, float beta, int32_t position) {
  setupPumpForProcessTests(pump);
  pump->feedback.adjustMin = 0;
  pump->feedback.adjustMax = 10000;
  pump->feedback.position = position;
  pump->demand.desiredPosition = (float)position;
  pump->demand.desired = position;
  pump->pid.error = 0;
  pump->pid.fullRangePrototype = true;
  pump->pid.researchReferenceWeight = beta;
  pump->pid.researchSlowDamping = false;
  pump->pid.negativeLimit = -220.0f;
  pump->pid.upperLimit = 340.0f;
  hal_pid_controller_set_kd(pump->pid.controller, .001f);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_stepDampedCorrection(pump, .005f, 12.0f,
                                                      pump->demand.desired));
}

static void destroyReferencePump(VP37Pump *pump) {
  hal_pid_controller_destroy(pump->pid.controller);
  pump->pid.controller = NULL;
}

static void verifyReferenceDisturbance(int32_t target, bool held) {
  static VP37Pump control;
  static VP37Pump weighted;
  prepareReferencePump(&control, 1.0f, target);
  prepareReferencePump(&weighted, .1f, target);
  const float initialDifference =
      weighted.pid.terms.output - control.pid.terms.output;
  const float amplitude = (target >= 8500 && target <= 9000) ? 100.0f : 20.0f;
  for (uint32_t i = 1U; i <= 1000U; ++i) {
    const int32_t position =
        target + (int32_t)lroundf(amplitude * sinf((float)i * .022f));
    VP37Pump *pumps[] = {&control, &weighted};
    for (size_t k = 0U; k < COUNTOF(pumps); ++k) {
      VP37Pump *pump = pumps[k];
      pump->feedback.position = position;
      pump->pid.error = target - position;
      const float deadband = held ? fabsf((float)pump->pid.error) : 12.0f;
      TEST_ASSERT_EQUAL(HAL_OK,
                        VP37_stepDampedCorrection(pump, .005f, deadband,
                                                  pump->demand.desired));
    }
    TEST_ASSERT_FLOAT_WITHIN(.003f, initialDifference,
                             weighted.pid.terms.output -
                                 control.pid.terms.output);
    TEST_ASSERT_EQUAL(control.pid.terms.saturated_high,
                      weighted.pid.terms.saturated_high);
    TEST_ASSERT_EQUAL(control.pid.terms.saturated_low,
                      weighted.pid.terms.saturated_low);
  }
  destroyReferencePump(&control);
  destroyReferencePump(&weighted);
}

void test_reference_weight_preserves_upper_disturbance_feedback_and_position_hold(
    void) {
  verifyReferenceDisturbance(9500, false);
  verifyReferenceDisturbance(9500, true);
}

void test_reference_weight_preserves_the_lower_controller(void) {
  verifyReferenceDisturbance(5000, false);
  verifyReferenceDisturbance(5000, true);
}

void test_reference_weight_reduces_kick_without_changing_disturbance_gain(
    void) {
  static VP37Pump control;
  static VP37Pump weighted;
  prepareReferencePump(&control, 1.0f, 9500);
  prepareReferencePump(&weighted, .1f, 9500);
  const float initialControl = control.pid.terms.output;
  const float initialWeighted = weighted.pid.terms.output;
  control.pid.error = weighted.pid.error = 400;
  control.demand.desiredPosition = weighted.demand.desiredPosition = 9900.0f;
  control.demand.desired = weighted.demand.desired = 9900;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(&control, .005f, 400.0f, 9500));
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(&weighted, .005f, 400.0f, 9500));
  TEST_ASSERT_FLOAT_WITHIN(.001f, 20.0f,
                           control.pid.terms.output - initialControl);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 2.0f,
                           weighted.pid.terms.output - initialWeighted);
  const float oldControl = control.pid.terms.output;
  const float oldWeighted = weighted.pid.terms.output;
  control.feedback.position = weighted.feedback.position = 9510;
  control.pid.error = weighted.pid.error = 390;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(&control, .005f, 390.0f, 9900));
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(&weighted, .005f, 390.0f, 9900));
  TEST_ASSERT_FLOAT_WITHIN(.002f, control.pid.terms.output - oldControl,
                           weighted.pid.terms.output - oldWeighted);
  destroyReferencePump(&control);
  destroyReferencePump(&weighted);
}

void test_reference_weight_potential_is_continuous_and_has_no_roundtrip_drift(
    void) {
  VP37Pump *pump = vp37TestPump();
  prepareReferencePump(pump, .1f, 8400);
  float weight = -1.0f;
  TEST_ASSERT_EQUAL_FLOAT(0.0f,
                          VP37_referencePotential(pump, 8500.0f, &weight));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, weight);
  TEST_ASSERT_EQUAL_FLOAT(62.5f,
                          VP37_referencePotential(pump, 8750.0f, &weight));
  TEST_ASSERT_EQUAL_FLOAT(.5f, weight);
  TEST_ASSERT_EQUAL_FLOAT(250.0f,
                          VP37_referencePotential(pump, 9000.0f, &weight));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, weight);
  TEST_ASSERT_EQUAL_FLOAT(1250.0f,
                          VP37_referencePotential(pump, 10000.0f, NULL));
  hal_pid_controller_set_kd(pump->pid.controller, 0.0f);
  const int32_t positions[] = {8500, 8750, 9000, 9500, 10000,
                               9500, 9000, 8750, 8500, 8400};
  for (size_t i = 0U; i < COUNTOF(positions); ++i) {
    const int32_t previous = pump->demand.desired;
    pump->feedback.position = positions[i];
    pump->demand.desiredPosition = (float)positions[i];
    pump->demand.desired = positions[i];
    pump->pid.error = 0;
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(pump, .005f, 0.0f, previous));
  }
  TEST_ASSERT_FLOAT_WITHIN(.003f, 0.0f, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.003f, 0.0f, pump->pid.terms.output);
}

static void verifyReferenceSaturation(int32_t error) {
  VP37Pump *pump = vp37TestPump();
  prepareReferencePump(pump, .1f, 9500);
  pump->pid.negativeLimit = -1.0f;
  pump->pid.upperLimit = 1.0f;
  pump->pid.error = error;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_stepDampedCorrection(pump, .005f, 12.0f,
                                                      pump->demand.desired));
  TEST_ASSERT_FLOAT_WITHIN(.001f, (error > 0) ? 1.0f : -1.0f,
                           pump->pid.terms.output);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.integral);
  TEST_ASSERT_EQUAL(error > 0, pump->pid.terms.saturated_high);
  TEST_ASSERT_EQUAL(error < 0, pump->pid.terms.saturated_low);
  TEST_ASSERT_FLOAT_WITHIN(.001f,
                           pump->pid.terms.proportional +
                               pump->pid.terms.integral +
                               pump->pid.terms.derivative,
                           pump->pid.terms.unconstrained);
}

void test_reference_weight_includes_compensation_in_both_antiwindup_limits(
    void) {
  verifyReferenceSaturation(2000);
  tearDown();
  setUp();
  verifyReferenceSaturation(-2000);
}

void test_reference_weight_switch_is_safe_and_excludes_full_I_freeze(void) {
  VP37Pump *pump = prepareSlowDamping();
  pump->demand.atRest = true;
  pump->demand.requestedPercent = 0.0f;
  pump->output.finalPWM = 0;
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_REFERENCE_WEIGHT, .1f));
  TEST_ASSERT_FLOAT_WITHIN(
      .0001f, .1f, VP37_getTuning(pump, VP37_TUNING_RESEARCH_REFERENCE_WEIGHT));
  TEST_ASSERT_EQUAL(HAL_ESTATE, VP37_setTuning(pump, VP37_TUNING_KI, 0.0f));
  TEST_ASSERT_EQUAL(HAL_ESTATE,
                    VP37_setTuning(pump, VP37_TUNING_RESEARCH_FREEZE, 1.0f));
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_MOTION_INTEGRAL, 1.0f));
  pump->output.finalPWM = 1;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_REFERENCE_WEIGHT, 1.0f));
  pump->output.finalPWM = 0;
  VP37_resetTuning(pump);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pump->pid.researchReferenceWeight);
}

#if VP37_TELEMETRY_ENABLED
void test_reference_weight_telemetry_publishes_physical_coefficients(void) {
  VP37Pump *pump = vp37TestPump();
  prepareReferencePump(pump, .1f, 9500);
  pump->pid.integralInput = -7.5f;
  hal_mock_set_millis(1000U);
  VP37_publish(pump);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_readSnapshot(pump, &snapshot));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, .1f,
                           snapshot.telemetry.pid.researchReferenceWeight);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .005f, snapshot.telemetry.pid.referenceKp);
  TEST_ASSERT_EQUAL_FLOAT(-7.5f, snapshot.telemetry.pid.integralInput);
  VP37_showDebug(pump, &snapshot);
}
#endif

void test_reference_weight_preserves_disturbances_through_the_upper_blend(
    void) {
  for (int32_t target = 8500; target <= 9000; target += 250) {
    verifyReferenceDisturbance(target, false);
    verifyReferenceDisturbance(target, true);
  }
}

void test_reference_weight_keeps_complete_standing_integral_authority(void) {
  static VP37Pump weighted;
  for (int direction = -1; direction <= 1; direction += 2) {
    prepareReferencePump(&weighted, .1f, 9500);
    hal_pid_controller_set_kd(weighted.pid.controller, 0.0f);
    hal_pid_controller_set_max_integral(weighted.pid.controller, 600.0f);
    weighted.pid.error = direction * 100;
    weighted.feedback.position = 9500 - weighted.pid.error;
    for (uint32_t i = 0U; i < 2000U; ++i) {
      TEST_ASSERT_EQUAL(
          HAL_OK, VP37_stepDampedCorrection(&weighted, .005f, 12.0f, 9500));
    }
    weighted.pid.error = 0;
    weighted.feedback.position = 9500;
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(&weighted, .005f, 12.0f, 9500));
    TEST_ASSERT_FLOAT_WITHIN(.01f, direction * 120.0f,
                             weighted.pid.terms.integral);
    TEST_ASSERT_FLOAT_WITHIN(.01f, direction * 120.0f,
                             weighted.pid.terms.output);
    destroyReferencePump(&weighted);
  }
}

void test_reference_weight_keeps_the_counter_with_motion_error_hold(void) {
  VP37Pump *pump = vp37TestPump();
  prepareReferencePump(pump, 1.0f, 9500);
  pump->demand.atRest = true;
  pump->demand.requestedPercent = 0.0f;
  pump->output.finalPWM = 0;
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_MOTION_INTEGRAL, 1.0f));
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_REFERENCE_WEIGHT, .1f));
  pump->demand.desired = 9900;
  pump->demand.desiredPosition = 9900.0f;
  pump->pid.error = 400;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 400.0f, 9500));
  TEST_ASSERT_FLOAT_WITHIN(.001f, -18.0f, pump->pid.terms.integral);
  pump->pid.motionIntegralReady = true;
  pump->pid.motionIntegralPosition = 9500;
  pump->pid.motionIntegralVelocity = 200.0f;
  pump->feedback.position = 9510;
  pump->pid.error = 390;
  TEST_ASSERT_TRUE(VP37_motionIntegralHold(pump, true, .005f));
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 390.0f, 9900));
  TEST_ASSERT_FLOAT_WITHIN(.001f, -18.0f, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 19.5f, pump->pid.terms.proportional);
}

#if VP37_TELEMETRY_ENABLED
void test_live_control_format_is_available_during_RAM_recording(void) {
  VP37Pump *pump = vp37TestPump();
  prepareReferencePump(pump, .1f, 9500);
  TEST_ASSERT_EQUAL(HAL_OK, VP37_startTrace(pump));
  TEST_ASSERT_TRUE(VP37_traceCapturing());
  VP37_publish(pump);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_readSnapshot(pump, &snapshot));
  printf("TRACE_RECORDING_C_BEGIN\n");
  VP37_showDebug(pump, &snapshot);
  printf("TRACE_RECORDING_C_END\n");
  TEST_ASSERT_TRUE(VP37_traceCapturing());
  VP37_traceStop();
  static VP37TraceSample sample;
  while (VP37_readTrace(&sample) == HAL_OK) {
  }
}
#endif

void test_ramp_damping_keeps_measurement_gain_during_both_reference_directions(
    void) {
  VP37Pump *pump = prepareSlowDamping();
  pump->pid.researchSlowDamping = false;
  pump->pid.researchRampDamping = true;
  pump->pid.topKd = .001f;
  pump->pid.kd = 0.0f;
  pump->pidDtUs = 5000U;
  pump->demand.atRest = false;
  const int32_t positions[] = {8500, 8750, 9000, 9500, 9000, 8750, 8500};
  const float gains[] = {0.0f, .0005f, .001f, .001f, .001f, .0005f, 0.0f};
  for (uint32_t i = 0U; i < COUNTOF(positions); ++i) {
    pump->demand.desiredPosition = (float)positions[i];
    VP37_updateDerivativeGain(pump, false);
    TEST_ASSERT_FLOAT_WITHIN(.000001f, gains[i], pump->pid.effectiveKd);
  }
}

void test_ramp_damping_has_no_reference_kick_and_opposes_measured_motion(void) {
  VP37Pump *pump = prepareSlowDamping();
  pump->pid.researchSlowDamping = false;
  pump->pid.researchRampDamping = true;
  pump->pid.topKd = .001f;
  pump->pid.kd = 0.0f;
  pump->pidDtUs = 5000U;
  pump->demand.atRest = false;
  pump->pid.tf = .003f;
  hal_pid_controller_set_tf(pump->pid.controller, .003f);
  hal_pid_controller_reset(pump->pid.controller);
  hal_pid_terms_t terms;
  VP37_updateDerivativeGain(pump, false);
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_pid_controller_step_ex(pump->pid.controller, 0.0f,
                                               9000.0f, .005f, 0.0f, &terms));
  pump->demand.desiredPosition = 9900.0f;
  VP37_updateDerivativeGain(pump, false);
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_pid_controller_step_ex(pump->pid.controller, 900.0f,
                                               9000.0f, .005f, 0.0f, &terms));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, terms.derivative);
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_pid_controller_step_ex(pump->pid.controller, 892.0f,
                                               9008.0f, .005f, 0.0f, &terms));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -1.0f, terms.derivative);
}

void test_ramp_damping_preserves_settled_gain_lower_range_and_release(void) {
  VP37Pump *pump = prepareSlowDamping();
  pump->pid.researchSlowDamping = false;
  pump->pid.topKd = .001f;
  pump->pid.kd = 0.0f;
  pump->pidDtUs = 5000U;
  pump->demand.atRest = false;
  pump->pid.researchRampDamping = false;
  pump->pid.topDBlend = 1.0f;
  VP37_updateDerivativeGain(pump, true);
  const float original = pump->pid.effectiveKd;
  pump->pid.researchRampDamping = true;
  VP37_updateDerivativeGain(pump, true);
  TEST_ASSERT_EQUAL_FLOAT(original, pump->pid.effectiveKd);
  pump->demand.desiredPosition = 8400.0f;
  VP37_updateDerivativeGain(pump, false);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.effectiveKd);
  pump->demand.desiredPosition = 9500.0f;
  pump->demand.atRest = true;
  VP37_updateDerivativeGain(pump, false);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.effectiveKd);
  pump->demand.atRest = false;
  pump->pid.fullRangePrototype = false;
  VP37_updateDerivativeGain(pump, false);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.effectiveKd);
}

void test_ramp_damping_switch_requires_released_zero_and_resets_on_stop(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->demand.atRest = true;
  pump->demand.requestedPercent = 0.0f;
  pump->output.finalPWM = 0;
  TEST_ASSERT_FALSE(pump->pid.researchRampDamping);
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_RAMP_DAMPING, 1.0f));
  pump->pid.fullRangePrototype = true;
  TEST_ASSERT_EQUAL(
      HAL_EINVAL, VP37_setTuning(pump, VP37_TUNING_RESEARCH_RAMP_DAMPING, .5f));
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_RAMP_DAMPING, 1.0f));
  TEST_ASSERT_EQUAL_FLOAT(
      1.0f, VP37_getTuning(pump, VP37_TUNING_RESEARCH_RAMP_DAMPING));
  pump->demand.requestedPercent = 95.0f;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_RAMP_DAMPING, 0.0f));
  pump->demand.requestedPercent = 0.0f;
  pump->output.finalPWM = 1;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_RAMP_DAMPING, 0.0f));
  VP37_stop(pump);
  TEST_ASSERT_FALSE(pump->pid.researchRampDamping);
  TEST_ASSERT_EQUAL_INT32(0, s_vp37Board.lastQuantityPwm);
  pump->pid.researchRampDamping = true;
  VP37_resetTuning(pump);
  TEST_ASSERT_FALSE(pump->pid.researchRampDamping);
}

#if VP37_TELEMETRY_ENABLED
void test_ramp_damping_telemetry_reports_the_selected_mode(void) {
  VP37Pump *pump = prepareSlowDamping();
  pump->pid.researchRampDamping = true;
  hal_mock_set_millis(3000U);
  VP37_publish(pump);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_readSnapshot(pump, &snapshot));
  TEST_ASSERT_TRUE(snapshot.telemetry.pid.researchRampDamping);
  VP37_showDebug(pump, &snapshot);
}
#endif

static VP37Pump *prepareUpperHolding(int32_t target = 9000) {
  VP37Pump *pump = vp37TestPump();
  prepareReferencePump(pump, 1.0f, target);
  pump->pid.fullRangePrototype = false;
  pump->demand.atRest = false;
  pump->demand.target = target;
  pump->feedback.fresh = true;
  pump->feedback.readStatus = HAL_OK;
  pump->currentControl.enabled = true;
  pump->thermal.driveCompensationEnabled = true;
  pump->thermal.driveCompensationUsed = true;
  pump->thermal.driveResistanceReady = true;
  pump->feedforward.mapTrimEnabled = false;
  pump->pid.error = 8;
  pump->feedback.position = target - 8;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 50.0f, target));
  hal_pid_controller_reset(pump->pid.controller);
  TEST_ASSERT_EQUAL(HAL_OK, hal_pid_controller_step_ex(
                                pump->pid.controller, 8.0f, (float)(target - 8),
                                .005f, 50.0f, &pump->pid.terms));
  float applied = 0.0f;
  TEST_ASSERT_EQUAL(
      HAL_OK, hal_pid_controller_shift_integral_ex(pump->pid.controller, -30.0f,
                                                   &applied, &pump->pid.terms));
  TEST_ASSERT_EQUAL_FLOAT(-30.0f, applied);
  pump->pid.correction = pump->pid.terms.output;
  return pump;
}

void test_upper_holding_quiet_window_forgets_motion_older_than_its_window(
    void) {
  VP37Pump *pump = prepareUpperHolding();
  for (unsigned i = 0U; i < 400U; ++i) {
    pump->feedback.position = (i % 40U) < 20U ? 8800 : 9200;
    (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  }
  TEST_ASSERT_GREATER_THAN(225.0f, pump->pid.upperHoldingVariance);
  pump->feedback.position = 9000;
  for (unsigned i = 0U; i < 200U; ++i) {
    (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, 9000.0f, pump->pid.upperHoldingMean);
  TEST_ASSERT_FLOAT_WITHIN(.01f, 0.0f, pump->pid.upperHoldingVariance);
}

void test_upper_holding_quiet_window_rejects_motion_since_its_last_publication(
    void) {
  VP37Pump *pump = prepareUpperHolding();
  pump->feedback.position = 9000;
  for (unsigned i = 0U; i < 740U; ++i) {
    (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  }
  TEST_ASSERT_FLOAT_WITHIN(.01f, 0.0f, pump->pid.upperHoldingVariance);
  // A previously quiet completed window must not admit a bias update now.
  pump->feedback.position = 9240;
  (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  TEST_ASSERT_GREATER_THAN_FLOAT(VP37_UPPER_HOLD_QUIET_VARIANCE,
                                 pump->pid.upperHoldingVariance);
}

void test_upper_holding_bias_keeps_its_slow_gain_at_the_blend_start(void) {
  VP37Pump *pump = prepareUpperHolding(8550);
  pump->feedback.position = 8450;
  pump->pid.error = 100;
  pump->pid.upperHoldingElapsed = 3.0f;
  pump->pid.upperHoldingBiasElapsed = 1.5f;
  (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  float weight = 0.0f;
  (void)VP37_referencePotential(pump, 8550.0f, &weight);
  // Isolate bias adaptation from the paired transfer of a gain increase.
  pump->pid.upperHoldingExtraKp = .1f * weight;
  const float before = pump->pid.terms.integral;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 8550));
  // Ki .2 * holding share .05 * 1.5s * (100-12)Hz, also near 85%.
  TEST_ASSERT_FLOAT_WITHIN(.001f, before + 1.32f, pump->pid.terms.integral);
}

void test_upper_holding_capture_delay_follows_the_existing_upper_blend(void) {
  VP37Pump *pump = prepareUpperHolding();
  TEST_ASSERT_EQUAL_FLOAT(12.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
  (void)VP37_prepareUpperHolding(pump, false, .005f, 192.0f);
  pump->demand.target = 8550;
  TEST_ASSERT_EQUAL_FLOAT(192.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
  pump->pid.upperHoldingElapsed = 2.69f;
  TEST_ASSERT_EQUAL_FLOAT(192.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
  pump->pid.upperHoldingElapsed = 2.71f;
  TEST_ASSERT_EQUAL_FLOAT(12.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
}

void test_upper_holding_preserves_arrival_deadband_and_confirmation_before_settle(
    void) {
  VP37Pump *pump = prepareUpperHolding(8550);
  TEST_ASSERT_EQUAL_FLOAT(192.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
  pump->pid.integralDeadbandHz = 192.0f;
  pump->pid.error = 10;
  pump->pid.integralHoldMeanError = 100.0f;
  pump->pidDtUs = 5000U;
  hal_mock_set_millis(2000U);
  VP37_updateIntegralHold(pump, true);
  hal_mock_set_millis(2007U);
  VP37_updateIntegralHold(pump, true);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  pump->pid.upperHoldingElapsed = VP37_UPPER_HOLD_SETTLE_S;
  pump->pid.integralDeadbandHz =
      VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  TEST_ASSERT_EQUAL_FLOAT(12.0f, pump->pid.integralDeadbandHz);
  hal_mock_set_millis(2014U);
  VP37_updateIntegralHold(pump, true);
  hal_mock_set_millis(2021U);
  VP37_updateIntegralHold(pump, true);
  TEST_ASSERT_TRUE(pump->pid.integralHold);
}

void test_upper_holding_preserves_PD_and_ordinary_arrival_integration(void) {
  VP37Pump *pump = prepareUpperHolding(8550);
  const float integral = pump->pid.terms.integral;
  TEST_ASSERT_EQUAL_FLOAT(192.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
  TEST_ASSERT_TRUE(pump->pid.upperHoldingActive);
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 8550));
  TEST_ASSERT_EQUAL_FLOAT(integral, pump->pid.terms.integral);
  TEST_ASSERT_EQUAL_FLOAT(.4f, pump->pid.terms.proportional);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.derivative);
  pump->pid.error = 70;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 8550));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, integral + .058f, pump->pid.terms.integral);
  TEST_ASSERT_EQUAL_FLOAT(3.5f, pump->pid.terms.proportional);
  pump->feedback.position += 8;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 8550));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -1.0f, pump->pid.terms.derivative);
}

void test_native_upper_holding_keeps_the_research_motion_gate_disabled(void) {
  VP37Pump *pump = prepareUpperHolding();
  (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  TEST_ASSERT_TRUE(pump->pid.upperHoldingActive);
  TEST_ASSERT_FALSE(VP37_motionIntegralHold(pump, true, .005f));
  pump->feedback.position += 100;
  TEST_ASSERT_FALSE(VP37_motionIntegralHold(pump, true, .005f));
  TEST_ASSERT_FALSE(pump->pid.motionIntegralReady);
}

void test_upper_holding_preserves_lower_moving_and_research_profiles(void) {
  VP37Pump *pump = prepareUpperHolding();
  pump->pid.upperHoldingElapsed = VP37_UPPER_HOLD_SETTLE_S;
  TEST_ASSERT_EQUAL_FLOAT(12.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
  TEST_ASSERT_TRUE(pump->pid.upperHoldingActive);
  pump->feedback.fresh = false;
  TEST_ASSERT_EQUAL_FLOAT(192.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
  TEST_ASSERT_FALSE(pump->pid.upperHoldingActive);
  pump->feedback.fresh = true;
  TEST_ASSERT_EQUAL_FLOAT(192.0f,
                          VP37_prepareUpperHolding(pump, false, .005f, 192.0f));
  pump->demand.target = 7500;
  TEST_ASSERT_EQUAL_FLOAT(192.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
  TEST_ASSERT_FALSE(pump->pid.upperHoldingActive);
  pump->demand.target = 9000;
  pump->pid.fullRangePrototype = true;
  TEST_ASSERT_EQUAL_FLOAT(192.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
  pump->pid.fullRangePrototype = false;
  pump->pid.researchFreezeMask = 1U;
  TEST_ASSERT_EQUAL_FLOAT(192.0f,
                          VP37_prepareUpperHolding(pump, true, .005f, 192.0f));
  TEST_ASSERT_FALSE(pump->pid.upperHoldingActive);
}

void test_upper_holding_limits_complete_command_and_releases_on_new_demand(
    void) {
  VP37Pump *pump = prepareUpperHolding();
  (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  pump->pid.error = -100;
  pump->pid.negativeLimit = -20.0f;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 9000));
  TEST_ASSERT_EQUAL_FLOAT(-20.0f, pump->pid.terms.output);
  TEST_ASSERT_EQUAL_FLOAT(-30.0f, pump->pid.terms.integral);
  TEST_ASSERT_TRUE(pump->pid.terms.saturated_low);
  pump->demand.target = 8000;
  TEST_ASSERT_EQUAL_FLOAT(192.0f,
                          VP37_prepareUpperHolding(pump, false, .005f, 192.0f));
  TEST_ASSERT_FALSE(pump->pid.upperHoldingActive);
}

void test_upper_holding_stiffens_without_a_drive_step_and_restores_moving_gain(
    void) {
  VP37Pump *pump = prepareUpperHolding();
  hal_pid_controller_set_kd(pump->pid.controller, 0.0f);
  pump->pid.upperHoldingElapsed = VP37_UPPER_HOLD_SETTLE_S;
  (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  pump->pid.error = 8;
  pump->feedback.position = 8992;
  const float before = pump->pid.terms.integral;
  pump->pid.upperHoldingMean = (float)pump->feedback.position;
  for (unsigned i = 0U; i < 400U; ++i) {
    (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(pump, .005f, 100.0f, 9000));
    TEST_ASSERT_FLOAT_WITHIN(.0002f, before + .4f, pump->pid.terms.output);
  }
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .01f, pump->pid.upperHoldingExtraKp);
  TEST_ASSERT_FLOAT_WITHIN(.001f, before - .08f, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .06f, pump->pid.referenceKp);
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.0002f, before + .4f, pump->pid.terms.output);
  (void)VP37_prepareUpperHolding(pump, false, .005f, 192.0f);
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 100.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.0002f, before + .4f, pump->pid.terms.output);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.upperHoldingExtraKp);
  TEST_ASSERT_EQUAL_FLOAT(.05f, pump->pid.referenceKp);
}

void test_upper_holding_captures_integral_early_without_the_legacy_offset_gate(
    void) {
  VP37Pump *pump = prepareUpperHolding();
  pump->pid.upperHoldingActive = true;
  pump->pid.upperHoldingCaptureReady = true;
  pump->pid.upperHoldingElapsed = VP37_UPPER_HOLD_SETTLE_S;
  pump->pid.integralDeadbandHz = 12.0f;
  pump->pid.error = 10;
  pump->pid.integralHoldMeanError = 100.0f;
  pump->pidDtUs = 5000U;
  hal_mock_set_millis(2000U);
  VP37_updateIntegralHold(pump, true);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  hal_mock_set_millis(2007U);
  VP37_updateIntegralHold(pump, true);
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  TEST_ASSERT_EQUAL_UINT32(VP37_INTEGRAL_HOLD_CONFIRM_MS,
                           pump->pid.integralHoldConfirmMs);
  pump->pid.error = 20;
  for (unsigned i = 1U; i <= 400U; ++i) {
    hal_mock_set_millis(2007U + i * 5U);
    VP37_updateIntegralHold(pump, true);
  }
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  VP37_updateIntegralHold(pump, false);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
  pump->pid.upperHoldingActive = false;
  pump->pid.error = 10;
  pump->pid.integralHoldMeanError = 100.0f;
  VP37_updateIntegralHold(pump, true);
  hal_mock_set_millis(4015U);
  VP37_updateIntegralHold(pump, true);
  TEST_ASSERT_FALSE(pump->pid.integralHold);
}

void test_upper_holding_gain_waits_for_a_quiet_interval_and_limits_each_transfer(
    void) {
  VP37Pump *pump = prepareUpperHolding();
  hal_pid_controller_set_kd(pump->pid.controller, 0.0f);
  (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  pump->pid.upperHoldingElapsed = VP37_UPPER_HOLD_SETTLE_S;
  pump->pid.upperHoldingVariance = 400.0f;
  pump->pid.upperHoldingBiasElapsed = 1.5f;
  pump->pid.error = 500;
  pump->feedback.position = 8500;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 500.0f, 9000));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.upperHoldingExtraKp);
  pump->pid.upperHoldingVariance = 0.0f;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 500.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .005f, pump->pid.upperHoldingExtraKp);
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -32.5f, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -5.0f, pump->pid.terms.output);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.upperHoldingBiasElapsed);
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 500.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .005f, pump->pid.upperHoldingExtraKp);
}

void test_upper_holding_stiffness_respects_integral_and_output_headroom(void) {
  VP37Pump *pump = prepareUpperHolding();
  pump->pid.upperHoldingElapsed = VP37_UPPER_HOLD_SETTLE_S;
  (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  pump->pid.error = 100;
  pump->pid.negativeLimit = -20.0f;
  pump->pid.upperHoldingBiasElapsed = 1.5f;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 100.0f, 9000));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.upperHoldingExtraKp);
  TEST_ASSERT_EQUAL_FLOAT(-30.0f, pump->pid.terms.integral);
  TEST_ASSERT_EQUAL_FLOAT(-20.0f, pump->pid.terms.output);
  TEST_ASSERT_TRUE(pump->pid.terms.saturated_low);
  pump->pid.negativeLimit = -220.0f;
  pump->pid.upperHoldingVariance = 400.0f;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 9000));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.integralInput);
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -25.0f, pump->pid.terms.output);
  TEST_ASSERT_FLOAT_WITHIN(.0001f,
                           -30.0f - 100.0f * pump->pid.upperHoldingExtraKp,
                           pump->pid.terms.integral);
  VP37_resetUpperHolding(pump);
  TEST_ASSERT_FALSE(pump->pid.upperHoldingActive);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.upperHoldingElapsed);
  pump->pid.upperHoldingExtraKp = .1f;
  pump->pid.error = -100;
  hal_pid_controller_set_max_integral(pump->pid.controller, 150.0f);
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 100.0f, 9000));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.upperHoldingExtraKp);
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -30.0f, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -35.0f, pump->pid.terms.output);
}

void test_upper_holding_bias_waits_until_the_bumpless_gain_ramp_finishes(void) {
  VP37Pump *pump = prepareUpperHolding();
  hal_pid_controller_set_kd(pump->pid.controller, 0.0f);
  (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  pump->pid.upperHoldingElapsed = VP37_UPPER_HOLD_SETTLE_S;
  pump->pid.upperHoldingMean = 8900.0f;
  pump->pid.upperHoldingVariance = 0.0f;
  pump->pid.error = 100;
  pump->feedback.position = 8900;
  const float before = pump->pid.terms.integral;
  const float drive = before + pump->pid.kp * (float)pump->pid.error;
  unsigned transfers = 0U;
  // Every paired P/I change preserves force. A concurrent bias step would
  // add 1.32 PWM here and defeat that transfer's purpose.
  while (pump->pid.upperHoldingExtraKp < .09999f && transfers < 20U) {
    pump->pid.upperHoldingBiasElapsed = VP37_UPPER_HOLD_BIAS_INTERVAL_S;
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(pump, .005f, 100.0f, 9000));
    TEST_ASSERT_FLOAT_WITHIN(.001f, drive, pump->pid.terms.output);
    TEST_ASSERT_FLOAT_WITHIN(.001f,
                             before - pump->pid.upperHoldingExtraKp * 100.0f,
                             pump->pid.terms.integral);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.upperHoldingBiasElapsed);
    ++transfers;
  }
  TEST_ASSERT_LESS_THAN_UINT(20U, transfers);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .1f, pump->pid.upperHoldingExtraKp);
  const float acquired = pump->pid.terms.integral;
  // With unchanged gain a later quiet interval still corrects the offset.
  pump->pid.upperHoldingBiasElapsed = VP37_UPPER_HOLD_BIAS_INTERVAL_S;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 100.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.001f, acquired + 1.32f, pump->pid.terms.integral);
  TEST_ASSERT_FLOAT_WITHIN(.001f, drive + 1.32f, pump->pid.terms.output);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.upperHoldingBiasElapsed);
}

void test_upper_holding_bias_is_a_bounded_step_between_quiet_intervals(void) {
  VP37Pump *pump = prepareUpperHolding();
  hal_pid_controller_set_kd(pump->pid.controller, 0.0f);
  pump->pid.upperHoldingElapsed = VP37_UPPER_HOLD_SETTLE_S;
  (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  pump->pid.upperHoldingExtraKp = .1f;
  pump->pid.upperHoldingMean = 8900.0f;
  pump->pid.upperHoldingBiasElapsed = 1.49f;
  pump->pid.error = 100;
  pump->feedback.position = 8900;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -30.0f, pump->pid.terms.integral);
  pump->pid.upperHoldingBiasElapsed = VP37_UPPER_HOLD_BIAS_INTERVAL_S;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -28.68f, pump->pid.terms.integral);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.upperHoldingBiasElapsed);
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -28.68f, pump->pid.terms.integral);
  pump->pid.upperHoldingBiasElapsed = VP37_UPPER_HOLD_BIAS_INTERVAL_S;
  pump->pid.upperHoldingVariance = 400.0f;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -28.68f, pump->pid.terms.integral);
  pump->pid.upperHoldingVariance = 0.0f;
  pump->pid.upperHoldingMean = 8000.0f;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 12.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -25.68f, pump->pid.terms.integral);
}

void test_upper_holding_bias_corrects_offset_while_continuous_I_is_held(void) {
  VP37Pump *pump = prepareUpperHolding();
  hal_pid_controller_set_kd(pump->pid.controller, 0.0f);
  (void)VP37_prepareUpperHolding(pump, true, .005f, 192.0f);
  pump->pid.upperHoldingElapsed = VP37_UPPER_HOLD_SETTLE_S;
  pump->pid.upperHoldingExtraKp = .1f;
  pump->pid.upperHoldingMean = 8900.0f;
  pump->pid.upperHoldingVariance = 0.0f;
  pump->pid.upperHoldingBiasElapsed = VP37_UPPER_HOLD_BIAS_INTERVAL_S;
  pump->pid.integralHold = true;
  pump->pid.error = 100;
  pump->feedback.position = 8900;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 100.0f, 9000));
  TEST_ASSERT_TRUE(pump->pid.integralHold);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.integralInput);
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -28.68f, pump->pid.terms.integral);
  pump->pid.upperHoldingBiasElapsed = VP37_UPPER_HOLD_BIAS_INTERVAL_S;
  pump->pid.upperHoldingMean = 9000.0f;
  pump->pid.error = 0;
  pump->feedback.position = 9000;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_stepDampedCorrection(pump, .005f, 0.0f, 9000));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -28.68f, pump->pid.terms.integral);
}

static VP37Pump *prepareUpperDeadband(void) {
  VP37Pump *pump = vp37TestPump();
  prepareReferencePump(pump, .1f, 9500);
  pump->pid.researchUpperDeadband = true;
  pump->pid.integralDeadbandTopHz = 41.0f;
  return pump;
}

void test_upper_deadband_preserves_lower_range_and_legacy_default(void) {
  VP37Pump *pump = prepareUpperDeadband();
  const float positions[] = {0.0f,    8500.0f, 9000.0f,
                             9250.0f, 9500.0f, 10000.0f};
  const float expected[] = {12.0f, 12.0f, 12.0f, 26.5f, 41.0f, 41.0f};
  for (uint32_t i = 0U; i < COUNTOF(positions); ++i) {
    pump->demand.desiredPosition = positions[i];
    TEST_ASSERT_FLOAT_WITHIN(.0001f, expected[i], VP37_integralDeadband(pump));
  }
  pump->pid.researchUpperDeadband = false;
  TEST_ASSERT_EQUAL_FLOAT(12.0f, VP37_integralDeadband(pump));
  pump->pid.researchUpperDeadband = true;
  pump->pid.integralDeadbandTopHz = 0.0f;
  TEST_ASSERT_EQUAL_FLOAT(12.0f, VP37_integralDeadband(pump));
}

void test_upper_deadband_stops_static_windup_and_preserves_P_and_D(void) {
  VP37Pump *pump = prepareUpperDeadband();
  pump->pid.error = -30;
  pump->feedback.position = 9530;
  hal_pid_controller_reset(pump->pid.controller);
  for (uint32_t i = 0U; i < 400U; ++i) {
    TEST_ASSERT_EQUAL(
        HAL_OK, VP37_stepDampedCorrection(pump, .005f,
                                          VP37_integralDeadband(pump), 9500));
  }
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.integral);
  TEST_ASSERT_EQUAL_FLOAT(-1.5f, pump->pid.terms.proportional);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.derivative);
  pump->feedback.position += 8;
  pump->pid.error -= 8;
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_stepDampedCorrection(pump, .005f,
                                        VP37_integralDeadband(pump), 9500));
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -1.0f, pump->pid.terms.derivative);
  TEST_ASSERT_FLOAT_WITHIN(.0001f, -1.9f, pump->pid.terms.proportional);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.integral);
}

void test_upper_deadband_retains_large_error_integration_and_output_limits(
    void) {
  VP37Pump *pump = prepareUpperDeadband();
  for (int32_t sign = -1; sign <= 1; sign += 2) {
    hal_pid_controller_reset(pump->pid.controller);
    pump->pid.error = sign * 80;
    TEST_ASSERT_EQUAL(
        HAL_OK, VP37_stepDampedCorrection(pump, .005f,
                                          VP37_integralDeadband(pump), 9500));
    TEST_ASSERT_FLOAT_WITHIN(.0001f, (float)sign * .039f,
                             pump->pid.terms.integral);
  }
  hal_pid_controller_reset(pump->pid.controller);
  pump->pid.negativeLimit = -1.0f;
  pump->pid.upperLimit = 1.0f;
  pump->pid.error = 100;
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_stepDampedCorrection(pump, .005f,
                                        VP37_integralDeadband(pump), 9500));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pump->pid.terms.output);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.terms.integral);
}

void test_upper_deadband_switch_requires_released_zero_and_resets(void) {
  VP37Pump *pump = prepareUpperDeadband();
  pump->pid.researchUpperDeadband = false;
  pump->demand.atRest = true;
  pump->demand.requestedPercent = 0.0f;
  pump->output.finalPWM = 0;
  pump->pid.fullRangePrototype = false;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_UPPER_DEADBAND, 1.0f));
  pump->pid.fullRangePrototype = true;
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_UPPER_DEADBAND, 1.0f));
  pump->output.finalPWM = 1;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_UPPER_DEADBAND, 0.0f));
  VP37_stop(pump);
  TEST_ASSERT_FALSE(pump->pid.researchUpperDeadband);
  pump->pid.researchUpperDeadband = true;
  VP37_resetTuning(pump);
  TEST_ASSERT_FALSE(pump->pid.researchUpperDeadband);
}

#if VP37_TELEMETRY_ENABLED
void test_upper_deadband_telemetry_reports_the_active_band_and_input(void) {
  VP37Pump *pump = prepareUpperDeadband();
  pump->pid.integralDeadbandHz = VP37_integralDeadband(pump);
  pump->pid.error = -30;
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_stepDampedCorrection(pump, .005f,
                                        pump->pid.integralDeadbandHz, 9500));
  hal_mock_set_millis(4000U);
  VP37_publish(pump);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_readSnapshot(pump, &snapshot));
  TEST_ASSERT_TRUE(snapshot.telemetry.pid.researchUpperDeadband);
  TEST_ASSERT_EQUAL_FLOAT(41.0f, snapshot.telemetry.pid.integralDeadbandHz);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, snapshot.telemetry.pid.integralInput);
  VP37_showDebug(pump, &snapshot);
}
#endif

/** @brief Run a moving-reference case with feedback deliberately ahead or late.
 */
static float rampBrakingTrim(bool enabled, bool rising, bool leading,
                             bool fullProfile = true) {
  VP37Pump *pump = vp37TestPump();
  if (pump->pid.controller != NULL) {
    hal_pid_controller_destroy(pump->pid.controller);
    pump->pid.controller = NULL;
  }
  setUpVp37Fixture();
  setupPumpForProcessTests(pump);
  pump->pid.fullRangePrototype = fullProfile;
  pump->pid.researchRampBraking = enabled;
  pump->pid.researchReferenceWeight = 1.0f;
  pump->demand.physicalLimitPercent = 100.0f;
  pump->feedback.adjustMin = 0;
  pump->feedback.adjustMax = 10000;
  VP37_setPositionDemandPercentage(pump, rising ? 0.0f : 90.0f);
  injectAdjRegisterData(rising ? 0 : 9000, 144, 49, ADJ_STATUS_OK);
  VP37_process(pump);
  VP37_setPositionDemandPercentage(pump, rising ? 90.0f : 30.0f);
  for (uint32_t ms = 5U; ms <= 150U; ms += 5U) {
    hal_mock_set_millis(ms);
    const int32_t offset = (rising == leading) ? 500 : -500;
    const int32_t position =
        hal_constrain(pump->demand.desired + offset, 0, 10000);
    injectAdjRegisterData((int16_t)position, 144, 49, ADJ_STATUS_OK);
    VP37_process(pump);
    TEST_ASSERT_NOT_EQUAL(pump->demand.target, pump->demand.desired);
  }
  return pump->pid.terms.integral;
}

void test_ramp_braking_opposes_upward_reference_lead(void) {
  TEST_ASSERT_LESS_THAN_FLOAT(-8.0f, rampBrakingTrim(true, true, true));
}

void test_ramp_braking_opposes_downward_reference_lead(void) {
  TEST_ASSERT_GREATER_THAN_FLOAT(8.0f, rampBrakingTrim(true, false, true));
}

void test_ramp_braking_does_not_integrate_upward_tracking_lag(void) {
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, rampBrakingTrim(true, true, false));
}

void test_ramp_braking_does_not_integrate_downward_tracking_lag(void) {
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, rampBrakingTrim(true, false, false));
}

void test_ramp_braking_off_preserves_the_existing_upward_rule(void) {
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, rampBrakingTrim(false, true, true));
}

void test_ramp_braking_off_preserves_the_existing_downward_rule(void) {
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, rampBrakingTrim(false, false, true));
}

void test_ramp_braking_requires_the_full_profile(void) {
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f,
                           rampBrakingTrim(true, true, true, false));
}

void test_ramp_braking_switch_requires_released_zero_and_resets(void) {
  VP37Pump *pump = prepareUpperDeadband();
  pump->demand.atRest = true;
  pump->demand.requestedPercent = 0.0f;
  pump->output.finalPWM = 0;
  pump->pid.fullRangePrototype = false;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_RAMP_BRAKING, 1.0f));
  pump->pid.fullRangePrototype = true;
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_RAMP_BRAKING, 1.0f));
  pump->output.finalPWM = 1;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_RAMP_BRAKING, 0.0f));
  VP37_stop(pump);
  TEST_ASSERT_FALSE(pump->pid.researchRampBraking);
  pump->pid.researchRampBraking = true;
  VP37_resetTuning(pump);
  TEST_ASSERT_FALSE(pump->pid.researchRampBraking);
}

#if VP37_TELEMETRY_ENABLED
void test_ramp_braking_telemetry_reports_the_selected_mode(void) {
  VP37Pump *pump = prepareUpperDeadband();
  pump->pid.researchRampBraking = true;
  hal_mock_set_millis(5000U);
  VP37_publish(pump);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_readSnapshot(pump, &snapshot));
  TEST_ASSERT_TRUE(snapshot.telemetry.pid.researchRampBraking);
  VP37_showDebug(pump, &snapshot);
}
#endif

static VP37Pump *prepareDescentUnwind(void) {
  VP37Pump *pump = vp37TestPump();
  prepareReferencePump(pump, 1.0f, 10000);
  hal_pid_controller_set_max_integral(pump->pid.controller, 600.0f);
  hal_pid_controller_set_output_limits(pump->pid.controller, -220.0f, 340.0f);
  hal_pid_controller_set_kd(pump->pid.controller, 0.0f);
  for (uint32_t i = 0U; i < 100U; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, hal_pid_controller_step_ex(
                                  pump->pid.controller, -1000.0f, 10000.0f,
                                  .005f, 0.0f, &pump->pid.terms));
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, -100.0f, pump->pid.terms.integral);
  pump->pid.researchDescentIntegralUnwind = true;
  pump->pid.researchMotionIntegralGate = true;
  pump->pid.motionIntegralHold = true;
  pump->pid.motionIntegralBias = -20.0f;
  pump->pid.motionIntegralStartPosition = 10000;
  pump->pid.descentUnwindTarget = -60.0f;
  pump->pid.descentUnwindPhase = VP37_DESCENT_UNWIND_ACTIVE;
  pump->pid.integralHold = false;
  pump->pid.error = -400;
  pump->demand.target = 9500;
  pump->demand.desired = 9500;
  return pump;
}

void test_descent_unwind_follows_actual_feedback_and_zero_still_releases(void) {
  verifyMotionIntegralApproach(-1, true);
}

void test_descent_unwind_returns_selected_bias_and_preserves_P(void) {
  VP37Pump *pump = prepareDescentUnwind();
  for (uint32_t i = 0U; i < 200U; ++i) {
    const float previous = pump->pid.terms.integral;
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(pump, .005f, 400.0f, 9500));
    TEST_ASSERT_TRUE(pump->pid.terms.integral >= previous);
    TEST_ASSERT_TRUE(pump->pid.terms.integral <= -60.0f);
    TEST_ASSERT_FLOAT_WITHIN(.001f, -.05f * 400.0f,
                             pump->pid.terms.proportional);
    TEST_ASSERT_FLOAT_WITHIN(.001f,
                             pump->pid.terms.proportional +
                                 pump->pid.terms.integral +
                                 pump->pid.terms.derivative,
                             pump->pid.terms.unconstrained);
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, -60.0f, pump->pid.terms.integral);
}

void test_descent_unwind_removes_three_quarters_before_a_fast_crossing(void) {
  VP37Pump *pump = prepareDescentUnwind();
  for (uint32_t i = 0U; i < 3U; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(pump, .005f, 400.0f, 9500));
  }
  TEST_ASSERT_TRUE(pump->pid.terms.integral >= -70.0f);
  TEST_ASSERT_TRUE(pump->pid.terms.integral <= -60.0f);
}

void test_descent_unwind_obeys_hold_freeze_motion_and_profile_rules(void) {
  for (uint32_t mode = 0U; mode < 8U; ++mode) {
    if (mode > 0U) {
      tearDown();
      setUp();
    }
    VP37Pump *pump = prepareDescentUnwind();
    if (mode == 0U)
      pump->pid.researchDescentIntegralUnwind = false;
    if (mode == 1U)
      pump->pid.fullRangePrototype = false;
    if (mode == 2U)
      pump->pid.motionIntegralHold = false;
    if (mode == 3U)
      pump->pid.integralHold = true;
    if (mode == 4U)
      pump->pid.researchFreezeMask = 1U;
    if (mode == 5U)
      pump->pid.error = 400;
    if (mode == 6U)
      pump->pid.descentUnwindPhase = VP37_DESCENT_UNWIND_WAITING;
    if (mode == 7U)
      pump->pid.descentUnwindPhase = VP37_DESCENT_UNWIND_ARRIVED;
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(pump, .005f, 400.0f, 9500));
    TEST_ASSERT_FLOAT_WITHIN(.001f, -100.0f, pump->pid.terms.integral);
  }
}

void test_descent_unwind_keeps_braking_bias_and_full_integral_authority(void) {
  VP37Pump *pump = prepareDescentUnwind();
  pump->pid.descentUnwindTarget = -110.0f;
  TEST_ASSERT_EQUAL(HAL_OK,
                    VP37_stepDampedCorrection(pump, .005f, 400.0f, 9500));
  TEST_ASSERT_FLOAT_WITHIN(.001f, -100.0f, pump->pid.terms.integral);
  pump->pid.error = 400;
  pump->pid.motionIntegralHold = false;
  for (uint32_t i = 0U; i < 1000U; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(pump, .005f, 0.0f, 9500));
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, 120.0f, pump->pid.terms.integral);
  pump->pid.error = -400;
  for (uint32_t i = 0U; i < 1000U; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(pump, .005f, 0.0f, 9500));
  }
  TEST_ASSERT_FLOAT_WITHIN(.001f, -120.0f, pump->pid.terms.integral);
}

void test_descent_unwind_respects_complete_command_antiwindup(void) {
  VP37Pump *pump = prepareDescentUnwind();
  // With the faster return, allow its first20PWM step and reject the next10.
  pump->pid.upperLimit = -95.0f;
  for (uint32_t i = 0U; i < 40U; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(pump, .005f, 400.0f, 9500));
    TEST_ASSERT_TRUE(pump->pid.terms.output <= -95.0f);
  }
  TEST_ASSERT_TRUE(pump->pid.terms.integral > -100.0f);
  TEST_ASSERT_TRUE(pump->pid.terms.integral < -75.0f);
}

void test_descent_unwind_baseline_changes_only_with_the_demand(void) {
  VP37Pump *pump = prepareDescentUnwind();
  VP37_resetMotionIntegralGate(pump);
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_EQUAL_FLOAT(-100.0f, pump->pid.motionIntegralBias);
  pump->pid.terms.integral = -80.0f;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_EQUAL_FLOAT(-100.0f, pump->pid.motionIntegralBias);
  pump->demand.target = 9000;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_EQUAL_FLOAT(-80.0f, pump->pid.motionIntegralBias);
  VP37_resetMotionIntegralGate(pump);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.motionIntegralBias);
  TEST_ASSERT_FALSE(pump->pid.motionIntegralReady);
}

static VP37Pump *prepareDescentQualification(void) {
  VP37Pump *pump = prepareDescentUnwind();
  VP37_resetMotionIntegralGate(pump);
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 10100;
  pump->feedback.position = 10000;
  pump->pid.integralDeadbandHz = 61.0f;
  pump->pid.terms.integral = -20.0f;
  pump->pid.error = -500;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  pump->pid.terms.integral = -100.0f;
  return pump;
}

void test_descent_unwind_ignores_small_jitter_and_latches_partial_target(void) {
  VP37Pump *pump = prepareDescentQualification();
  pump->feedback.position = 9990;
  pump->pid.error = -490;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_TRUE(pump->pid.motionIntegralHold);
  TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_WAITING, pump->pid.descentUnwindPhase);
  pump->feedback.position = 9970;
  pump->pid.error = -470;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_ACTIVE, pump->pid.descentUnwindPhase);
  TEST_ASSERT_EQUAL_FLOAT(-52.0f, pump->pid.descentUnwindTarget);
  pump->pid.terms.integral = -110.0f;
  pump->feedback.position = 9900;
  pump->pid.error = -400;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_EQUAL_FLOAT(-52.0f, pump->pid.descentUnwindTarget);
}

void test_descent_unwind_first_arrival_blocks_reactivation_until_new_target(
    void) {
  for (uint32_t mode = 0U; mode < 2U; ++mode) {
    if (mode > 0U) {
      tearDown();
      setUp();
    }
    VP37Pump *pump = prepareDescentQualification();
    pump->feedback.position = 9970;
    pump->pid.error = -470;
    (void)VP37_motionIntegralHold(pump, true, .005f);
    TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_ACTIVE, pump->pid.descentUnwindPhase);
    // Include a jump across the entire I band between feedback samples.
    pump->feedback.position = mode == 0U ? 9550 : 9400;
    pump->pid.error = 9500 - pump->feedback.position;
    (void)VP37_motionIntegralHold(pump, true, .005f);
    TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_ARRIVED,
                      pump->pid.descentUnwindPhase);
    pump->feedback.position = 10000;
    pump->pid.error = -500;
    (void)VP37_motionIntegralHold(pump, true, .005f);
    pump->feedback.position = 9700;
    pump->pid.error = -200;
    (void)VP37_motionIntegralHold(pump, true, .005f);
    TEST_ASSERT_TRUE(pump->pid.motionIntegralHold);
    TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_ARRIVED,
                      pump->pid.descentUnwindPhase);
    const float integral = pump->pid.terms.integral;
    TEST_ASSERT_EQUAL(HAL_OK,
                      VP37_stepDampedCorrection(pump, .005f, 400.0f, 9500));
    TEST_ASSERT_EQUAL_FLOAT(integral, pump->pid.terms.integral);
    pump->demand.target = 9000;
    pump->demand.desired = 9000;
    pump->pid.error = -700;
    (void)VP37_motionIntegralHold(pump, true, .005f);
    TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_WAITING,
                      pump->pid.descentUnwindPhase);
    TEST_ASSERT_EQUAL_FLOAT(integral, pump->pid.motionIntegralBias);
    TEST_ASSERT_EQUAL_INT32(9700, pump->pid.motionIntegralStartPosition);
  }
}

void test_descent_unwind_qualification_requires_settled_negative_acquired_bias(
    void) {
  for (uint32_t mode = 0U; mode < 3U; ++mode) {
    if (mode > 0U) {
      tearDown();
      setUp();
    }
    VP37Pump *pump = prepareDescentQualification();
    pump->feedback.position = 9950;
    pump->pid.error = -450;
    if (mode == 0U)
      pump->pid.terms.integral = -10.0f;
    if (mode == 1U)
      pump->pid.researchFreezeMask = 1U;
    (void)VP37_motionIntegralHold(pump, mode != 2U, .005f);
    TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_WAITING,
                      pump->pid.descentUnwindPhase);
  }
}

#if ECU_FUNCTIONAL_TESTS_ENABLED
void test_descent_trace_waits_beyond_buffer_duration_and_retains_previous_step(
    void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  pump->pid.fullRangePrototype = true;
  pump->pid.researchMotionIntegralGate = true;
  pump->feedback.adjustMin = 0;
  pump->feedback.adjustMax = 10000;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_setPositionDemandPercentage(pump, 95.0f));
  TEST_ASSERT_EQUAL(HAL_OK, VP37_startTraceOnDescent(pump));
  VP37TraceSample sample = {};
  for (uint32_t i = 1U; i <= 1500U; ++i) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData(10000, 144, 26, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_TRUE(VP37_traceCapturing());
  TEST_ASSERT_EQUAL(HAL_EAGAIN, VP37_readTrace(&sample));
  TEST_ASSERT_EQUAL(HAL_EBUSY, VP37_startTrace(pump));
  for (uint32_t i = 1501U; i < 1500U + VP37_TRACE_SAMPLES; ++i) {
    hal_mock_set_millis(i * 5U);
    injectAdjRegisterData(9970, 144, 26, ADJ_STATUS_OK);
    VP37_process(pump);
  }
  TEST_ASSERT_FALSE(VP37_traceCapturing());
  for (uint32_t i = 0U; i < VP37_TRACE_SAMPLES; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, VP37_readTrace(&sample));
    TEST_ASSERT_EQUAL_UINT32(1500U + i, sample.sequence);
    TEST_ASSERT_EQUAL_UINT32((1500U + i) * 5000U, sample.us);
    TEST_ASSERT_EQUAL_INT32(i == 0U ? 10000 : 9970, sample.measured);
  }
  TEST_ASSERT_EQUAL(HAL_ENOENT, VP37_readTrace(&sample));
  uint32_t completed = 77U;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_takeTraceCompletion(&completed));
  TEST_ASSERT_EQUAL_UINT32(VP37_TRACE_SAMPLES, completed);
  TEST_ASSERT_EQUAL(HAL_EAGAIN, VP37_takeTraceCompletion(&completed));
  TEST_ASSERT_EQUAL_UINT32(VP37_TRACE_SAMPLES, completed);
}

void test_descent_trace_armed_release_and_explicit_stop_finish_partial_capture(
    void) {
  for (uint32_t mode = 0U; mode < 2U; ++mode) {
    if (mode > 0U) {
      tearDown();
      setUp();
    }
    VP37Pump *pump = prepareDescentQualification();
    pump->demand.atRest = false;
    TEST_ASSERT_EQUAL(HAL_OK, VP37_startTraceOnDescent(pump));
    VP37_publish(pump);
    VP37_traceRecord(pump);
    TEST_ASSERT_TRUE(VP37_traceCapturing());
    if (mode == 0U) {
      pump->demand.atRest = true;
      VP37_traceRecord(pump);
    } else {
      VP37_traceStop();
    }
    TEST_ASSERT_FALSE(VP37_traceCapturing());
    VP37TraceSample sample = {};
    uint32_t count = 0U;
    while (VP37_readTrace(&sample) == HAL_OK) {
      ++count;
    }
    TEST_ASSERT_EQUAL_UINT32(mode == 0U ? 2U : 1U, count);
    uint32_t completed = 77U;
    TEST_ASSERT_EQUAL(HAL_OK, VP37_takeTraceCompletion(&completed));
    TEST_ASSERT_EQUAL_UINT32(count, completed);
    TEST_ASSERT_EQUAL(HAL_OK, VP37_startTrace(pump));
    VP37_traceStop();
    TEST_ASSERT_EQUAL(HAL_ENOENT, VP37_readTrace(&sample));
  }
}

void test_descent_trace_empty_completion_preserves_pending_on_invalid_output(
    void) {
  VP37Pump *pump = prepareDescentQualification();
  TEST_ASSERT_EQUAL(HAL_OK, VP37_startTraceOnDescent(pump));
  VP37_traceStop();
  VP37TraceSample sample = {};
  TEST_ASSERT_EQUAL(HAL_ENOENT, VP37_readTrace(&sample));
  TEST_ASSERT_EQUAL(HAL_EINVAL, VP37_takeTraceCompletion(NULL));
  uint32_t completed = 77U;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_takeTraceCompletion(&completed));
  TEST_ASSERT_EQUAL_UINT32(0U, completed);
  TEST_ASSERT_EQUAL(HAL_EAGAIN, VP37_takeTraceCompletion(&completed));
  TEST_ASSERT_EQUAL_UINT32(0U, completed);
}

void test_descent_trace_requires_running_full_profile_and_motion_gate(void) {
  VP37Pump *pump = prepareDescentQualification();
  TEST_ASSERT_EQUAL(HAL_EINVAL, VP37_startTraceOnDescent(NULL));
  pump->pid.fullRangePrototype = false;
  TEST_ASSERT_EQUAL(HAL_ESTATE, VP37_startTraceOnDescent(pump));
  pump->pid.fullRangePrototype = true;
  pump->pid.researchMotionIntegralGate = false;
  TEST_ASSERT_EQUAL(HAL_ESTATE, VP37_startTraceOnDescent(pump));
  pump->pid.researchMotionIntegralGate = true;
  pump->vp37Initialized = false;
  TEST_ASSERT_EQUAL(HAL_EAGAIN, VP37_startTraceOnDescent(pump));
  TEST_ASSERT_FALSE(VP37_traceCapturing());
}
#endif

static VP37Pump *prepareInteriorHolding(void) {
  VP37Pump *pump = prepareDescentUnwind();
  VP37_resetMotionIntegralGate(pump);
  pump->pid.integralDeadbandHz = 61.0f;
  pump->pid.terms.integral = -53.6f;
  pump->pid.integralLimit = 240.0f;
  pump->feedback.position = 9540;
  pump->pid.error = -40;
  pump->output.finalPWM = 700;
  return pump;
}

static VP37Pump *prepareMeasuredHoldingReturn(void) {
  VP37Pump *pump = prepareInteriorHolding();
  for (uint32_t i = 0U; i < 45U; ++i) {
    (void)VP37_motionIntegralHold(pump, true, .05f);
  }
  pump->demand.target = 10000;
  pump->demand.desired = 10000;
  pump->pid.error = 460;
  (void)VP37_motionIntegralHold(pump, false, .005f);
  pump->feedback.position = 10000;
  pump->pid.error = 0;
  pump->pid.terms.integral = 240.0f;
  for (uint32_t i = 0U; i < 45U; ++i) {
    (void)VP37_motionIntegralHold(pump, true, .05f);
  }
  pump->demand.target = 9500;
  pump->demand.desired = 9500;
  pump->pid.error = -500;
  (void)VP37_motionIntegralHold(pump, false, .005f);
  return pump;
}

void test_holding_bias_requires_two_seconds_and_keeps_control_terms(void) {
  VP37Pump *pump = prepareInteriorHolding();
  for (uint32_t i = 0U; i < 39U; ++i) {
    (void)VP37_motionIntegralHold(pump, true, .05f);
  }
  TEST_ASSERT_FALSE(pump->pid.holdingIntegralReady);
  for (uint32_t i = 0U; i < 4U; ++i) {
    (void)VP37_motionIntegralHold(pump, true, .05f);
  }
  TEST_ASSERT_TRUE(pump->pid.holdingIntegralReady);
  TEST_ASSERT_EQUAL(9500, pump->pid.holdingIntegralTarget);
  TEST_ASSERT_EQUAL_FLOAT(-53.6f, pump->pid.holdingIntegralBias);
  TEST_ASSERT_EQUAL_FLOAT(-53.6f, pump->pid.terms.integral);
  TEST_ASSERT_EQUAL(700, pump->output.finalPWM);
}

void test_holding_bias_rejects_endpoint_transient_freeze_and_saturation(void) {
  for (uint32_t fault = 0U; fault < 11U; ++fault) {
    if (fault > 0U) {
      tearDown();
      setUp();
    }
    VP37Pump *pump = prepareInteriorHolding();
    if (fault == 0U) {
      pump->demand.target = 10000;
      pump->feedback.position = 10040;
    }
    if (fault == 1U) {
      pump->demand.target = 0;
      pump->feedback.position = 40;
    }
    if (fault == 2U) {
      pump->pid.error = -62;
    }
    if (fault == 3U) {
      pump->pid.researchFreezeMask = VP37_RESEARCH_FREEZE_INTEGRAL_MASK;
    }
    if (fault == 4U) {
      pump->pid.terms.saturated_high = true;
    }
    if (fault == 5U) {
      pump->pid.terms.saturated_low = true;
    }
    if (fault == 6U) {
      pump->output.finalPWM = 0;
    }
    if (fault == 7U) {
      pump->pid.terms.integral = -240.0f;
    }
    if (fault == 8U) {
      pump->pid.terms.integral = NAN;
    }
    if (fault == 10U) {
      pump->output.finalPWM = VP37_PWM_MIN;
    }
    for (uint32_t i = 0U; i < 45U; ++i) {
      (void)VP37_motionIntegralHold(pump, fault != 9U, .05f);
    }
    TEST_ASSERT_FALSE(pump->pid.holdingIntegralReady);
  }
}

void test_holding_bias_excursion_and_I_drift_restart_qualification(void) {
  for (uint32_t fault = 0U; fault < 2U; ++fault) {
    if (fault > 0U) {
      tearDown();
      setUp();
    }
    VP37Pump *pump = prepareInteriorHolding();
    for (uint32_t i = 0U; i < 35U; ++i) {
      (void)VP37_motionIntegralHold(pump, true, .05f);
    }
    if (fault == 0U) {
      pump->pid.error = -62;
    } else {
      pump->pid.terms.integral = -54.2f;
    }
    (void)VP37_motionIntegralHold(pump, true, .05f);
    pump->pid.error = -40;
    for (uint32_t i = 0U; i < 10U; ++i) {
      (void)VP37_motionIntegralHold(pump, true, .05f);
    }
    TEST_ASSERT_FALSE(pump->pid.holdingIntegralReady);
  }
}

void test_holding_bias_is_used_only_for_its_measured_target(void) {
  VP37Pump *pump = prepareMeasuredHoldingReturn();
  pump->demand.target = 9400;
  pump->demand.desired = 9400;
  pump->pid.error = -600;
  (void)VP37_motionIntegralHold(pump, false, .005f);
  pump->pid.terms.integral = -87.4f;
  pump->feedback.position = 9970;
  pump->pid.error = -570;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_FALSE(pump->pid.descentUnwindUsesHolding);
  TEST_ASSERT_FLOAT_WITHIN(.001f, -34.96f, pump->pid.descentUnwindTarget);
}

void test_holding_bias_does_not_add_negative_drive(void) {
  VP37Pump *pump = prepareMeasuredHoldingReturn();
  pump->pid.terms.integral = -40.0f;
  pump->feedback.position = 9970;
  pump->pid.error = -470;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_TRUE(pump->pid.descentUnwindUsesHolding);
  TEST_ASSERT_EQUAL_FLOAT(-40.0f, pump->pid.descentUnwindTarget);
}

void test_holding_bias_is_invalidated_by_tuning_stop_and_invalid_dt(void) {
  for (uint32_t fault = 0U; fault < 3U; ++fault) {
    if (fault > 0U) {
      tearDown();
      setUp();
    }
    VP37Pump *pump = prepareMeasuredHoldingReturn();
    TEST_ASSERT_TRUE(pump->pid.holdingIntegralReady);
    if (fault == 0U) {
      TEST_ASSERT_EQUAL(HAL_OK, VP37_setTuning(pump, VP37_TUNING_KP, .05f));
    }
    if (fault == 1U) {
      VP37_stop(pump);
    }
    if (fault == 2U) {
      (void)VP37_motionIntegralHold(pump, true, NAN);
    }
    TEST_ASSERT_FALSE(pump->pid.holdingIntegralReady);
  }
}

#if VP37_TELEMETRY_ENABLED
void test_holding_bias_telemetry_reports_measured_target_and_use(void) {
  VP37Pump *pump = prepareMeasuredHoldingReturn();
  pump->pid.terms.integral = -87.4f;
  pump->feedback.position = 9970;
  pump->pid.error = -470;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  hal_mock_set_millis(9000U);
  VP37_publish(pump);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_readSnapshot(pump, &snapshot));
  TEST_ASSERT_TRUE(snapshot.telemetry.pid.holdingIntegralReady);
  TEST_ASSERT_TRUE(snapshot.telemetry.pid.descentUnwindUsesHolding);
  TEST_ASSERT_EQUAL(9500, snapshot.telemetry.pid.holdingIntegralTarget);
  TEST_ASSERT_FLOAT_WITHIN(.001f, -53.6f,
                           snapshot.telemetry.pid.holdingIntegralBias);
  VP37_showDebug(pump, &snapshot);
}
#endif

void test_descent_return_uses_measured_interior_holding_bias(void) {
  VP37Pump *pump = prepareMeasuredHoldingReturn();
  pump->pid.terms.integral = -87.4f;
  pump->feedback.position = 9970;
  pump->pid.error = -470;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_ACTIVE, pump->pid.descentUnwindPhase);
  TEST_ASSERT_FLOAT_WITHIN(.001f, -53.6f, pump->pid.descentUnwindTarget);
}

void test_descent_from_calibration_max_ignores_arbitrary_previous_integral(
    void) {
  const float previous[] = {120.0f, 0.0f, -20.0f, -180.0f};
  for (uint32_t i = 0U; i < COUNTOF(previous); ++i) {
    if (i > 0U) {
      tearDown();
      setUp();
    }
    VP37Pump *pump = prepareDescentUnwind();
    VP37_resetMotionIntegralGate(pump);
    pump->pid.integralDeadbandHz = 61.0f;
    pump->pid.terms.integral = previous[i];
    pump->feedback.position = 10000;
    pump->pid.error = -500;
    (void)VP37_motionIntegralHold(pump, true, .005f);
    TEST_ASSERT_EQUAL_FLOAT(previous[i], pump->pid.motionIntegralBias);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.motionIntegralAnchor);
    pump->pid.terms.integral = -158.0f;
    pump->feedback.position = 9970;
    pump->pid.error = -470;
    (void)VP37_motionIntegralHold(pump, true, .005f);
    TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_ACTIVE, pump->pid.descentUnwindPhase);
    TEST_ASSERT_FLOAT_WITHIN(.001f, -63.2f, pump->pid.descentUnwindTarget);
  }
}

void test_descent_anchor_remembers_maximum_demand_across_feedback_jitter(void) {
  VP37Pump *pump = prepareDescentUnwind();
  VP37_resetMotionIntegralGate(pump);
  pump->demand.target = 10000;
  pump->demand.desired = 10000;
  pump->feedback.position = 10010;
  pump->pid.error = -10;
  pump->pid.terms.integral = 120.0f;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  pump->demand.target = 9500;
  pump->demand.desired = 9500;
  pump->feedback.position = 9999;
  pump->pid.error = -499;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_EQUAL_FLOAT(120.0f, pump->pid.motionIntegralBias);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.motionIntegralAnchor);
  pump->pid.terms.integral = -158.0f;
  pump->feedback.position = 9969;
  pump->pid.error = -469;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_ACTIVE, pump->pid.descentUnwindPhase);
  TEST_ASSERT_FLOAT_WITHIN(.001f, -63.2f, pump->pid.descentUnwindTarget);
}

void test_descent_endpoint_anchor_preserves_positive_braking_bias(void) {
  VP37Pump *pump = prepareDescentUnwind();
  VP37_resetMotionIntegralGate(pump);
  pump->feedback.position = 10000;
  pump->pid.error = -500;
  pump->pid.terms.integral = 120.0f;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  pump->feedback.position = 9970;
  pump->pid.error = -470;
  pump->pid.terms.integral = 20.0f;
  (void)VP37_motionIntegralHold(pump, true, .005f);
  TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_WAITING, pump->pid.descentUnwindPhase);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, pump->pid.terms.integral);
}

void test_descent_share_selects_the_declared_acquired_bias_fraction(void) {
  const float shares[] = {.25f, .4f, .5f, .75f};
  const float expected[] = {-40.0f, -52.0f, -60.0f, -80.0f};
  for (uint32_t i = 0U; i < COUNTOF(shares); ++i) {
    if (i > 0U) {
      tearDown();
      setUp();
    }
    VP37Pump *pump = prepareDescentQualification();
    pump->pid.descentIntegralRetainedShare = shares[i];
    pump->feedback.position = 9970;
    pump->pid.error = -470;
    (void)VP37_motionIntegralHold(pump, true, .005f);
    TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_ACTIVE, pump->pid.descentUnwindPhase);
    TEST_ASSERT_FLOAT_WITHIN(.001f, expected[i], pump->pid.descentUnwindTarget);
    TEST_ASSERT_TRUE(pump->pid.descentUnwindTarget <
                     pump->pid.motionIntegralBias);
    TEST_ASSERT_TRUE(pump->pid.descentUnwindTarget > pump->pid.terms.integral);
  }
}

void test_descent_share_requires_released_zero_and_defaults_reset(void) {
  VP37Pump *pump = prepareDescentUnwind();
  TEST_ASSERT_EQUAL_FLOAT(
      .4f, VP37_getTuning(pump, VP37_TUNING_RESEARCH_DESCENT_SHARE));
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_DESCENT_SHARE, .5f));
  pump->demand.atRest = true;
  pump->demand.requestedPercent = 0.0f;
  pump->output.finalPWM = 0;
  pump->pid.fullRangePrototype = false;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_DESCENT_SHARE, .5f));
  pump->pid.fullRangePrototype = true;
  assertTuningRefused(pump, VP37_TUNING_RESEARCH_DESCENT_SHARE, .1f);
  assertTuningRefused(pump, VP37_TUNING_RESEARCH_DESCENT_SHARE, .9f);
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_DESCENT_SHARE, .5f));
  TEST_ASSERT_EQUAL_FLOAT(
      .5f, VP37_getTuning(pump, VP37_TUNING_RESEARCH_DESCENT_SHARE));
  TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_WAITING, pump->pid.descentUnwindPhase);
  VP37_stop(pump);
  TEST_ASSERT_EQUAL_FLOAT(
      .4f, VP37_getTuning(pump, VP37_TUNING_RESEARCH_DESCENT_SHARE));
  pump->pid.descentIntegralRetainedShare = .75f;
  VP37_resetTuning(pump);
  TEST_ASSERT_EQUAL_FLOAT(
      .4f, VP37_getTuning(pump, VP37_TUNING_RESEARCH_DESCENT_SHARE));
}

void test_descent_unwind_switch_requires_released_zero_and_resets(void) {
  VP37Pump *pump = prepareDescentUnwind();
  pump->pid.researchDescentIntegralUnwind = false;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_DESCENT_UNWIND, 1.0f));
  pump->demand.atRest = true;
  pump->demand.requestedPercent = 0.0f;
  pump->output.finalPWM = 0;
  pump->pid.fullRangePrototype = false;
  TEST_ASSERT_EQUAL(
      HAL_ESTATE,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_DESCENT_UNWIND, 1.0f));
  pump->pid.fullRangePrototype = true;
  TEST_ASSERT_EQUAL(
      HAL_EINVAL,
      VP37_setTuning(pump, VP37_TUNING_RESEARCH_DESCENT_UNWIND, .5f));
  TEST_ASSERT_EQUAL(
      HAL_OK, VP37_setTuning(pump, VP37_TUNING_RESEARCH_DESCENT_UNWIND, 1.0f));
  TEST_ASSERT_EQUAL_FLOAT(
      1.0f, VP37_getTuning(pump, VP37_TUNING_RESEARCH_DESCENT_UNWIND));
  VP37_stop(pump);
  TEST_ASSERT_FALSE(pump->pid.researchDescentIntegralUnwind);
  pump->pid.researchDescentIntegralUnwind = true;
  VP37_resetTuning(pump);
  TEST_ASSERT_FALSE(pump->pid.researchDescentIntegralUnwind);
}

#if VP37_TELEMETRY_ENABLED
void test_descent_share_telemetry_reports_selected_fraction(void) {
  VP37Pump *pump = prepareDescentUnwind();
  pump->pid.descentIntegralRetainedShare = .5f;
  hal_mock_set_millis(7000U);
  VP37_publish(pump);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_readSnapshot(pump, &snapshot));
  TEST_ASSERT_EQUAL_FLOAT(.5f,
                          snapshot.telemetry.pid.descentIntegralRetainedShare);
  VP37_showDebug(pump, &snapshot);
}
#endif

#if VP37_TELEMETRY_ENABLED
void test_descent_unwind_telemetry_reports_flag_and_saved_bias(void) {
  VP37Pump *pump = prepareDescentUnwind();
  hal_mock_set_millis(7000U);
  VP37_publish(pump);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL(HAL_OK, VP37_readSnapshot(pump, &snapshot));
  TEST_ASSERT_TRUE(snapshot.telemetry.pid.researchDescentIntegralUnwind);
  TEST_ASSERT_EQUAL_FLOAT(-20.0f, snapshot.telemetry.pid.motionIntegralBias);
  TEST_ASSERT_EQUAL(VP37_DESCENT_UNWIND_ACTIVE,
                    snapshot.telemetry.pid.descentUnwindPhase);
  TEST_ASSERT_EQUAL_FLOAT(-60.0f, snapshot.telemetry.pid.descentUnwindTarget);
  VP37_showDebug(pump, &snapshot);
}
#endif

/** @brief A new start of the pump within one test: releases the controller
 * of the previous setup first, as tearDown() does. */
static void restartPumpForProcessTests(VP37Pump *pump) {
  if (pump->pid.controller != NULL) {
    hal_pid_controller_destroy(pump->pid.controller);
    pump->pid.controller = NULL;
  }
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50.0f);
}

/** @brief One control step at the given hal_micros(), with fresh feedback. */
static void controlStepAt(VP37Pump *pump, uint32_t us) {
  hal_mock_set_micros(us);
  injectAdjRegisterData(4600, 144, 26, ADJ_STATUS_OK);
  VP37_process(pump);
}

void test_vp37_control_grid_keeps_the_deadline_after_a_late_start(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50.0f);
  // The first step starts the grid now and counts a whole period.
  controlStepAt(pump, 100000U);
  TEST_ASSERT_EQUAL_UINT32(1U, pump->controlSequence);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->controlLateUs);
  TEST_ASSERT_EQUAL_UINT32(5000U, pump->controlDtUs);
  TEST_ASSERT_EQUAL_UINT32(105000U, pump->controlDueUs);
  // A step 700 us late measures the real period, but the next deadline stays
  // on the grid instead of moving by the delay.
  controlStepAt(pump, 105700U);
  TEST_ASSERT_EQUAL_UINT32(2U, pump->controlSequence);
  TEST_ASSERT_EQUAL_UINT32(700U, pump->controlLateUs);
  TEST_ASSERT_EQUAL_UINT32(5700U, pump->controlDtUs);
  TEST_ASSERT_EQUAL_UINT32(110000U, pump->controlDueUs);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->controlOverruns);
  // Nothing runs before the deadline; on it the short period closes the gap.
  controlStepAt(pump, 109999U);
  TEST_ASSERT_EQUAL_UINT32(2U, pump->controlSequence);
  controlStepAt(pump, 110000U);
  TEST_ASSERT_EQUAL_UINT32(3U, pump->controlSequence);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->controlLateUs);
  TEST_ASSERT_EQUAL_UINT32(4300U, pump->controlDtUs);
  TEST_ASSERT_EQUAL_UINT32(115000U, pump->controlDueUs);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->controlOverruns);
}

void test_vp37_control_grid_restarts_after_a_whole_period_late(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50.0f);
  controlStepAt(pump, 100000U);
  // A stall of more than a period: one step, counted, and a new grid from
  // now instead of a burst of catch-up steps.
  controlStepAt(pump, 112000U);
  TEST_ASSERT_EQUAL_UINT32(2U, pump->controlSequence);
  TEST_ASSERT_EQUAL_UINT32(7000U, pump->controlLateUs);
  TEST_ASSERT_EQUAL_UINT32(12000U, pump->controlDtUs);
  TEST_ASSERT_EQUAL_UINT32(117000U, pump->controlDueUs);
  TEST_ASSERT_EQUAL_UINT32(1U, pump->controlOverruns);
  controlStepAt(pump, 116999U);
  TEST_ASSERT_EQUAL_UINT32(2U, pump->controlSequence);
  controlStepAt(pump, 117000U);
  TEST_ASSERT_EQUAL_UINT32(3U, pump->controlSequence);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->controlLateUs);
  TEST_ASSERT_EQUAL_UINT32(122000U, pump->controlDueUs);
  // Exactly a period late is an overrun as well.
  controlStepAt(pump, 127000U);
  TEST_ASSERT_EQUAL_UINT32(4U, pump->controlSequence);
  TEST_ASSERT_EQUAL_UINT32(5000U, pump->controlLateUs);
  TEST_ASSERT_EQUAL_UINT32(132000U, pump->controlDueUs);
  TEST_ASSERT_EQUAL_UINT32(2U, pump->controlOverruns);
}

void test_vp37_control_grid_crosses_the_microsecond_wrap(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50.0f);
  controlStepAt(pump, UINT32_MAX - 100U);
  TEST_ASSERT_EQUAL_UINT32(4899U, pump->controlDueUs);
  controlStepAt(pump, 4898U);
  TEST_ASSERT_EQUAL_UINT32(1U, pump->controlSequence);
  controlStepAt(pump, 5100U);
  TEST_ASSERT_EQUAL_UINT32(2U, pump->controlSequence);
  TEST_ASSERT_EQUAL_UINT32(201U, pump->controlLateUs);
  TEST_ASSERT_EQUAL_UINT32(5201U, pump->controlDtUs);
  TEST_ASSERT_EQUAL_UINT32(9899U, pump->controlDueUs);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->controlOverruns);
}

void test_vp37_control_grid_starts_afresh_after_a_stop_and_a_new_start(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50.0f);
  controlStepAt(pump, 100000U);
  controlStepAt(pump, 105000U);
  TEST_ASSERT_EQUAL_UINT32(110000U, pump->controlDueUs);
  VP37_stop(pump);
  // A stopped pump takes no step, whatever the old grid says.
  controlStepAt(pump, 110000U);
  TEST_ASSERT_EQUAL_UINT32(2U, pump->controlSequence);
  // A new start steps at once, from a grid that begins there.
  restartPumpForProcessTests(pump);
  controlStepAt(pump, 300123U);
  TEST_ASSERT_EQUAL_UINT32(1U, pump->controlSequence);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->controlLateUs);
  TEST_ASSERT_EQUAL_UINT32(5000U, pump->controlDtUs);
  TEST_ASSERT_EQUAL_UINT32(305123U, pump->controlDueUs);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->controlOverruns);
}

void test_vp37_control_due_within_reports_the_deadline_margin(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50.0f);
  TEST_ASSERT_FALSE(VP37_controlDueWithin(NULL, 0U, 400U));
  // Before the first step every moment is due, also one the counter puts
  // "before" the zero the deadline still holds.
  TEST_ASSERT_TRUE(VP37_controlDueWithin(pump, 0U, 0U));
  TEST_ASSERT_TRUE(VP37_controlDueWithin(pump, 123456U, 400U));
  TEST_ASSERT_TRUE(VP37_controlDueWithin(pump, UINT32_MAX - 1000U, 400U));
  controlStepAt(pump, 100000U);
  // Due within the margin, on the deadline and after it; not before.
  TEST_ASSERT_FALSE(VP37_controlDueWithin(pump, 104599U, 400U));
  TEST_ASSERT_TRUE(VP37_controlDueWithin(pump, 104600U, 400U));
  TEST_ASSERT_FALSE(VP37_controlDueWithin(pump, 104999U, 0U));
  TEST_ASSERT_TRUE(VP37_controlDueWithin(pump, 105000U, 0U));
  TEST_ASSERT_TRUE(VP37_controlDueWithin(pump, 106000U, 0U));
  // The same across the counter wrap.
  restartPumpForProcessTests(pump);
  controlStepAt(pump, UINT32_MAX - 100U);
  TEST_ASSERT_FALSE(VP37_controlDueWithin(pump, 4498U, 400U));
  TEST_ASSERT_TRUE(VP37_controlDueWithin(pump, 4499U, 400U));
  TEST_ASSERT_TRUE(VP37_controlDueWithin(pump, 4899U, 0U));
}

void test_vp37_control_late_window_reports_the_last_completed_window(void) {
  VP37Pump *pump = vp37TestPump();
  setupPumpForProcessTests(pump);
  VP37_setPositionDemandPercentage(pump, 50.0f);
  controlStepAt(pump, 100000U);
  // Steps on the grid, three of them late, one of them a whole period late:
  // the window keeps the largest late start among the regular steps.
  for (uint32_t step = 2U; step <= VP37_CONTROL_LATE_WINDOW_STEPS; step++) {
    uint32_t lateUs = 0U;
    if (step == 10U) {
      lateUs = 150U;
    } else if (step == 40U) {
      lateUs = 320U;
    } else if (step == 60U) {
      lateUs = 6000U;
    } else if (step == 80U) {
      lateUs = 210U;
    } else {
      lateUs = 0U;
    }
    controlStepAt(pump, pump->controlDueUs + lateUs);
    TEST_ASSERT_EQUAL_UINT32(step, pump->controlSequence);
    if (step < VP37_CONTROL_LATE_WINDOW_STEPS) {
      // Nothing completed yet: the report still shows the previous window.
      TEST_ASSERT_EQUAL_UINT32(0U, pump->controlLateMaxUs);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1U, pump->controlOverruns);
  TEST_ASSERT_EQUAL_UINT32(320U, pump->controlLateMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0U, pump->controlLateWindowUs);
  // The next window starts from zero and leaves the report alone.
  controlStepAt(pump, pump->controlDueUs + 50U);
  TEST_ASSERT_EQUAL_UINT32(320U, pump->controlLateMaxUs);
  TEST_ASSERT_EQUAL_UINT32(50U, pump->controlLateWindowUs);
  controlStepAt(pump, pump->controlDueUs);
  TEST_ASSERT_EQUAL_UINT32(50U, pump->controlLateWindowUs);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_holding_bias_requires_two_seconds_and_keeps_control_terms);
  RUN_TEST(test_holding_bias_rejects_endpoint_transient_freeze_and_saturation);
  RUN_TEST(test_holding_bias_excursion_and_I_drift_restart_qualification);
  RUN_TEST(test_holding_bias_is_used_only_for_its_measured_target);
  RUN_TEST(test_holding_bias_does_not_add_negative_drive);
  RUN_TEST(test_holding_bias_is_invalidated_by_tuning_stop_and_invalid_dt);
  RUN_TEST(test_descent_return_uses_measured_interior_holding_bias);
  RUN_TEST(
      test_descent_from_calibration_max_ignores_arbitrary_previous_integral);
  RUN_TEST(test_descent_anchor_remembers_maximum_demand_across_feedback_jitter);
  RUN_TEST(test_descent_endpoint_anchor_preserves_positive_braking_bias);
  RUN_TEST(test_descent_share_selects_the_declared_acquired_bias_fraction);
  RUN_TEST(test_descent_share_requires_released_zero_and_defaults_reset);
#if VP37_TELEMETRY_ENABLED
  RUN_TEST(test_holding_bias_telemetry_reports_measured_target_and_use);
  RUN_TEST(test_descent_share_telemetry_reports_selected_fraction);
#endif
  RUN_TEST(test_descent_unwind_removes_three_quarters_before_a_fast_crossing);
#if ECU_FUNCTIONAL_TESTS_ENABLED
  RUN_TEST(
      test_descent_trace_waits_beyond_buffer_duration_and_retains_previous_step);
  RUN_TEST(
      test_descent_trace_armed_release_and_explicit_stop_finish_partial_capture);
  RUN_TEST(test_descent_trace_requires_running_full_profile_and_motion_gate);
  RUN_TEST(
      test_descent_trace_empty_completion_preserves_pending_on_invalid_output);
#endif
  RUN_TEST(test_descent_unwind_ignores_small_jitter_and_latches_partial_target);
  RUN_TEST(
      test_descent_unwind_first_arrival_blocks_reactivation_until_new_target);
  RUN_TEST(
      test_descent_unwind_qualification_requires_settled_negative_acquired_bias);
  RUN_TEST(test_descent_unwind_follows_actual_feedback_and_zero_still_releases);
  RUN_TEST(test_descent_unwind_returns_selected_bias_and_preserves_P);
  RUN_TEST(test_descent_unwind_obeys_hold_freeze_motion_and_profile_rules);
  RUN_TEST(test_descent_unwind_keeps_braking_bias_and_full_integral_authority);
  RUN_TEST(test_descent_unwind_respects_complete_command_antiwindup);
  RUN_TEST(test_descent_unwind_baseline_changes_only_with_the_demand);
  RUN_TEST(test_descent_unwind_switch_requires_released_zero_and_resets);
#if VP37_TELEMETRY_ENABLED
  RUN_TEST(test_descent_unwind_telemetry_reports_flag_and_saved_bias);
#endif
  RUN_TEST(test_ramp_braking_opposes_upward_reference_lead);
  RUN_TEST(test_ramp_braking_opposes_downward_reference_lead);
  RUN_TEST(test_ramp_braking_does_not_integrate_upward_tracking_lag);
  RUN_TEST(test_ramp_braking_does_not_integrate_downward_tracking_lag);
  RUN_TEST(test_ramp_braking_off_preserves_the_existing_upward_rule);
  RUN_TEST(test_ramp_braking_off_preserves_the_existing_downward_rule);
  RUN_TEST(test_ramp_braking_requires_the_full_profile);
  RUN_TEST(test_ramp_braking_switch_requires_released_zero_and_resets);

  RUN_TEST(
      test_upper_holding_quiet_window_forgets_motion_older_than_its_window);
  RUN_TEST(
      test_upper_holding_quiet_window_rejects_motion_since_its_last_publication);
  RUN_TEST(test_upper_holding_bias_keeps_its_slow_gain_at_the_blend_start);
  RUN_TEST(test_upper_holding_capture_delay_follows_the_existing_upper_blend);
  RUN_TEST(
      test_upper_holding_preserves_arrival_deadband_and_confirmation_before_settle);
  RUN_TEST(test_upper_holding_preserves_PD_and_ordinary_arrival_integration);
  RUN_TEST(test_native_upper_holding_keeps_the_research_motion_gate_disabled);
  RUN_TEST(test_upper_holding_preserves_lower_moving_and_research_profiles);
  RUN_TEST(
      test_upper_holding_limits_complete_command_and_releases_on_new_demand);
  RUN_TEST(
      test_upper_holding_stiffens_without_a_drive_step_and_restores_moving_gain);
  RUN_TEST(test_upper_holding_stiffness_respects_integral_and_output_headroom);
  RUN_TEST(
      test_upper_holding_captures_integral_early_without_the_legacy_offset_gate);
  RUN_TEST(
      test_upper_holding_gain_waits_for_a_quiet_interval_and_limits_each_transfer);
  RUN_TEST(test_upper_holding_bias_waits_until_the_bumpless_gain_ramp_finishes);
  RUN_TEST(test_upper_holding_bias_is_a_bounded_step_between_quiet_intervals);
  RUN_TEST(test_upper_holding_bias_corrects_offset_while_continuous_I_is_held);
  RUN_TEST(test_upper_deadband_preserves_lower_range_and_legacy_default);
  RUN_TEST(test_upper_deadband_stops_static_windup_and_preserves_P_and_D);
  RUN_TEST(
      test_upper_deadband_retains_large_error_integration_and_output_limits);
  RUN_TEST(test_upper_deadband_switch_requires_released_zero_and_resets);
  RUN_TEST(
      test_ramp_damping_keeps_measurement_gain_during_both_reference_directions);
  RUN_TEST(test_ramp_damping_has_no_reference_kick_and_opposes_measured_motion);
  RUN_TEST(test_ramp_damping_preserves_settled_gain_lower_range_and_release);
  RUN_TEST(test_ramp_damping_switch_requires_released_zero_and_resets_on_stop);
  RUN_TEST(test_reference_weight_keeps_the_counter_with_motion_error_hold);
  RUN_TEST(test_reference_weight_keeps_complete_standing_integral_authority);
  RUN_TEST(
      test_reference_weight_preserves_disturbances_through_the_upper_blend);
#if VP37_TELEMETRY_ENABLED
  RUN_TEST(test_reference_weight_telemetry_publishes_physical_coefficients);
#endif
  RUN_TEST(
      test_reference_weight_preserves_upper_disturbance_feedback_and_position_hold);
  RUN_TEST(test_reference_weight_preserves_the_lower_controller);
  RUN_TEST(
      test_reference_weight_reduces_kick_without_changing_disturbance_gain);
  RUN_TEST(
      test_reference_weight_potential_is_continuous_and_has_no_roundtrip_drift);
  RUN_TEST(
      test_reference_weight_includes_compensation_in_both_antiwindup_limits);
  RUN_TEST(test_reference_weight_switch_is_safe_and_excludes_full_I_freeze);
#if VP37_TELEMETRY_ENABLED
  RUN_TEST(test_slow_damping_telemetry_publishes_the_flag_and_complete_extra);
  RUN_TEST(test_ramp_damping_telemetry_reports_the_selected_mode);
  RUN_TEST(test_ramp_braking_telemetry_reports_the_selected_mode);
  RUN_TEST(test_upper_deadband_telemetry_reports_the_active_band_and_input);
  RUN_TEST(test_live_control_format_is_available_during_RAM_recording);
#endif
  RUN_TEST(test_slow_damping_preserves_dc_and_opposes_velocity);
  RUN_TEST(test_slow_damping_rejects_fast_feedback_without_removing_native_D);
  RUN_TEST(test_slow_damping_resets_on_disable_invalid_dt_and_reentry);
  RUN_TEST(
      test_slow_damping_keeps_complete_command_inside_both_antiwindup_limits);
  RUN_TEST(test_slow_damping_switch_requires_released_zero_and_default_is_off);
  RUN_TEST(test_vp37_current_scan_increases_drive_for_a_current_deficit);
  RUN_TEST(test_vp37_acquires_scan_between_position_steps);
  RUN_TEST(test_vp37_current_scan_reduces_drive_for_a_current_excess);
  RUN_TEST(test_vp37_current_scan_matches_target_before_active_low_latch);
  RUN_TEST(
      test_vp37_resistance_scan_uses_the_command_that_produced_the_current);
  RUN_TEST(test_vp37_current_scan_cache_rechecks_capture_age_and_quality);
  RUN_TEST(test_vp37_current_scan_cache_rechecks_historical_command_age);
  RUN_TEST(test_vp37_current_scan_cache_follows_sequence_command_and_reset);
  RUN_TEST(
      test_vp37_resistance_scan_keeps_onset_capture_rejected_after_settling);
  RUN_TEST(test_vp37_current_feedback_preserves_both_physical_limits);
  RUN_TEST(test_vp37_current_feedback_clears_at_rest_and_stop);
  RUN_TEST(test_vp37_derivative_opposes_rebound_within_one_control_step);
  RUN_TEST(test_vp37_default_derivative_ignores_large_feedback_step);
  RUN_TEST(test_vp37_upper_derivative_blends_only_above_85_percent);
  RUN_TEST(test_vp37_upper_derivative_waits_for_stationary_target_and_slew);
  RUN_TEST(test_vp37_upper_derivative_blend_uses_elapsed_control_time);
  RUN_TEST(test_vp37_upper_derivative_requires_an_explicit_gain);
  RUN_TEST(
      test_vp37_upper_derivative_survives_62ms_target_updates_without_steps);
  RUN_TEST(test_vp37_upper_derivative_opposes_both_directions_of_motion);
  RUN_TEST(test_vp37_upper_derivative_changes_preserve_integral_and_history);
  RUN_TEST(test_vp37_upper_derivative_keeps_zero_release_and_fault_shutdown);
  RUN_TEST(test_vp37_stationary_target_uses_soft_approach);
  RUN_TEST(test_vp37_slew_tracks_a_250_percent_per_second_input);
  RUN_TEST(test_vp37_ramp_unwinds_existing_negative_trim);
  RUN_TEST(test_upper_reference_increase_keeps_positive_incremental_drive);
  RUN_TEST(test_vp37_motion_feedforward_brakes_when_upper_ramp_stops);
  RUN_TEST(test_vp37_motion_taper_preserves_lower_drive_holding_and_descent);
  RUN_TEST(
      test_vp37_single_demand_step_is_a_standing_target_from_its_first_cycle);
  RUN_TEST(test_vp37_first_demand_after_init_ramps_from_the_bottom);
  RUN_TEST(test_vp37_zero_first_demand_after_init_rests_at_once);
  RUN_TEST(test_vp37_tracked_demand_keeps_the_moving_rate_and_assist);
  RUN_TEST(test_vp37_upward_assist_saturates_above_the_rate_cap);
  RUN_TEST(test_vp37_standing_ramp_brakes_to_rest_at_its_target);
  RUN_TEST(test_vp37_tracked_ramp_brakes_into_the_top_of_the_range);
  RUN_TEST(test_vp37_top_brake_off_keeps_the_moving_rate_to_the_top);
  RUN_TEST(test_vp37_falling_standing_ramp_is_not_braked);
  RUN_TEST(
      test_vp37_lone_falling_step_keeps_the_moving_assist_for_the_settle_time);
  RUN_TEST(test_vp37_upper_stroke_rules_apply_under_any_physical_limit);
  RUN_TEST(test_vp37_upper_slew_applies_under_any_physical_limit);
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
  RUN_TEST(test_vp37_integral_hold_offset_release_removes_standing_offset);
  RUN_TEST(
      test_vp37_integral_hold_keeps_standing_offset_without_offset_release);
  RUN_TEST(test_vp37_integral_hold_offset_release_needs_a_persistent_mean);
  RUN_TEST(
      test_vp37_integral_hold_offset_release_stays_off_on_the_upper_stroke);
  RUN_TEST(test_vp37_integral_hold_waits_for_the_mean_after_an_arrival);
  RUN_TEST(test_vp37_integral_hold_mean_entry_band_waits_for_a_closer_mean);
  RUN_TEST(
      test_vp37_integral_hold_mean_entry_band_stays_off_on_the_upper_stroke);
  RUN_TEST(test_vp37_thermal_hold_keeps_the_multiplier_from_falling);
  RUN_TEST(test_vp37_thermal_hold_follows_a_rise_above_the_lowest_value);
  RUN_TEST(test_vp37_thermal_hold_current_target_keeps_the_held_excess);
  RUN_TEST(test_vp37_thermal_hold_current_target_is_plain_without_the_hold);
  RUN_TEST(test_vp37_thermal_hold_resumes_when_the_target_moves);
  RUN_TEST(test_vp37_thermal_hold_stays_off_on_the_upper_stroke);
  RUN_TEST(test_vp37_thermal_hold_waits_for_the_position_to_arrive);
  RUN_TEST(
      test_vp37_integral_hold_releases_positive_drift_with_exit_band_noise);
  RUN_TEST(
      test_vp37_integral_hold_releases_negative_drift_with_exit_band_noise);
  RUN_TEST(test_vp37_supply_change_does_not_freeze_integral);
  RUN_TEST(test_vp37_supply_change_alone_leaves_the_integral_hold_engaged);
  RUN_TEST(test_vp37_integral_limit_tapers_with_ramped_position);
  RUN_TEST(test_vp37_feedforward_is_nonlinear_and_calibration_independent);
  RUN_TEST(test_vp37_climb_floor_does_not_bypass_target_ramp);
  RUN_TEST(test_vp37_climb_floor_preserves_learned_negative_trim_at_target);
  RUN_TEST(test_vp37_climb_floor_keeps_acceleration_reserve_for_moving_demand);
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
  RUN_TEST(test_vp37_percent_demand_ends_at_the_physical_limit);
  RUN_TEST(test_vp37_counts_demand_is_capped_at_the_physical_limit);
  RUN_TEST(test_vp37_full_physical_limit_keeps_the_calibrated_top);
  RUN_TEST(test_vp37_physical_limit_is_a_share_of_the_stroke);
  RUN_TEST(test_vp37_pid_time_update_setter);
  RUN_TEST(test_vp37_init_returns_already_initialized);
  RUN_TEST(test_vp37_init_returns_ok_when_baseline_ready);
  RUN_TEST(test_vp37_init_rejects_insufficient_calibration_travel);
  RUN_TEST(test_vp37_positive_limit_matches_hot_actuator_compensation);
  RUN_TEST(test_vp37_positive_limit_falls_back_for_bad_temperature);
  RUN_TEST(test_vp37_hot_positive_error_uses_expanded_range);
  RUN_TEST(test_vp37_hot_negative_error_keeps_original_range);
  RUN_TEST(test_vp37_pwm_limit_matches_physical_resolution);
  RUN_TEST(test_vp37_process_disables_after_adj_comm_cutoff_timeout);
  RUN_TEST(test_vp37_init_without_board_services_leaves_the_pump_stopped);
  RUN_TEST(test_vp37_set_callbacks_checks_required_entries_and_fills_defaults);
  RUN_TEST(test_vp37_board_services_stay_fixed_after_init_and_stop);
  RUN_TEST(test_vp37_status_is_published_by_the_step_and_a_stop_once);
  RUN_TEST(test_vp37_tuning_accepts_each_range_and_refuses_the_rest);
  RUN_TEST(test_vp37_tuning_side_effects_follow_the_setting);
  RUN_TEST(test_vp37_tuning_reset_restores_defaults_and_keeps_switches);
  RUN_TEST(test_vp37_is_ready_needs_a_running_calibrated_pump);
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
  RUN_TEST(test_vp37_control_grid_keeps_the_deadline_after_a_late_start);
  RUN_TEST(test_vp37_control_grid_restarts_after_a_whole_period_late);
  RUN_TEST(test_vp37_control_grid_crosses_the_microsecond_wrap);
  RUN_TEST(test_vp37_control_grid_starts_afresh_after_a_stop_and_a_new_start);
  RUN_TEST(test_vp37_control_due_within_reports_the_deadline_margin);
  RUN_TEST(test_vp37_control_late_window_reports_the_last_completed_window);
  RUN_TEST(test_vp37_pwm_floor_is_visible_to_integrator);
  RUN_TEST(test_vp37_zero_demand_releases_drive_despite_feedback_offset);
  RUN_TEST(test_vp37_zero_demand_finishes_slew_and_resumes_without_stored_pid);
  RUN_TEST(test_vp37_positive_demand_rounded_to_min_still_regulates);
  RUN_TEST(test_vp37_feedback_fault_at_rest_still_latches_drive_off);
  RUN_TEST(test_vp37_target_ramp_uses_elapsed_time_and_preserves_overshoot);
#if ECU_FUNCTIONAL_TESTS_ENABLED
  RUN_TEST(test_vp37_trace_preserves_consecutive_steps_until_drained);
  RUN_TEST(test_vp37_trace_stopped_before_a_sample_frees_the_buffer);
#endif
  RUN_TEST(test_vp37_integral_authority_is_independent_of_ki);
  RUN_TEST(test_vp37_bench_cap_and_stop);
  RUN_TEST(test_vp37_measured_drive_replaces_the_fuel_temperature_multiplier);
  RUN_TEST(test_vp37_measured_drive_learns_while_position_moves_once_ready);
  RUN_TEST(test_vp37_measured_drive_does_not_learn_in_motion_before_ready);
  RUN_TEST(test_vp37_measured_drive_rejects_stale_and_mismatched_captures);
  RUN_TEST(test_vp37_measured_drive_consumes_each_capture_once);
  RUN_TEST(test_vp37_measured_drive_holds_through_supply_transients);
  RUN_TEST(test_vp37_measured_drive_detects_accumulated_small_voltage_changes);
  RUN_TEST(
      test_vp37_measured_drive_retains_estimate_through_a_long_supply_sweep);
  RUN_TEST(test_vp37_measured_drive_expires_when_fresh_captures_stop);
  RUN_TEST(test_vp37_measured_drive_does_not_apply_silence_as_heating_time);
  RUN_TEST(test_vp37_stroke_taper_holds_ends_flat_and_walks_the_knots);
  RUN_TEST(test_vp37_integral_deadband_widens_only_in_the_upper_stroke);
  RUN_TEST(test_vp37_dead_zone_bends_at_the_map_knot);
  RUN_TEST(
      test_vp37_integral_hold_bands_stay_fixed_under_the_scheduled_dead_zone);
  RUN_TEST(test_vp37_map_trim_absorbs_the_settled_integral_without_a_bump);
  RUN_TEST(test_vp37_fresh_supply_scales_the_command_with_bounded_prediction);
  RUN_TEST(test_vp37_init_status_names_every_result);
  RUN_TEST(test_september_reference_keeps_motion_at_95_and_small_deadband);
  RUN_TEST(
      test_september_reference_ramp_has_no_arrival_brake_and_uses_adj_supply);
  RUN_TEST(test_full_range_prototype_has_bidirectional_integral_authority);
  RUN_TEST(test_full_range_uses_base_hold_offset_release_and_current_policy);
  RUN_TEST(test_full_range_integral_recovers_above_old_cap_and_respects_output);
  RUN_TEST(test_research_integral_hold_keeps_bias_and_zero_still_releases);
  RUN_TEST(test_normal_integral_hold_keeps_bias_and_zero_still_releases);
  RUN_TEST(
      test_research_drive_hold_observes_without_changing_estimate_or_scale);
  RUN_TEST(test_normal_drive_hold_observes_without_changing_estimate_or_scale);
  RUN_TEST(test_motion_integral_retains_bias_and_D_during_upward_approach);
  RUN_TEST(test_motion_integral_retains_bias_and_D_during_downward_approach);
  RUN_TEST(test_motion_integral_switch_is_profile_gated_and_resettable);
#if VP37_TELEMETRY_ENABLED
  RUN_TEST(test_calibration_probe_preserves_calibration_pid_and_zeroes_output);
  RUN_TEST(test_calibration_probe_refuses_active_or_unreleased_state);
  RUN_TEST(test_calibration_probe_invalid_feedback_aborts_and_retains_result);
  RUN_TEST(test_calibration_probe_bus_failure_and_clock_wrap_release_output);
  RUN_TEST(test_calibration_probe_unsettled_feedback_times_out_at_one_second);
#endif
  return UNITY_END();
}

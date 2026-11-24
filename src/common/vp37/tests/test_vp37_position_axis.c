/* The position axis of the VP37 module: the signed position it reads from the
 * Adjustometer, the calibration on that axis, control around limits below
 * zero, and the limits following the fuel temperature. The fake board models
 * the actuator: during calibration the position follows the command, later
 * the test places it. Built twice, with the fuel-temperature correction on
 * and off. */
#include "vp37_pump_setup.h"
#include <hal/usb/hal_usb.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* After the HAL headers: Unity's noreturn macro would rename their
 * attribute. */
#include "unity.h"

/* After the HAL configuration: the CAN part of the bridge needs it. */
#include "../../tests/hal_mock_c_bridge.h"

/** Baseline of the fake Adjustometer [Hz]. */
#define AXIS_BASELINE_HZ 412345U
/** Published magnitude below which the Adjustometer holds zero [Hz]. */
#define AXIS_ZERO_HOLD_HZ 50

static VP37Pump pump;

static struct {
  int32_t lastPwm;
  bool driveEnabled;
  bool followCommand; /* Calibration: rest at PWM 0, top otherwise. */
  int32_t rest;       /* Positions the calibration finds [Hz]. */
  int32_t top;
  int16_t restFuelTempC; /* Fuel temperature while at rest and at the top. */
  int16_t topFuelTempC;
  int32_t position; /* Position once the test places the actuator [Hz]. */
  int16_t fuelTempC;
  uint8_t status;
  bool signalDropped; /* A lost signal also zeroes the published frequency. */
  uint32_t number;
  uint32_t transfers;
  bool extension;
  bool tornExtension;
  int16_t chipTempDeciC;
} board;

static void writeQuantity(int32_t value) { board.lastPwm = value; }
static void writeTiming(int32_t value) { (void)value; }
static void enableDrive(bool enabled) { board.driveEnabled = enabled; }
static bool driveEnabled(void) { return board.driveEnabled; }

/* One fast frame as the Adjustometer publishes it: the filtered frequency
 * falls as the actuator lifts and pulseHz is its zero-held magnitude. */
static hal_status_t transfer(uint8_t reg, uint8_t *out, size_t length) {
  board.transfers++;
  if (((size_t)reg + length) > ADJUSTOMETER_TOTAL_REG_COUNT) {
    return HAL_EBUS;
  }
  const bool atTop = board.followCommand && (board.lastPwm > 0);
  const int32_t position =
      board.followCommand ? (atTop ? board.top : board.rest) : board.position;
  const int32_t magnitude = (position < 0) ? -position : position;
  adjustometer_feedback_t sample = {0};
  sample.baselineHz = AXIS_BASELINE_HZ;
  sample.filteredHz = board.signalDropped
                          ? 0U
                          : (uint32_t)((int32_t)AXIS_BASELINE_HZ - position);
  sample.rawHz = sample.filteredHz;
  sample.pulseHz = (int16_t)((magnitude <= AXIS_ZERO_HOLD_HZ) ? 0 : magnitude);
  sample.voltage = 140U;
  sample.fuelTemp = board.followCommand
                        ? (atTop ? board.topFuelTempC : board.restFuelTempC)
                        : board.fuelTempC;
  sample.status = board.status;
  sample.number = ++board.number;
  sample.measuredUs = hal_micros();
  uint8_t registers[ADJUSTOMETER_TOTAL_REG_COUNT] = {0};
  adjustometer_feedback_encode(registers + ADJUSTOMETER_FEEDBACK_START, &sample,
                               2U);
  if (board.extension) {
    registers[ADJUSTOMETER_REG_EXT_VERSION] = ADJUSTOMETER_EXT_VERSION;
    registers[ADJUSTOMETER_REG_EXT_SEQ_BEGIN] = 6U;
    registers[ADJUSTOMETER_REG_EXT_SEQ_END] = board.tornExtension ? 8U : 6U;
    registers[ADJUSTOMETER_REG_EXT_FLAGS] =
        ADJUSTOMETER_EXT_FLAG_CHIP_TEMP_VALID;
    /* Deliberately older position data in the slow publication. */
    jh_store_be32(registers + ADJUSTOMETER_REG_SIGNAL_HZ,
                  AXIS_BASELINE_HZ - 900U);
    jh_store_be32(registers + ADJUSTOMETER_REG_BASELINE_HZ, AXIS_BASELINE_HZ);
    jh_store_be16(registers + ADJUSTOMETER_REG_CHIP_TEMP_DECI_C,
                  (uint16_t)board.chipTempDeciC);
  }
  memcpy(out, registers + reg, length);
  return HAL_OK;
}

static const VP37Callbacks callbacks = {
    .writeQuantityPwm = writeQuantity,
    .writeTimingPwm = writeTiming,
    .setDriveEnabled = enableDrive,
    .driveEnabled = driveEnabled,
    .adjustometerTransfer = transfer,
};

void setUp(void) {
  fiesta_test_set_millis(0U);
  memset(&board, 0, sizeof(board));
  memset(&pump, 0, sizeof(pump));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setCallbacks(&pump, &callbacks));
  const float ratio = ((float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_HIGH_OHMS +
                       (float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_LOW_OHMS) /
                      (float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_LOW_OHMS;
  fiesta_test_adc_inject(JH_HW_NODE_VP37_PIN_SUPPLY_ADC,
                         (int)(14.0f / ratio * 4095.0f / 3.3f + 0.5f));
}

void tearDown(void) {
#if VP37_TELEMETRY_ENABLED
  VP37CalibrationSample sample;
  VP37CalibrationResult result;
  while (VP37_readCalibrationTrace(&sample) == HAL_OK) {
  }
  (void)VP37_takeCalibrationResult(&result);
#endif
  if (pump.pid.controller != NULL) {
    hal_pid_controller_destroy(pump.pid.controller);
    pump.pid.controller = NULL;
  }
}

/* Start the pump against an actuator that rests at @p rest and reaches @p top
 * under the calibration command; afterwards it stands at the rest. */
static void startPump(int32_t rest, int32_t top, int16_t restFuelTempC,
                      int16_t topFuelTempC, uint8_t status) {
  board.followCommand = true;
  board.rest = rest;
  board.top = top;
  board.restFuelTempC = restFuelTempC;
  board.topFuelTempC = topFuelTempC;
  board.status = status;
  TEST_ASSERT_EQUAL_INT(VP37_INIT_OK, VP37_init(&pump));
  board.followCommand = false;
  board.position = rest;
  board.fuelTempC = topFuelTempC;
}

static void step(void) {
  fiesta_test_set_millis(hal_millis() + 5U);
  VP37_process(&pump);
}

/* Control steps with the actuator on the ramp, one step behind it. */
static void track(uint32_t steps) {
  for (uint32_t i = 0U; i < steps; i++) {
    board.position = pump.demand.desired;
    step();
  }
}

static int32_t readPosition(void) {
  TEST_ASSERT_TRUE(VP37_updateAdjustometerPosition(&pump));
  return pump.feedback.position;
}

static void
test_feedback_caches_diagnostics_without_using_their_position(void) {
  board.extension = true;
  board.chipTempDeciC = -125;
  board.position = 4500;
  const uint32_t transfers = board.transfers;
  TEST_ASSERT_EQUAL_INT32(4500, readPosition());
  TEST_ASSERT_EQUAL_UINT32(transfers + 1U, board.transfers);
  TEST_ASSERT_TRUE(pump.adjustometer.last.extendedTelemetryValid);
  TEST_ASSERT_EQUAL_INT16(-125, pump.adjustometer.last.chipTempDeciC);
  TEST_ASSERT_EQUAL_INT32(-4500, pump.adjustometer.last.signedDeltaHz);
  TEST_ASSERT_EQUAL_UINT32(AXIS_BASELINE_HZ - 4500U,
                           pump.adjustometer.last.signalHz);
  board.tornExtension = true;
  board.position = 4600;
  TEST_ASSERT_EQUAL_INT32(4600, readPosition());
  TEST_ASSERT_EQUAL_UINT32(transfers + 2U, board.transfers);
  TEST_ASSERT_FALSE(pump.adjustometer.last.extendedTelemetryValid);
  TEST_ASSERT_TRUE(pump.adjustometer.last.feedbackFresh);
}

/* Position a percent demand stands for on the stroke in effect. */
static int32_t percentTarget(float percent) {
  return (int32_t)hal_math_map_f32(
      percent, 0.0f, 100.0f, (float)VP37_getPositionDemandMinValue(&pump),
      (float)VP37_getPositionDemandMaxValue(&pump));
}

// ── Signed position ─────────────────────────────────────────────────────────

static void test_position_above_the_baseline_reads_negative(void) {
  // A rest 130 Hz above a stale baseline: the Adjustometer publishes the
  // magnitude, the position keeps the sign.
  board.position = -130;
  TEST_ASSERT_EQUAL_INT32(-130, readPosition());
  TEST_ASSERT_TRUE(pump.feedback.positionValid);
  TEST_ASSERT_EQUAL_INT16(130, pump.adjustometer.last.pulseHz);
  board.position = 4500;
  TEST_ASSERT_EQUAL_INT32(4500, readPosition());
}

static void test_position_near_the_baseline_keeps_its_signed_value(void) {
  // The published magnitude is held at zero here; the position is not.
  const int32_t positions[] = {-50, -45, -1, 0, 1, 30, 45, 50};
  for (size_t i = 0U; i < COUNTOF(positions); i++) {
    board.position = positions[i];
    TEST_ASSERT_EQUAL_INT32(positions[i], readPosition());
    TEST_ASSERT_EQUAL_INT16(0, pump.adjustometer.last.pulseHz);
  }
}

static void test_signal_loss_and_pending_baseline_keep_the_last_position(void) {
  board.position = 2000;
  board.signalDropped = true;
  board.status = ADJ_STATUS_SIGNAL_LOST;
  (void)readPosition();
  TEST_ASSERT_FALSE(pump.feedback.positionValid);
  board.signalDropped = false;
  board.status = ADJ_STATUS_OK;
  TEST_ASSERT_EQUAL_INT32(2000, readPosition());
  TEST_ASSERT_TRUE(pump.feedback.positionValid);
  board.position = 2600;
  board.signalDropped = true;
  board.status = ADJ_STATUS_SIGNAL_LOST;
  TEST_ASSERT_EQUAL_INT32(2000, readPosition());
  board.signalDropped = false;
  board.status = ADJ_STATUS_BASELINE_PENDING;
  TEST_ASSERT_EQUAL_INT32(2000, readPosition());
  TEST_ASSERT_TRUE(pump.feedback.positionValid);
}

static void test_signal_loss_still_stops_the_running_pump(void) {
  startPump(-130, 8000, 25, 25, ADJ_STATUS_OK);
  (void)VP37_setPositionDemandPercentage(&pump, 40.0f);
  track(20U);
  TEST_ASSERT_GREATER_THAN_INT32(0, board.lastPwm);
  const int32_t position = pump.feedback.position;
  board.signalDropped = true;
  board.status = ADJ_STATUS_SIGNAL_LOST;
  step();
  TEST_ASSERT_FALSE(pump.vp37Initialized);
  TEST_ASSERT_EQUAL_INT32(0, board.lastPwm);
  TEST_ASSERT_EQUAL_INT32(position, pump.feedback.position);
}

static void test_full_diagnostic_queue_cannot_delay_the_signal_loss_stop(void) {
  startPump(-130, 8000, 25, 25, ADJ_STATUS_OK);
  (void)VP37_setPositionDemandPercentage(&pump, 40.0f);
  track(20U);
  TEST_ASSERT_GREATER_THAN_INT32(0, board.lastPwm);
  VP37_printEvents(&pump);
  for (unsigned i = 0U; i < COUNTOF(pump.diagnostics.records); ++i) {
    VP37_enableVP37(&pump, true);
  }
  deb("diagnostic-stop sentinel");
  board.signalDropped = true;
  board.status = ADJ_STATUS_SIGNAL_LOST;
  step();
  TEST_ASSERT_FALSE(pump.vp37Initialized);
  TEST_ASSERT_FALSE(board.driveEnabled);
  TEST_ASSERT_EQUAL_INT32(0, board.lastPwm);
  TEST_ASSERT_EQUAL_STRING("diagnostic-stop sentinel",
                           fiesta_test_debug_last_line());
  TEST_ASSERT_EQUAL_UINT32(2U,
                           hal_debug_events_dropped(&pump.diagnostics.queue));
  VP37_printEvents(&pump);
  TEST_ASSERT_NOT_NULL(strstr(fiesta_test_serial_last_line(),
                              "VP37 diagnostic log overflow: 2"));
}

// ── Calibration on the signed axis ──────────────────────────────────────────

static void test_calibration_keeps_a_rest_above_the_baseline_below_zero(void) {
  startPump(-130, 8000, 25, 25, ADJ_STATUS_OK);
  TEST_ASSERT_TRUE(pump.feedback.calibrationDone);
  TEST_ASSERT_EQUAL_INT32(-130, pump.feedback.calibratedMin);
  TEST_ASSERT_EQUAL_INT32(8000, pump.feedback.calibratedMax);
  TEST_ASSERT_EQUAL_INT32(-130, pump.feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT32(8000, pump.feedback.adjustMax);
  TEST_ASSERT_EQUAL_INT32(-130, VP37_getPositionDemandMinValue(&pump));
  // The axis spans the whole travel, not the travel less twice the rest.
  TEST_ASSERT_EQUAL_INT32(8130,
                          pump.feedback.adjustMax - pump.feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT32(-130, pump.demand.desired);
  TEST_ASSERT_TRUE(pump.demand.desiredValid);
  TEST_ASSERT_FALSE(pump.demand.targetValid);
#if VP37_TELEMETRY_ENABLED
  VP37CalibrationSample sample;
  uint32_t samples = 0U;
  while (VP37_readCalibrationTrace(&sample) == HAL_OK) {
    samples++;
  }
  VP37CalibrationResult result;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_takeCalibrationResult(&result));
  TEST_ASSERT_EQUAL_UINT32(samples, result.samples);
  TEST_ASSERT_EQUAL_INT32(-130, result.minimum);
  TEST_ASSERT_EQUAL_INT32(8000, result.maximum);
  TEST_ASSERT_TRUE(result.valid);
  TEST_ASSERT_TRUE(result.fuelTempValid);
  TEST_ASSERT_EQUAL_INT16(250, result.fuelTempDeciC);
#endif
}

static void
test_calibration_takes_the_mean_fuel_temperature_of_both_ends(void) {
  startPump(100, 8200, 25, 26, ADJ_STATUS_OK);
  TEST_ASSERT_TRUE(pump.feedback.calibrationFtValid);
  TEST_ASSERT_EQUAL_INT16(255, pump.feedback.calibrationFtDeciC);
  TEST_ASSERT_EQUAL_INT16(0, pump.feedback.limitCorrectionMin);
  TEST_ASSERT_EQUAL_INT16(0, pump.feedback.limitCorrectionMax);
}

static void test_calibration_with_a_broken_fuel_sensor_has_no_reference(void) {
  startPump(100, 8200, 25, 25, ADJ_STATUS_FUEL_TEMP_BROKEN);
  TEST_ASSERT_TRUE(pump.feedback.calibrationDone);
  TEST_ASSERT_FALSE(pump.feedback.calibrationFtValid);
  // Without a reference the limits stay where the calibration put them.
  board.status = ADJ_STATUS_OK;
  board.fuelTempC = 35;
  (void)VP37_setPositionDemandPercentage(&pump, 0.0f);
  for (uint32_t i = 0U; i < 50U; i++) {
    step();
  }
  TEST_ASSERT_EQUAL_INT32(100, pump.feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT32(8200, pump.feedback.adjustMax);
}

static void test_calibration_accepts_a_stroke_centred_below_zero(void) {
  // Only the travel qualifies the range; its middle may lie anywhere.
  startPump(-6000, 1000, 25, 25, ADJ_STATUS_OK);
  TEST_ASSERT_TRUE(pump.feedback.calibrationDone);
  TEST_ASSERT_EQUAL_INT32(-2500, pump.feedback.adjustMiddle);
  TEST_ASSERT_TRUE(VP37_isReady(&pump));
}

// ── Control around limits below zero ────────────────────────────────────────

static void test_bottom_below_zero_rests_at_zero_demand(void) {
  startPump(-1500, 7000, 25, 25, ADJ_STATUS_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(&pump, 0.0f));
  TEST_ASSERT_TRUE(pump.demand.targetValid);
  TEST_ASSERT_EQUAL_INT32(-1500, pump.demand.target);
  track(3U);
  TEST_ASSERT_TRUE(pump.demand.atRest);
  TEST_ASSERT_EQUAL_INT32(0, board.lastPwm);
  TEST_ASSERT_EQUAL_INT32(-1500, pump.demand.desired);
}

static void test_negative_target_ramps_holds_and_returns_to_rest(void) {
  startPump(-1500, 7000, 25, 25, ADJ_STATUS_OK);
  // Ten percent of the usable stroke still lies below zero.
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(&pump, 10.0f));
  const int32_t target = pump.demand.target;
  TEST_ASSERT_EQUAL_INT32(percentTarget(10.0f), target);
  TEST_ASSERT_LESS_THAN_INT32(0, target);
  // A standing ramp: the first step moves part of the way from the bottom.
  track(1U);
  TEST_ASSERT_FALSE(pump.demand.atRest);
  TEST_ASSERT_GREATER_THAN_INT32(-1500, pump.demand.desired);
  TEST_ASSERT_LESS_THAN_INT32(target, pump.demand.desired);
  TEST_ASSERT_GREATER_THAN_INT32(0, board.lastPwm);
  int32_t previous = pump.demand.desired;
  for (uint32_t i = 0U; (i < 200U) && (pump.demand.desired != target); i++) {
    track(1U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(previous, pump.demand.desired);
    previous = pump.demand.desired;
  }
  TEST_ASSERT_EQUAL_INT32(target, pump.demand.desired);
  // Held under drive: the current loop qualifies the quiet hold.
  track(100U);
  TEST_ASSERT_EQUAL_INT32(target, pump.demand.desired);
  TEST_ASSERT_GREATER_OR_EQUAL_INT32(VP37_PWM_MIN, board.lastPwm);
  TEST_ASSERT_TRUE(pump.currentControl.driveSettled);
  // Back to zero: a lone falling step, then the release at the bottom.
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(&pump, 0.0f));
  TEST_ASSERT_TRUE(pump.demand.targetMoving);
  track(100U);
  TEST_ASSERT_TRUE(pump.demand.atRest);
  TEST_ASSERT_EQUAL_INT32(-1500, pump.demand.desired);
  TEST_ASSERT_EQUAL_INT32(0, board.lastPwm);
}

static void test_uncalibrated_pump_has_no_position_limits(void) {
  TEST_ASSERT_EQUAL_INT32(VP37_POSITION_NONE,
                          VP37_getPositionDemandMinValue(&pump));
  TEST_ASSERT_EQUAL_INT32(VP37_POSITION_NONE,
                          VP37_getPositionDemandMaxValue(&pump));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, VP37_setPositionDemandValue(&pump, -1));
  TEST_ASSERT_FALSE(pump.demand.targetValid);
}

// ── Limits following the fuel temperature ───────────────────────────────────

/* Calibrated at 25 C with the bottom below zero, resting at zero demand. */
static void startResting(void) {
  startPump(-130, 8000, 25, 25, ADJ_STATUS_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(&pump, 0.0f));
  track(2U);
  TEST_ASSERT_TRUE(pump.demand.atRest);
}

/* The longest control period the pump accepts [ms]. The temperature filter
 * advances by the measured step period, so minutes of it take few steps. */
#define AXIS_SLOW_STEP_MS 100U
/* One time constant of the fuel-temperature filter [ms]. */
#define AXIS_TAU_MS (VP37_LIMIT_FT_FILTER_TAU_S * 1000U)

/* Rest or hold for @p ms of control steps at AXIS_SLOW_STEP_MS, the actuator
 * on the ramp. */
static void trackSlowly(uint32_t ms) {
  pump.pidTimeUpdate = (float)AXIS_SLOW_STEP_MS;
  for (uint32_t elapsed = 0U; elapsed < ms; elapsed += AXIS_SLOW_STEP_MS) {
    board.position = pump.demand.desired;
    fiesta_test_set_millis(hal_millis() + AXIS_SLOW_STEP_MS);
    VP37_process(&pump);
  }
}

#if VP37_LIMIT_FT_CORRECTION_ENABLED
/* Correction for a fuel-temperature difference [Hz]. */
static int32_t correctionFor(int32_t slopeDeciHz, float deltaC) {
  return (int32_t)lroundf((float)slopeDeciHz * 0.1f * deltaC);
}

/* Correction a first-order filter reaches @p seconds into a step of
 * @p deltaC from the reference [Hz]. */
static int32_t correctionAfter(int32_t slopeDeciHz, float deltaC,
                               float seconds) {
  return correctionFor(
      slopeDeciHz,
      deltaC * (1.0f - expf(-seconds / (float)VP37_LIMIT_FT_FILTER_TAU_S)));
}

static void test_limit_correction_ignores_a_toggling_degree(void) {
  // Calibrated at 28 C at rest and 29 C at the top: the reference is 28.5 C,
  // and the sensor then toggles 28/29 C every 300 ms for two time constants.
  startPump(-130, 8000, 28, 29, ADJ_STATUS_OK);
  TEST_ASSERT_EQUAL_INT16(285, pump.feedback.calibrationFtDeciC);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(&pump, 0.0f));
  const uint32_t halfPeriodMs = 3U * AXIS_SLOW_STEP_MS;
  // A filter of time constant tau keeps a +-A square wave of half period h
  // within A * h / tau of its mean.
  const float rippleC =
      0.5f * ((float)halfPeriodMs * 0.001f) / (float)VP37_LIMIT_FT_FILTER_TAU_S;
  const int32_t bound = (int32_t)ceilf(
      (float)VP37_LIMIT_FT_REST_SLOPE_DHZ_PER_C * 0.1f * rippleC);
  int32_t largest = 0;
  for (uint32_t half = 0U;
       half < (2U * VP37_LIMIT_FT_FILTER_TAU_S * 1000U) / halfPeriodMs;
       half++) {
    board.fuelTempC = ((half % 2U) == 0U) ? 28 : 29;
    trackSlowly(halfPeriodMs);
    const int32_t minCorrection = abs(pump.feedback.limitCorrectionMin);
    const int32_t maxCorrection = abs(pump.feedback.limitCorrectionMax);
    largest = (minCorrection > largest) ? minCorrection : largest;
    largest = (maxCorrection > largest) ? maxCorrection : largest;
  }
  TEST_ASSERT_LESS_OR_EQUAL_INT32(bound, largest);
  TEST_ASSERT_TRUE(pump.demand.atRest);
}

static void test_limit_correction_follows_a_lasting_step_with_its_lag(void) {
  startResting();
  // A 6 C step: no expected correction falls on a half hertz, where the
  // result depends on how lroundf breaks the tie. Valgrind on arm64 rounds a
  // tie to even until the fix of KDE bug 489338 (July 2024), so under
  // Valgrind 3.19 the 80.5 Hz of a 5 C step read 80 instead of 81.
  board.fuelTempC = 31;
  const float tau = (float)VP37_LIMIT_FT_FILTER_TAU_S;
  const int32_t finalMin = correctionFor(VP37_LIMIT_FT_REST_SLOPE_DHZ_PER_C, 6);
  const int32_t finalMax = correctionFor(VP37_LIMIT_FT_TOP_SLOPE_DHZ_PER_C, 6);
  TEST_ASSERT_EQUAL_INT32(97, finalMin);
  TEST_ASSERT_EQUAL_INT32(53, finalMax);
  // One time constant: about 63 % of the way, not more.
  trackSlowly(AXIS_TAU_MS);
  TEST_ASSERT_INT32_WITHIN(
      2, correctionAfter(VP37_LIMIT_FT_REST_SLOPE_DHZ_PER_C, 6.0f, tau),
      pump.feedback.limitCorrectionMin);
  TEST_ASSERT_INT32_WITHIN(
      2, correctionAfter(VP37_LIMIT_FT_TOP_SLOPE_DHZ_PER_C, 6.0f, tau),
      pump.feedback.limitCorrectionMax);
  TEST_ASSERT_LESS_OR_EQUAL_INT32((finalMin * 64) / 100,
                                  pump.feedback.limitCorrectionMin);
  // Two time constants: further along the same curve.
  trackSlowly(AXIS_TAU_MS);
  TEST_ASSERT_INT32_WITHIN(
      2, correctionAfter(VP37_LIMIT_FT_REST_SLOPE_DHZ_PER_C, 6.0f, 2.0f * tau),
      pump.feedback.limitCorrectionMin);
  // Six time constants: the whole correction.
  trackSlowly(4U * AXIS_TAU_MS);
  TEST_ASSERT_INT32_WITHIN(1, finalMin, pump.feedback.limitCorrectionMin);
  TEST_ASSERT_INT32_WITHIN(1, finalMax, pump.feedback.limitCorrectionMax);
  TEST_ASSERT_EQUAL_INT32(-130 + pump.feedback.limitCorrectionMin,
                          pump.feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT32(8000 + pump.feedback.limitCorrectionMax,
                          pump.feedback.adjustMax);
  TEST_ASSERT_EQUAL_INT32(
      pump.feedback.adjustMin +
          ((pump.feedback.adjustMax - pump.feedback.adjustMin) / 2),
      pump.feedback.adjustMiddle);
  // The measured limits stay as calibrated; the rest follows the bottom.
  TEST_ASSERT_EQUAL_INT32(-130, pump.feedback.calibratedMin);
  TEST_ASSERT_EQUAL_INT32(8000, pump.feedback.calibratedMax);
  TEST_ASSERT_TRUE(pump.demand.atRest);
  TEST_ASSERT_EQUAL_INT32(pump.feedback.adjustMin, pump.demand.target);
  TEST_ASSERT_EQUAL_INT32(pump.feedback.adjustMin, pump.demand.desired);
  TEST_ASSERT_EQUAL_INT32(0, board.lastPwm);
}

static void test_limit_correction_moves_at_most_one_hertz_a_step(void) {
  startResting();
  // The guard behind the filter: a filtered temperature that jumps, which the
  // filter itself never does, still moves the limits one hertz a step.
  board.fuelTempC = 35;
  pump.feedback.fuelTempFilteredC = 35.0f;
  track(1U);
  TEST_ASSERT_EQUAL_INT32(-129, pump.feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT32(8001, pump.feedback.adjustMax);
  track(9U);
  TEST_ASSERT_EQUAL_INT32(-120, pump.feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT32(8010, pump.feedback.adjustMax);
  track(200U);
  TEST_ASSERT_EQUAL_INT16(161, pump.feedback.limitCorrectionMin);
  TEST_ASSERT_EQUAL_INT16(88, pump.feedback.limitCorrectionMax);
}

static void test_limit_correction_holds_beyond_the_measured_range(void) {
  startResting();
  const float maxDeltaC = (float)VP37_LIMIT_FT_MAX_DELTA_C;
  board.fuelTempC = (int16_t)(25 + VP37_LIMIT_FT_MAX_DELTA_C + 15);
  trackSlowly(3U * AXIS_TAU_MS);
  TEST_ASSERT_EQUAL_INT16(
      correctionFor(VP37_LIMIT_FT_REST_SLOPE_DHZ_PER_C, maxDeltaC),
      pump.feedback.limitCorrectionMin);
  TEST_ASSERT_EQUAL_INT16(
      correctionFor(VP37_LIMIT_FT_TOP_SLOPE_DHZ_PER_C, maxDeltaC),
      pump.feedback.limitCorrectionMax);
  board.fuelTempC = (int16_t)(25 - VP37_LIMIT_FT_MAX_DELTA_C - 15);
  trackSlowly(3U * AXIS_TAU_MS);
  TEST_ASSERT_EQUAL_INT16(
      correctionFor(VP37_LIMIT_FT_REST_SLOPE_DHZ_PER_C, -maxDeltaC),
      pump.feedback.limitCorrectionMin);
  TEST_ASSERT_EQUAL_INT16(
      correctionFor(VP37_LIMIT_FT_TOP_SLOPE_DHZ_PER_C, -maxDeltaC),
      pump.feedback.limitCorrectionMax);
  TEST_ASSERT_EQUAL_INT32(-130 + pump.feedback.limitCorrectionMin,
                          pump.feedback.adjustMin);
}

static void test_limit_filter_freezes_on_a_broken_sensor_and_restarts(void) {
  startResting();
  TEST_ASSERT_EQUAL_FLOAT(25.0f, pump.feedback.fuelTempFilteredC);
  board.fuelTempC = 35;
  trackSlowly(AXIS_TAU_MS / 2U);
  const float filtered = pump.feedback.fuelTempFilteredC;
  const int16_t minCorrection = pump.feedback.limitCorrectionMin;
  const int16_t maxCorrection = pump.feedback.limitCorrectionMax;
  TEST_ASSERT_GREATER_THAN_FLOAT(25.0f, filtered);
  TEST_ASSERT_GREATER_THAN_INT16(0, minCorrection);
  // A broken sensor keeps the filter and the correction where they are.
  board.status = ADJ_STATUS_FUEL_TEMP_BROKEN;
  board.fuelTempC = 60;
  trackSlowly(AXIS_TAU_MS);
  TEST_ASSERT_EQUAL_FLOAT(filtered, pump.feedback.fuelTempFilteredC);
  TEST_ASSERT_EQUAL_INT16(minCorrection, pump.feedback.limitCorrectionMin);
  TEST_ASSERT_EQUAL_INT16(maxCorrection, pump.feedback.limitCorrectionMax);
  TEST_ASSERT_EQUAL_INT32(-130 + minCorrection, pump.feedback.adjustMin);
  // A new calibration starts the filter at its own reference.
  VP37_stop(&pump);
  startPump(-100, 8100, 33, 33, ADJ_STATUS_OK);
  TEST_ASSERT_EQUAL_INT16(330, pump.feedback.calibrationFtDeciC);
  TEST_ASSERT_EQUAL_FLOAT(33.0f, pump.feedback.fuelTempFilteredC);
  TEST_ASSERT_EQUAL_INT16(0, pump.feedback.limitCorrectionMin);
  TEST_ASSERT_EQUAL_INT32(-100, pump.feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(&pump, 0.0f));
  trackSlowly(AXIS_TAU_MS);
  TEST_ASSERT_EQUAL_INT16(0, pump.feedback.limitCorrectionMin);
  TEST_ASSERT_EQUAL_INT16(0, pump.feedback.limitCorrectionMax);
  TEST_ASSERT_EQUAL_INT32(-100, pump.feedback.adjustMin);
}

static void test_percent_demand_keeps_its_place_as_the_limits_move(void) {
  startPump(-130, 8000, 25, 25, ADJ_STATUS_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(&pump, 50.0f));
  track(300U);
  const int32_t before = pump.demand.target;
  TEST_ASSERT_EQUAL_INT32(before, pump.demand.desired);
  const uint32_t changedMs = pump.demand.targetChangedMs;
  board.fuelTempC = 35;
  track(2000U);
  // The stored percent maps onto the limits in effect, and the moving limits
  // neither restart the settle time nor make the target a tracked one.
  TEST_ASSERT_GREATER_THAN_INT16(0, pump.feedback.limitCorrectionMin);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, pump.demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(percentTarget(50.0f), pump.demand.target);
  TEST_ASSERT_GREATER_THAN_INT32(before, pump.demand.target);
  TEST_ASSERT_EQUAL_INT32(pump.demand.target, pump.demand.desired);
  TEST_ASSERT_EQUAL_UINT32(changedMs, pump.demand.targetChangedMs);
  TEST_ASSERT_FALSE(pump.demand.targetMoving);
  TEST_ASSERT_FALSE(pump.demand.atRest);
}

static void test_counts_demand_keeps_its_counts_as_the_limits_move(void) {
  startPump(-130, 8000, 25, 25, ADJ_STATUS_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandValue(&pump, 3000));
  track(300U);
  const float percent = pump.demand.requestedPercent;
  board.fuelTempC = 35;
  track(4000U);
  TEST_ASSERT_GREATER_THAN_INT16(30, pump.feedback.limitCorrectionMin);
  TEST_ASSERT_EQUAL_INT32(3000, pump.demand.target);
  TEST_ASSERT_EQUAL_INT32(3000, pump.demand.desired);
  // The percent the telemetry reports follows the counts on the new stroke.
  TEST_ASSERT_LESS_THAN_FLOAT(percent, pump.demand.requestedPercent);
  TEST_ASSERT_FLOAT_WITHIN(
      0.01f,
      hal_math_map_f32(3000.0f, (float)pump.feedback.adjustMin,
                       (float)VP37_getPositionDemandMaxValue(&pump), 0.0f,
                       100.0f),
      pump.demand.requestedPercent);
  // A bottom that has risen past a counts demand clamps it, and the clamped
  // counts stay when the bottom sinks again.
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandValue(&pump, -100));
  const int32_t clamped = pump.demand.target;
  TEST_ASSERT_EQUAL_INT32(pump.feedback.adjustMin, clamped);
  TEST_ASSERT_GREATER_THAN_INT32(-100, clamped);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump.demand.requestedPercent);
  board.fuelTempC = 25;
  track(4000U);
  TEST_ASSERT_LESS_THAN_INT32(clamped, pump.feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT32(clamped, pump.demand.target);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump.demand.requestedPercent);
}
#else
static void test_limits_stay_calibrated_with_the_correction_off(void) {
  startResting();
  TEST_ASSERT_TRUE(pump.feedback.calibrationFtValid);
  (void)VP37_setPositionDemandPercentage(&pump, 50.0f);
  board.fuelTempC = 40;
  track(400U);
  TEST_ASSERT_EQUAL_INT16(0, pump.feedback.limitCorrectionMin);
  TEST_ASSERT_EQUAL_INT16(0, pump.feedback.limitCorrectionMax);
  TEST_ASSERT_EQUAL_INT32(-130, pump.feedback.adjustMin);
  TEST_ASSERT_EQUAL_INT32(8000, pump.feedback.adjustMax);
  TEST_ASSERT_EQUAL_INT32(percentTarget(50.0f), pump.demand.target);
  trackSlowly(AXIS_TAU_MS);
  TEST_ASSERT_EQUAL_FLOAT(25.0f, pump.feedback.fuelTempFilteredC);
  TEST_ASSERT_EQUAL_INT32(-130, pump.feedback.adjustMin);
}
#endif

#if VP37_TELEMETRY_ENABLED
// ── Native calibration report ───────────────────────────────────────────────

static void test_report_does_not_access_the_feedback_bus(void) {
  startPump(-130, 8000, 25, 25, ADJ_STATUS_OK);
  (void)VP37_setPositionDemandPercentage(&pump, 50.0f);
  track(20U);
  fiesta_test_set_millis(hal_millis() + VP37_TELEMETRY_UPDATE + 1U);
  VP37_publish(&pump);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readSnapshot(&pump, &snapshot));
  const uint32_t transfers = board.transfers;
  VP37_showDebug(&pump, &snapshot);
  TEST_ASSERT_EQUAL_UINT32(transfers, board.transfers);
}

static void test_calibration_report_prints_the_signed_position(void) {
  startPump(-130, 8000, 25, 25, ADJ_STATUS_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_init());
  fiesta_test_usb_connected(true);
  VP37_showCalibration();
  const char *line = fiesta_test_debug_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "VP37 CAL "));
  TEST_ASSERT_NOT_NULL(strstr(line, "phase:MIN"));
  TEST_ASSERT_NOT_NULL(strstr(line, " adj:-130 "));
  TEST_ASSERT_NOT_NULL(strstr(line, " p:130"));
  // Two samples a call: more calls than the trace has samples.
  for (uint32_t i = 0U; i < 128U; i++) {
    VP37_showCalibration();
    line = fiesta_test_debug_last_line();
    if (strstr(line, "VP37 CAL END") != NULL) {
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(strstr(line, "VP37 CAL END"));
  TEST_ASSERT_NOT_NULL(strstr(line, " min:-130 max:8000 "));
  TEST_ASSERT_NOT_NULL(strstr(line, " calft:25.0 calftok:1"));
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_feedback_caches_diagnostics_without_using_their_position);
  RUN_TEST(test_position_above_the_baseline_reads_negative);
  RUN_TEST(test_position_near_the_baseline_keeps_its_signed_value);
  RUN_TEST(test_signal_loss_and_pending_baseline_keep_the_last_position);
  RUN_TEST(test_signal_loss_still_stops_the_running_pump);
  RUN_TEST(test_full_diagnostic_queue_cannot_delay_the_signal_loss_stop);
  RUN_TEST(test_calibration_keeps_a_rest_above_the_baseline_below_zero);
  RUN_TEST(test_calibration_takes_the_mean_fuel_temperature_of_both_ends);
  RUN_TEST(test_calibration_with_a_broken_fuel_sensor_has_no_reference);
  RUN_TEST(test_calibration_accepts_a_stroke_centred_below_zero);
  RUN_TEST(test_bottom_below_zero_rests_at_zero_demand);
  RUN_TEST(test_negative_target_ramps_holds_and_returns_to_rest);
  RUN_TEST(test_uncalibrated_pump_has_no_position_limits);
#if VP37_LIMIT_FT_CORRECTION_ENABLED
  RUN_TEST(test_limit_correction_ignores_a_toggling_degree);
  RUN_TEST(test_limit_correction_follows_a_lasting_step_with_its_lag);
  RUN_TEST(test_limit_correction_moves_at_most_one_hertz_a_step);
  RUN_TEST(test_limit_correction_holds_beyond_the_measured_range);
  RUN_TEST(test_limit_filter_freezes_on_a_broken_sensor_and_restarts);
  RUN_TEST(test_percent_demand_keeps_its_place_as_the_limits_move);
  RUN_TEST(test_counts_demand_keeps_its_counts_as_the_limits_move);
#else
  RUN_TEST(test_limits_stay_calibrated_with_the_correction_off);
#endif
#if VP37_TELEMETRY_ENABLED
  RUN_TEST(test_report_does_not_access_the_feedback_bus);
  RUN_TEST(test_calibration_report_prints_the_signed_position);
#endif
  return UNITY_END();
}

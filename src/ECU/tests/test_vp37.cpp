#include "dtcManager.h"
#include "ecuContext.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
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
  pump->throttleRampLastMs = hal_millis();
  pump->lastAdjustometerStatus = ADJ_STATUS_OK;
  hal_pid_controller_set_kp(pump->adjustController, VP37_PID_KP);
  hal_pid_controller_set_ki(pump->adjustController, VP37_PID_KI);
  hal_pid_controller_set_kd(pump->adjustController, VP37_PID_KD);
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

  int32_t expectedTarget = (int32_t)mapfloat(
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
  TEST_ASSERT_FLOAT_WITHIN(0.01f, VP37_PID_CORR_LIMIT_POSITIVE_COLD,
                           VP37_computePositiveCorrectionLimit(
                               22.0f, ADJ_STATUS_OK, VP37_PWM_FF_AT_MAX));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 323.71f,
                           VP37_computePositiveCorrectionLimit(
                               51.0f, ADJ_STATUS_OK, VP37_PWM_FF_AT_MAX));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 338.02f,
                           VP37_computePositiveCorrectionLimit(
                               55.0f, ADJ_STATUS_OK, VP37_PWM_FF_AT_MAX));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 310.78f,
                           VP37_computePositiveCorrectionLimit(
                               55.0f, ADJ_STATUS_OK, VP37_PWM_FF_AT_MIN));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, VP37_PID_CORR_LIMIT_POSITIVE_MAX,
                           VP37_computePositiveCorrectionLimit(
                               80.0f, ADJ_STATUS_OK, VP37_PWM_FF_AT_MAX));
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
  hal_pid_controller_set_kp(pump->adjustController, 1.0f);
  hal_pid_controller_set_ki(pump->adjustController, 0.0f);
  hal_pid_controller_set_kd(pump->adjustController, 0.0f);

  VP37_setVP37Throttle(pump, 100.0f);
  injectAdjRegisterData(100, 144, 55, ADJ_STATUS_OK);
  VP37_process(pump);

  TEST_ASSERT_FLOAT_WITHIN(0.05f, 338.02f, pump->pidPositiveLimit);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 338.02f, pump->pidCorrection);
  TEST_ASSERT_TRUE(pump->pidSaturatedHigh);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1028.02f, pump->pwmValue);
}

void test_vp37_hot_negative_error_keeps_original_range(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  setupPumpForProcessTests(pump);
  hal_pid_controller_set_kp(pump->adjustController, 1.0f);
  hal_pid_controller_set_ki(pump->adjustController, 0.0f);
  hal_pid_controller_set_kd(pump->adjustController, 0.0f);

  VP37_setVP37Throttle(pump, 0.0f);
  injectAdjRegisterData(9100, 144, 55, ADJ_STATUS_OK);
  VP37_process(pump);

  TEST_ASSERT_FLOAT_WITHIN(0.05f, 310.78f, pump->pidPositiveLimit);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -220.0f, pump->pidCorrection);
  TEST_ASSERT_FALSE(pump->pidSaturatedHigh);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 260.0f, pump->pwmValue);
}

void test_vp37_pwm_limit_matches_physical_resolution(void) {
  TEST_ASSERT_EQUAL_INT32(PWM_RESOLUTION, VP37_PWM_MAX);
}

void test_vp37_process_disables_after_adj_comm_cutoff_timeout(void) {
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  setupPumpForProcessTests(pump);

  hal_mock_i2c_set_busy(true);

  hal_mock_set_millis(100);
  VP37_process(pump); // comm error #1 (commOk still true)
  VP37_process(pump); // comm error #2 (commOk still true)
  VP37_process(pump); // comm error #3 (commOk false, timestamp starts)
  TEST_ASSERT_TRUE(pump->vp37Initialized);
  TEST_ASSERT_EQUAL_UINT32(100, pump->adjCommLostSince);

  hal_mock_set_millis(100 + (uint32_t)(VP37_ADJ_COMM_CUTOFF_S * SECOND));
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

int main(void) {
  UNITY_BEGIN();

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

  return UNITY_END();
}

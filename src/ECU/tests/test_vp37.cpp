#include "unity.h"
#include "sensors.h"
#include "vp37.h"
#include "dtcManager.h"
#include "ecuContext.h"
#include "hal/impl/.mock/hal_mock.h"

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

static void injectAdjRegisterDataRepeated(int count, int16_t pulseHz,
                                          uint8_t voltage, uint8_t fuelTemp,
                                          uint8_t status) {
    for(int i = 0; i < count; i++) {
        injectAdjRegisterData(pulseHz, voltage, fuelTemp, status);
    }
}

static void setupPumpForProcessTests(VP37Pump *pump) {
    memset(pump, 0, sizeof(*pump));
    pump->adjustController = hal_pid_controller_create();
    pump->vp37Initialized = true;
    pump->calibrationDone = true;
    pump->VP37_ADJUST_MIN = 100;
    pump->VP37_ADJUST_MAX = 9100;
    pump->VP37_ADJUST_MIDDLE = (pump->VP37_ADJUST_MAX + pump->VP37_ADJUST_MIN) / 2;
    pump->desiredAdjustometerTarget = -1;
    pump->desiredAdjustometer = -1;
    pump->lastThrottle = -1.0f;
    pump->pidTimeUpdate = VP37_PID_TIME_UPDATE;
    pump->throttleRampLastMs = hal_millis();
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
    if(pump->adjustController != NULL) {
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

    int32_t expectedTarget = (int32_t)mapfloat((float)VP37_ACCELERATION_MAX,
                                               VP37_PERCENT_MIN,
                                               VP37_PERCENT_MAX,
                                               (float)pump->VP37_ADJUST_MIN,
                                               (float)pump->VP37_ADJUST_MAX);
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

    TEST_ASSERT_EQUAL_INT32(pump->VP37_ADJUST_MIN, pump->desiredAdjustometerTarget);
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

    // Ensure all reads done during baseline wait + calibration have valid data.
    injectAdjRegisterDataRepeated(64, 1200, 132, 40, ADJ_STATUS_OK);

    VP37InitStatus status = VP37_init(pump);

    TEST_ASSERT_EQUAL_INT(VP37_INIT_OK, status);
    TEST_ASSERT_TRUE(pump->vp37Initialized);
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

void test_vp37_process_throttle_ramp_down_is_time_gated(void) {
    ecu_context_t *ctx = getECUContext();
    VP37Pump *pump = &ctx->injectionPump;
    setupPumpForProcessTests(pump);

    // First cycle at full throttle initializes lastThrottle.
    setGlobalValue(F_THROTTLE_POS, (float)PWM_RESOLUTION);
    injectAdjRegisterData(500, 138, 40, ADJ_STATUS_OK);
    hal_mock_set_millis(100);
    VP37_process(pump);
    float firstThrottle = pump->lastThrottle;

    // Drop requested throttle but keep elapsed below ramp interval.
    setGlobalValue(F_THROTTLE_POS, 0.0f);
    injectAdjRegisterData(500, 138, 40, ADJ_STATUS_OK);
    hal_mock_set_millis(100 + VP37_THROTTLE_RAMP_DOWN_INTERVAL_MS - 1);
    VP37_process(pump);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, firstThrottle, pump->lastThrottle);

    // Once interval elapsed, ramp-down should reduce lastThrottle.
    injectAdjRegisterData(500, 138, 40, ADJ_STATUS_OK);
    hal_mock_set_millis(100 + VP37_THROTTLE_RAMP_DOWN_INTERVAL_MS);
    VP37_process(pump);
    TEST_ASSERT_TRUE(pump->lastThrottle < firstThrottle);
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
    RUN_TEST(test_vp37_process_disables_after_adj_comm_cutoff_timeout);
    RUN_TEST(test_vp37_process_disables_when_rpm_above_max);
    RUN_TEST(test_vp37_process_updates_globals_from_adjustometer_reading);
    RUN_TEST(test_vp37_process_throttle_ramp_down_is_time_gated);

    return UNITY_END();
}

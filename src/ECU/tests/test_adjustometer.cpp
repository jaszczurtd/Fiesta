#include "unity.h"
#include "sensors.h"
#include "vp37.h"
#include "dtcManager.h"
#include "ecuContext.h"
#include "hal/impl/.mock/hal_mock.h"

// ── Helpers ──────────────────────────────────────────────────────────────────

// Injects Adjustometer register data into mock I2C RX buffer.
// Layout: [pulseHi, pulseLo, voltage, fuelTemp, status]
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

// ── Lifecycle ────────────────────────────────────────────────────────────────

void setUp(void) {
    hal_mock_set_millis(0);
    hal_i2c_init(4, 5, 400000);
    initSensors();
    initI2C();
    dtcManagerInit();
    dtcManagerClearAll();
}

void tearDown(void) {}

// ── Adjustometer readout tests ───────────────────────────────────────────────

void test_adjustometer_positive_pulse(void) {
    // +500 Hz deviation, 13.2 V, 45 °C, status OK
    injectAdjRegisterData(500, 132, 45, ADJ_STATUS_OK);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);
    TEST_ASSERT_EQUAL_INT32(500, r.pulseHz);
}

void test_adjustometer_negative_pulse(void) {
    // -200 Hz deviation
    injectAdjRegisterData(-200, 120, 30, ADJ_STATUS_OK);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);
    TEST_ASSERT_EQUAL_INT32(-200, r.pulseHz);
}

void test_adjustometer_zero_pulse(void) {
    injectAdjRegisterData(0, 140, 50, ADJ_STATUS_OK);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);
    TEST_ASSERT_EQUAL_INT32(0, r.pulseHz);
}

void test_adjustometer_large_negative(void) {
    // INT16_MIN boundary
    injectAdjRegisterData(-32768, 100, 20, ADJ_STATUS_OK);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);
    TEST_ASSERT_EQUAL_INT32(-32768, r.pulseHz);
}

void test_adjustometer_voltage_conversion(void) {
    // Register value 138 → 13.8 V
    injectAdjRegisterData(0, 138, 40, ADJ_STATUS_OK);
    float v = getSystemSupplyVoltage();
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 13.8f, v);
}

void test_adjustometer_voltage_zero(void) {
    injectAdjRegisterData(0, 0, 25, ADJ_STATUS_OK);
    float v = getSystemSupplyVoltage();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, v);
}

void test_adjustometer_voltage_max(void) {
    // 255 → 25.5 V
    injectAdjRegisterData(0, 255, 20, ADJ_STATUS_OK);
    float v = getSystemSupplyVoltage();
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 25.5f, v);
}

void test_adjustometer_fuel_temp(void) {
    injectAdjRegisterData(0, 130, 85, ADJ_STATUS_OK);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);
    TEST_ASSERT_EQUAL_UINT8(85, r.fuelTempC);
}

void test_adjustometer_fuel_temp_zero(void) {
    injectAdjRegisterData(0, 130, 0, ADJ_STATUS_OK);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);
    TEST_ASSERT_EQUAL_UINT8(0, r.fuelTempC);
}

// ── Adjustometer status flags are currently informational only ──────────────

void test_adjustometer_status_signal_lost_does_not_auto_set_dtc(void) {
    injectAdjRegisterData(0, 120, 40, ADJ_STATUS_SIGNAL_LOST);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);
    TEST_ASSERT_EQUAL_INT32(0, r.pulseHz);
    TEST_ASSERT_EQUAL_UINT8(0, dtcManagerCount(DTC_KIND_ACTIVE));
}

void test_adjustometer_status_fuel_temp_broken_does_not_auto_set_dtc(void) {
    injectAdjRegisterData(0, 120, 0, ADJ_STATUS_FUEL_TEMP_BROKEN);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);
    TEST_ASSERT_EQUAL_UINT8(ADJ_STATUS_FUEL_TEMP_BROKEN, r.status);
    TEST_ASSERT_EQUAL_UINT8(0, dtcManagerCount(DTC_KIND_ACTIVE));
}

void test_adjustometer_status_voltage_bad_does_not_auto_set_dtc(void) {
    injectAdjRegisterData(0, 50, 40, ADJ_STATUS_VOLTAGE_BAD);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.0f, getSystemSupplyVoltage());
    TEST_ASSERT_EQUAL_UINT8(0, dtcManagerCount(DTC_KIND_ACTIVE));
}

void test_adjustometer_comm_lost_on_nack(void) {
    // Simulate I2C NACK — set busy flag so endTransmission returns != 0.
    // commOk goes false after ADJ_COMM_ERROR_THRESHOLD (3) consecutive errors.
    hal_mock_i2c_set_busy(true);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);  // error 1
    getVP37Adjustometer(&r);  // error 2
    getVP37Adjustometer(&r);  // error 3 → commOk = false
    float v = getSystemSupplyVoltage();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, v);
    hal_mock_i2c_set_busy(false);
}

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
    for(uint8_t i = 0; i < count; i++) {
        if(codes[i] == DTC_ADJ_COMM_LOST) foundComm = true;
        if(codes[i] == DTC_ADJ_SIGNAL_LOST) foundSig = true;
        if(codes[i] == DTC_ADJ_FUEL_TEMP_BROKEN) foundFt = true;
        if(codes[i] == DTC_ADJ_VOLTAGE_BAD) foundVolt = true;
    }
    TEST_ASSERT_TRUE(foundComm);
    TEST_ASSERT_TRUE(foundSig);
    TEST_ASSERT_TRUE(foundFt);
    TEST_ASSERT_TRUE(foundVolt);
}

// ── VP37 PID API tests ──────────────────────────────────────────────────────

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

    hal_pid_controller_destroy(pump->adjustController);
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

    hal_pid_controller_destroy(pump->adjustController);
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

void test_vp37_pid_time_update_setter(void) {
    ecu_context_t *ctx = getECUContext();
    VP37Pump *pump = &ctx->injectionPump;
    pump->pidTimeUpdate = 45.0f;

    pump->pidTimeUpdate = 60.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, VP37_getVP37PIDTimeUpdate(pump));
}

void test_vp37_percentage_error_constant(void) {
    ecu_context_t *ctx = getECUContext();
    VP37Pump *pump = &ctx->injectionPump;
    (void)pump;
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

// ── Int16 BE encoding edge cases ────────────────────────────────────────────

void test_adjustometer_big_endian_byte_order(void) {
    // Manually inject raw bytes: 0x01, 0x00 → +256 Hz
    uint8_t buf[5] = {0x01, 0x00, 130, 50, 0x00};
    hal_mock_i2c_inject_rx(buf, 5);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);
    TEST_ASSERT_EQUAL_INT32(256, r.pulseHz);
}

void test_adjustometer_big_endian_negative(void) {
    // 0xFF, 0x00 → -256 in int16_t (0xFF00 = -256)
    uint8_t buf[5] = {0xFF, 0x00, 130, 50, 0x00};
    hal_mock_i2c_inject_rx(buf, 5);
    adjustometer_reading_t r;
    getVP37Adjustometer(&r);
    TEST_ASSERT_EQUAL_INT32(-256, r.pulseHz);
}

// ── Runner ───────────────────────────────────────────────────────────────────

int main(void) {
    UNITY_BEGIN();

    // Adjustometer readout
    RUN_TEST(test_adjustometer_positive_pulse);
    RUN_TEST(test_adjustometer_negative_pulse);
    RUN_TEST(test_adjustometer_zero_pulse);
    RUN_TEST(test_adjustometer_large_negative);
    RUN_TEST(test_adjustometer_voltage_conversion);
    RUN_TEST(test_adjustometer_voltage_zero);
    RUN_TEST(test_adjustometer_voltage_max);
    RUN_TEST(test_adjustometer_fuel_temp);
    RUN_TEST(test_adjustometer_fuel_temp_zero);

    // Byte order
    RUN_TEST(test_adjustometer_big_endian_byte_order);
    RUN_TEST(test_adjustometer_big_endian_negative);

    // DTC status
    RUN_TEST(test_adjustometer_status_signal_lost_does_not_auto_set_dtc);
    RUN_TEST(test_adjustometer_status_fuel_temp_broken_does_not_auto_set_dtc);
    RUN_TEST(test_adjustometer_status_voltage_bad_does_not_auto_set_dtc);
    RUN_TEST(test_adjustometer_comm_lost_on_nack);
    RUN_TEST(test_dtc_array_covers_adjustometer_codes);

    // VP37 PID / throttle API
    RUN_TEST(test_vp37_pid_setter_updates_controller_gains);
    RUN_TEST(test_vp37_pid_reset_restores_pwm_tracking_state);
    RUN_TEST(test_vp37_throttle_caps_target_to_configured_range);
    RUN_TEST(test_vp37_pid_time_update_setter);
    RUN_TEST(test_vp37_percentage_error_constant);
    RUN_TEST(test_vp37_init_returns_already_initialized);
    RUN_TEST(test_vp37_init_returns_ok_when_baseline_ready);

    return UNITY_END();
}

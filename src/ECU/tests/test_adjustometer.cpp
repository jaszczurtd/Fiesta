#include "unity.h"
#include "sensors.h"
#include "vp37.h"
#include "dtcManager.h"
#include "ecuContext.h"
#include "hal/hal_eeprom.h"
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

// ── Lifecycle ────────────────────────────────────────────────────────────────

void setUp(void) {
    hal_mock_set_millis(0);
    hal_mock_eeprom_reset();
    hal_eeprom_init(HAL_EEPROM_RP2040, ECU_EEPROM_SIZE_BYTES, 0);
    initSensors();
    dtcManagerInit();
    dtcManagerClearAll();
}

void tearDown(void) {}

// ── Adjustometer readout tests ───────────────────────────────────────────────

void test_adjustometer_positive_pulse(void) {
    // +500 Hz deviation, 13.2 V, 45 °C, status OK
    injectAdjRegisterData(500, 132, 45, ADJ_STATUS_OK);
    int32_t pulse = getVP37Adjustometer();
    TEST_ASSERT_EQUAL_INT32(500, pulse);
}

void test_adjustometer_negative_pulse(void) {
    // -200 Hz deviation
    injectAdjRegisterData(-200, 120, 30, ADJ_STATUS_OK);
    int32_t pulse = getVP37Adjustometer();
    TEST_ASSERT_EQUAL_INT32(-200, pulse);
}

void test_adjustometer_zero_pulse(void) {
    injectAdjRegisterData(0, 140, 50, ADJ_STATUS_OK);
    int32_t pulse = getVP37Adjustometer();
    TEST_ASSERT_EQUAL_INT32(0, pulse);
}

void test_adjustometer_large_negative(void) {
    // INT16_MIN boundary
    injectAdjRegisterData(-32768, 100, 20, ADJ_STATUS_OK);
    int32_t pulse = getVP37Adjustometer();
    TEST_ASSERT_EQUAL_INT32(-32768, pulse);
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
    float t = getVP37FuelTemperature();
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 85.0f, t);
}

void test_adjustometer_fuel_temp_zero(void) {
    injectAdjRegisterData(0, 130, 0, ADJ_STATUS_OK);
    float t = getVP37FuelTemperature();
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, t);
}

// ── DTC status tests ─────────────────────────────────────────────────────────

void test_adjustometer_status_signal_lost_sets_dtc(void) {
    injectAdjRegisterData(0, 120, 40, ADJ_STATUS_SIGNAL_LOST);
    (void)getVP37Adjustometer();

    TEST_ASSERT_EQUAL_UINT8(1, dtcManagerCount(DTC_KIND_ACTIVE));
}

void test_adjustometer_status_ok_clears_dtc(void) {
    // First: set signal lost
    injectAdjRegisterData(0, 120, 40, ADJ_STATUS_SIGNAL_LOST);
    (void)getVP37Adjustometer();
    // Then: OK status
    injectAdjRegisterData(100, 120, 40, ADJ_STATUS_OK);
    (void)getVP37Adjustometer();

    // Signal lost DTC should no longer be active
    uint16_t codes[16];
    uint8_t count = dtcManagerGetCodes(DTC_KIND_ACTIVE, codes, 16);
    bool found = false;
    for(uint8_t i = 0; i < count; i++) {
        if(codes[i] == DTC_ADJ_SIGNAL_LOST) found = true;
    }
    TEST_ASSERT_FALSE(found);
}

void test_adjustometer_status_fuel_temp_broken(void) {
    injectAdjRegisterData(0, 120, 0, ADJ_STATUS_FUEL_TEMP_BROKEN);
    (void)getVP37FuelTemperature();

    uint16_t codes[16];
    uint8_t count = dtcManagerGetCodes(DTC_KIND_ACTIVE, codes, 16);
    bool found = false;
    for(uint8_t i = 0; i < count; i++) {
        if(codes[i] == DTC_ADJ_FUEL_TEMP_BROKEN) found = true;
    }
    TEST_ASSERT_TRUE(found);
}

void test_adjustometer_status_voltage_bad(void) {
    injectAdjRegisterData(0, 50, 40, ADJ_STATUS_VOLTAGE_BAD);
    (void)getSystemSupplyVoltage();

    uint16_t codes[16];
    uint8_t count = dtcManagerGetCodes(DTC_KIND_ACTIVE, codes, 16);
    bool found = false;
    for(uint8_t i = 0; i < count; i++) {
        if(codes[i] == DTC_ADJ_VOLTAGE_BAD) found = true;
    }
    TEST_ASSERT_TRUE(found);
}

void test_adjustometer_comm_lost_on_busy_bus(void) {
    // Simulate I2C bus failure
    hal_mock_i2c_set_busy(true);
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

// ── VP37 PID EEPROM save/load tests ─────────────────────────────────────────

void test_vp37_pid_save_and_load(void) {
    ecu_context_t *ctx = getECUContext();
    VP37Pump *pump = &ctx->injectionPump;
    memset(pump, 0, sizeof(*pump));
    pump->adjustController = hal_pid_controller_create();
    pump->pidTimeUpdate = VP37_PID_TIME_UPDATE_DEFAULT;

    // Set custom PID values
    VP37_setVP37PID(pump, 0.55f, 0.12f, 0.025f, false);
    pump->pidTimeUpdate = 50.0f;

    // Save
    TEST_ASSERT_TRUE(VP37_savePIDToEEPROM(pump));

    // Change values
    VP37_setVP37PID(pump, 0.0f, 0.0f, 0.0f, false);
    pump->pidTimeUpdate = 1.0f;

    // Reload
    TEST_ASSERT_TRUE(VP37_loadPIDFromEEPROM(pump));

    float kp, ki, kd;
    VP37_getVP37PIDValues(pump, &kp, &ki, &kd);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.55f, kp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.12f, ki);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.025f, kd);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, pump->pidTimeUpdate);

    hal_pid_controller_destroy(pump->adjustController);
}

void test_vp37_pid_load_returns_false_when_empty(void) {
    ecu_context_t *ctx = getECUContext();
    VP37Pump *pump = &ctx->injectionPump;
    memset(pump, 0, sizeof(*pump));
    pump->adjustController = hal_pid_controller_create();

    // EEPROM is clean, load should fail
    TEST_ASSERT_FALSE(VP37_loadPIDFromEEPROM(pump));

    hal_pid_controller_destroy(pump->adjustController);
}

void test_vp37_pid_time_update_setter(void) {
    ecu_context_t *ctx = getECUContext();
    VP37Pump *pump = &ctx->injectionPump;
    pump->pidTimeUpdate = 45.0f;

    VP37_setVP37PIDTimeUpdate(pump, 60.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, VP37_getVP37PIDTimeUpdate(pump));

    // Negative value should not change
    VP37_setVP37PIDTimeUpdate(pump, -5.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, VP37_getVP37PIDTimeUpdate(pump));
}

void test_vp37_percentage_error_setter(void) {
    ecu_context_t *ctx = getECUContext();
    VP37Pump *pump = &ctx->injectionPump;
    pump->percentageError = 3.0f;

    VP37_setPercentageError(pump, 5.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, pump->percentageError);
}

// ── Int16 BE encoding edge cases ────────────────────────────────────────────

void test_adjustometer_big_endian_byte_order(void) {
    // Manually inject raw bytes: 0x01, 0x00 → +256 Hz
    uint8_t buf[5] = {0x01, 0x00, 130, 50, 0x00};
    hal_mock_i2c_inject_rx(buf, 5);
    int32_t pulse = getVP37Adjustometer();
    TEST_ASSERT_EQUAL_INT32(256, pulse);
}

void test_adjustometer_big_endian_negative(void) {
    // 0xFF, 0x00 → -256 in int16_t (0xFF00 = -256)
    uint8_t buf[5] = {0xFF, 0x00, 130, 50, 0x00};
    hal_mock_i2c_inject_rx(buf, 5);
    int32_t pulse = getVP37Adjustometer();
    TEST_ASSERT_EQUAL_INT32(-256, pulse);
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
    RUN_TEST(test_adjustometer_status_signal_lost_sets_dtc);
    RUN_TEST(test_adjustometer_status_ok_clears_dtc);
    RUN_TEST(test_adjustometer_status_fuel_temp_broken);
    RUN_TEST(test_adjustometer_status_voltage_bad);
    RUN_TEST(test_adjustometer_comm_lost_on_busy_bus);
    RUN_TEST(test_dtc_array_covers_adjustometer_codes);

    // VP37 PID EEPROM
    RUN_TEST(test_vp37_pid_save_and_load);
    RUN_TEST(test_vp37_pid_load_returns_false_when_empty);
    RUN_TEST(test_vp37_pid_time_update_setter);
    RUN_TEST(test_vp37_percentage_error_setter);

    return UNITY_END();
}

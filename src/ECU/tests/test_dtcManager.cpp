#include "unity.h"
#include "dtcManager.h"
#include "sensors.h"
#include "hal/hal_eeprom.h"
#include "hal/impl/.mock/hal_mock.h"

static bool containsCode(const uint16_t *codes, uint8_t count, uint16_t code) {
    for(uint8_t i = 0; i < count; i++) {
        if(codes[i] == code) {
            return true;
        }
    }
    return false;
}

void setUp(void) {
    hal_mock_set_millis(0);
    hal_mock_eeprom_reset();
    hal_eeprom_init(HAL_EEPROM_RP2040, ECU_EEPROM_SIZE_BYTES, 0);
    initSensors();
    hal_mock_gps_reset();

    dtcManagerInit();
    dtcManagerClearAll();
}

void tearDown(void) {}

void test_dtc_set_active_updates_all_kinds(void) {
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, true);

    TEST_ASSERT_EQUAL_UINT8(1, dtcManagerCount(DTC_KIND_ACTIVE));
    TEST_ASSERT_EQUAL_UINT8(1, dtcManagerCount(DTC_KIND_STORED));
    TEST_ASSERT_EQUAL_UINT8(1, dtcManagerCount(DTC_KIND_PERMANENT));

    uint16_t codes[4] = {0};
    uint8_t n = dtcManagerGetCodes(DTC_KIND_STORED, codes, 4);
    TEST_ASSERT_EQUAL_UINT8(1, n);
    TEST_ASSERT_TRUE(containsCode(codes, n, DTC_OBD_CAN_INIT_FAIL));
}

void test_dtc_deactivate_clears_only_active_flag(void) {
    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);
    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, false);

    TEST_ASSERT_EQUAL_UINT8(0, dtcManagerCount(DTC_KIND_ACTIVE));
    TEST_ASSERT_EQUAL_UINT8(1, dtcManagerCount(DTC_KIND_STORED));
    TEST_ASSERT_EQUAL_UINT8(1, dtcManagerCount(DTC_KIND_PERMANENT));
}

void test_dtc_clear_all_resets_all_kinds(void) {
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, true);
    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);

    dtcManagerClearAll();

    TEST_ASSERT_EQUAL_UINT8(0, dtcManagerCount(DTC_KIND_ACTIVE));
    TEST_ASSERT_EQUAL_UINT8(0, dtcManagerCount(DTC_KIND_STORED));
    TEST_ASSERT_EQUAL_UINT8(0, dtcManagerCount(DTC_KIND_PERMANENT));
}

void test_dtc_timestamp_nonzero_when_gps_is_available(void) {
    hal_mock_gps_set_valid(true);
    hal_mock_gps_set_age(0);
    hal_mock_gps_set_date(2026, 4, 4);
    hal_mock_gps_set_time(12, 0, 0);

    dtcManagerSetActive(DTC_PWM_CHANNEL_NOT_INIT, true);
    uint32_t ts = dtcManagerGetTimestamp(DTC_PWM_CHANNEL_NOT_INIT);

    TEST_ASSERT_TRUE(ts > 0u);
}

void test_dtc_timestamp_zero_when_gps_is_unavailable(void) {
    hal_mock_gps_set_valid(false);
    hal_mock_gps_set_age(0);

    dtcManagerSetActive(DTC_DPF_COMM_LOST, true);
    uint32_t ts = dtcManagerGetTimestamp(DTC_DPF_COMM_LOST);

    TEST_ASSERT_EQUAL_UINT32(0u, ts);
}

void test_dtc_get_codes_respects_output_limit(void) {
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, true);
    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);
    dtcManagerSetActive(DTC_PWM_CHANNEL_NOT_INIT, true);

    uint16_t codes[2] = {0};
    uint8_t n = dtcManagerGetCodes(DTC_KIND_STORED, codes, 2);

    TEST_ASSERT_EQUAL_UINT8(2, n);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dtc_set_active_updates_all_kinds);
    RUN_TEST(test_dtc_deactivate_clears_only_active_flag);
    RUN_TEST(test_dtc_clear_all_resets_all_kinds);
    RUN_TEST(test_dtc_timestamp_nonzero_when_gps_is_available);
    RUN_TEST(test_dtc_timestamp_zero_when_gps_is_unavailable);
    RUN_TEST(test_dtc_get_codes_respects_output_limit);
    return UNITY_END();
}

#include "unity.h"

#include "config.h"
#include "dtcManager.h"
#include "testable/ecuParams_testable.h"

#include "hal/hal_eeprom.h"
#include "hal/hal_kv.h"
#include "hal/impl/.mock/hal_mock.h"

static void assertActiveEquals(const ecu_params_values_t *expected) {
  const ecu_params_values_t *active = ecuParamsActive();
  TEST_ASSERT_EQUAL_INT16(expected->fanCoolantStartC, active->fanCoolantStartC);
  TEST_ASSERT_EQUAL_INT16(expected->fanCoolantStopC, active->fanCoolantStopC);
  TEST_ASSERT_EQUAL_INT16(expected->fanAirStartC, active->fanAirStartC);
  TEST_ASSERT_EQUAL_INT16(expected->fanAirStopC, active->fanAirStopC);
  TEST_ASSERT_EQUAL_INT16(expected->heaterStopC, active->heaterStopC);
  TEST_ASSERT_EQUAL_INT16(expected->nominalRpm, active->nominalRpm);
}

void setUp(void) {
  hal_mock_set_millis(0);
  dtcManagerClearAll();
  ecuParamsResetRuntimeStateForTest();
}

void tearDown(void) {}

void test_ecu_params_init_uses_config_defaults_when_blob_missing(void) {
  ecuParamsInit();

  ecu_params_values_t expected = {
    .fanCoolantStartC = TEMP_FAN_START,
    .fanCoolantStopC = TEMP_FAN_STOP,
    .fanAirStartC = AIR_TEMP_FAN_START,
    .fanAirStopC = AIR_TEMP_FAN_STOP,
    .heaterStopC = TEMP_HEATER_STOP,
    .nominalRpm = NOMINAL_RPM_VALUE
  };
  assertActiveEquals(&expected);
}

void test_ecu_params_init_loads_valid_persisted_blob(void) {
  ecu_params_values_t candidate = {
    .fanCoolantStartC = 108,
    .fanCoolantStopC = 96,
    .fanAirStartC = 58,
    .fanAirStopC = 44,
    .heaterStopC = 79,
    .nominalRpm = 930
  };

  TEST_ASSERT_TRUE(ecuParamsValidate(&candidate, NULL));
  TEST_ASSERT_TRUE(ecuParamsPersist(&candidate));

  ecuParamsResetRuntimeStateForTest();
  ecuParamsInit();
  assertActiveEquals(&candidate);
}

void test_ecu_params_init_falls_back_to_defaults_on_malformed_blob(void) {
  uint8_t malformedBlob[3] = {1u, 2u, 3u};
  TEST_ASSERT_TRUE(hal_kv_set_blob(ecuParamsBlobKeyForTest(),
                                   malformedBlob,
                                   (uint16_t)sizeof(malformedBlob)));

  ecuParamsInit();

  ecu_params_values_t expected = {
    .fanCoolantStartC = TEMP_FAN_START,
    .fanCoolantStopC = TEMP_FAN_STOP,
    .fanAirStartC = AIR_TEMP_FAN_START,
    .fanAirStopC = AIR_TEMP_FAN_STOP,
    .heaterStopC = TEMP_HEATER_STOP,
    .nominalRpm = NOMINAL_RPM_VALUE
  };
  assertActiveEquals(&expected);
}

void test_ecu_params_validate_rejects_invalid_hysteresis(void) {
  ecu_params_values_t candidate = {
    .fanCoolantStartC = TEMP_FAN_START,
    .fanCoolantStopC = TEMP_FAN_START,
    .fanAirStartC = AIR_TEMP_FAN_START,
    .fanAirStopC = AIR_TEMP_FAN_STOP,
    .heaterStopC = TEMP_HEATER_STOP,
    .nominalRpm = NOMINAL_RPM_VALUE
  };

  const char *reason = NULL;
  TEST_ASSERT_FALSE(ecuParamsValidate(&candidate, &reason));
  TEST_ASSERT_EQUAL_STRING("fan_coolant_hysteresis", reason);
}

void test_ecu_params_stage_and_apply_updates_active_set(void) {
  ecuParamsInit();
  const int16_t defaultHeaterStop = ecuParamsHeaterStop();
  const int16_t defaultNominalRpm = ecuParamsNominalRpm();

  ecu_params_values_t candidate = {
    .fanCoolantStartC = 109,
    .fanCoolantStopC = 95,
    .fanAirStartC = 57,
    .fanAirStopC = 43,
    .heaterStopC = 78,
    .nominalRpm = 920
  };

  TEST_ASSERT_TRUE(ecuParamsStage(&candidate, NULL));
  TEST_ASSERT_EQUAL_INT16(defaultHeaterStop, ecuParamsHeaterStop());
  TEST_ASSERT_EQUAL_INT16(defaultNominalRpm, ecuParamsNominalRpm());

  ecuParamsApply();
  assertActiveEquals(&candidate);
}

void test_ecu_params_validate_rejects_nominal_rpm_out_of_range(void) {
  ecu_params_values_t candidate = {
    .fanCoolantStartC = TEMP_FAN_START,
    .fanCoolantStopC = TEMP_FAN_STOP,
    .fanAirStartC = AIR_TEMP_FAN_START,
    .fanAirStopC = AIR_TEMP_FAN_STOP,
    .heaterStopC = TEMP_HEATER_STOP,
    .nominalRpm = (int16_t)(ECU_PARAMS_NOMINAL_RPM_MAX + 1)
  };

  const char *reason = NULL;
  TEST_ASSERT_FALSE(ecuParamsValidate(&candidate, &reason));
  TEST_ASSERT_EQUAL_STRING("nominal_rpm_range", reason);
}

int main(void) {
  hal_mock_eeprom_reset();
  hal_eeprom_init(HAL_EEPROM_RP2040, ECU_EEPROM_SIZE_BYTES, 0);
  dtcManagerInit();

  UNITY_BEGIN();
  RUN_TEST(test_ecu_params_init_uses_config_defaults_when_blob_missing);
  RUN_TEST(test_ecu_params_init_loads_valid_persisted_blob);
  RUN_TEST(test_ecu_params_init_falls_back_to_defaults_on_malformed_blob);
  RUN_TEST(test_ecu_params_validate_rejects_invalid_hysteresis);
  RUN_TEST(test_ecu_params_validate_rejects_nominal_rpm_out_of_range);
  RUN_TEST(test_ecu_params_stage_and_apply_updates_active_set);
  return UNITY_END();
}

#include "dtc_codes.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

namespace {

struct ProjectCode {
  uint16_t alias;
  uint16_t wireValue;
};

// The wire values the ECU reported before the catalogue existed. A scan tool
// and the OBD provider know these numbers; the aliases must keep them.
const ProjectCode kProjectCodes[] = {
    {DTC_OBD_CAN_INIT_FAIL, 0xD900U},    {DTC_PCF8574_COMM_FAIL, 0xD90DU},
    {DTC_CAN_BUS_FAULT, 0xC073U},        {DTC_PWM_CHANNEL_NOT_INIT, 0x0657U},
    {DTC_DPF_COMM_LOST, 0xC100U},        {DTC_EGT_COMM_LOST, 0xD902U},
    {DTC_CAN0_INIT_FAIL, 0xD903U},       {DTC_ISOTP_FC_TIMEOUT, 0xD906U},
    {DTC_ISOTP_FC_ABORT, 0xD907U},       {DTC_ENGINE_OVERSPEED, 0x0219U},
    {DTC_ECM_EEPROM_FAULT, 0x062FU},     {DTC_SYSTEM_VOLTAGE_LOW, 0x0562U},
    {DTC_SYSTEM_VOLTAGE_HIGH, 0x0563U},  {DTC_THROTTLE_RANGE_PERF, 0x0121U},
    {DTC_COOLANT_TEMP_RANGE, 0x0116U},   {DTC_INTAKE_TEMP_RANGE, 0x0111U},
    {DTC_MAP_BARO_RANGE, 0x0106U},       {DTC_FUEL_LEVEL_RANGE, 0x0460U},
    {DTC_ADJ_COMM_LOST, 0xD908U},        {DTC_ADJ_SIGNAL_LOST, 0xD909U},
    {DTC_ADJ_FUEL_TEMP_BROKEN, 0xD90AU}, {DTC_ADJ_VOLTAGE_BAD, 0xD90BU},
    {DTC_RPM_IRQ_INIT_FAIL, 0xD90CU},
};

} // namespace

void test_catalog_ascends_without_duplicates(void) {
  const size_t count = obdDtcCatalogCount();
  TEST_ASSERT_TRUE(count > 1600U);

  uint16_t previous = 0U;
  for (size_t i = 0U; i < count; i++) {
    const obd_dtc_entry_t *entry = obdDtcCatalogEntry(i);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_NOT_NULL(entry->description);
    TEST_ASSERT_TRUE(entry->description[0] != '\0');
    if (i > 0U) {
      TEST_ASSERT_TRUE_MESSAGE(entry->code > previous,
                               "catalogue rows must ascend by value");
    }
    previous = entry->code;
  }
  TEST_ASSERT_NULL(obdDtcCatalogEntry(count));
}

void test_project_aliases_keep_their_wire_values(void) {
  for (const ProjectCode &code : kProjectCodes) {
    TEST_ASSERT_EQUAL_HEX16(code.wireValue, code.alias);
  }
}

void test_every_project_code_has_a_description(void) {
  for (const ProjectCode &code : kProjectCodes) {
    const char *description = obdDtcDescription(code.alias);
    TEST_ASSERT_NOT_NULL_MESSAGE(description, "alias without a catalogue row");
    TEST_ASSERT_TRUE(description[0] != '\0');
  }
}

void test_descriptions_are_the_catalogue_texts(void) {
  TEST_ASSERT_EQUAL_STRING("System Voltage Low",
                           obdDtcDescription(DTC_SYSTEM_VOLTAGE_LOW));
  TEST_ASSERT_EQUAL_STRING(
      "Engine Coolant Temperature Circuit Range/Performance Problem",
      obdDtcDescription(DTC_COOLANT_TEMP_RANGE));
  TEST_ASSERT_EQUAL_STRING("Internal Control Module EEPROM Error",
                           obdDtcDescription(DTC_ECM_EEPROM_FAULT));
  TEST_ASSERT_EQUAL_STRING("Lost Communication With ECM/PCM \"A\"",
                           obdDtcDescription(DTC_DPF_COMM_LOST));
  TEST_ASSERT_EQUAL_STRING("RPM interrupt core-affinity/init failure",
                           obdDtcDescription(DTC_RPM_IRQ_INIT_FAIL));
  TEST_ASSERT_EQUAL_STRING("Glow Plug/Heater Circuit \"A\" Malfunction",
                           obdDtcDescription((uint16_t)OBD_DTC_P0380));
  TEST_ASSERT_EQUAL_STRING(
      "Turbocharger/Supercharger \"A\" Underboost Condition",
      obdDtcDescription((uint16_t)OBD_DTC_P0299));
}

void test_unknown_codes_have_no_description(void) {
  TEST_ASSERT_NULL(obdDtcDescription(0x0000U));
  TEST_ASSERT_NULL(obdDtcDescription(0xFFFFU));
  TEST_ASSERT_NULL(obdDtcDescription(0xD901U)); // a gap in the project range
  TEST_ASSERT_NULL(obdDtcDescription(0x4000U)); // chassis range, not listed
}

void test_format_code_prints_letter_and_hex_digits(void) {
  struct Case {
    uint16_t code;
    const char *text;
  };
  const Case cases[] = {
      {0x0562U, "P0562"}, {0x062FU, "P062F"}, {0x2A05U, "P2A05"},
      {0x3493U, "P3493"}, {0x4123U, "C0123"}, {0x8000U, "B0000"},
      {0xC073U, "U0073"}, {0xD90CU, "U190C"}, {0xFFFFU, "U3FFF"},
  };
  for (const Case &c : cases) {
    char buffer[OBD_DTC_TEXT_SIZE];
    TEST_ASSERT_EQUAL(HAL_OK, obdDtcFormatCode(c.code, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING(c.text, buffer);
  }
}

void test_format_code_rejects_missing_or_short_buffer(void) {
  char buffer[OBD_DTC_TEXT_SIZE];
  memset(buffer, 'x', sizeof(buffer));

  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    obdDtcFormatCode(0x0562U, NULL, sizeof(buffer)));
  TEST_ASSERT_EQUAL(HAL_EINVAL,
                    obdDtcFormatCode(0x0562U, buffer, OBD_DTC_TEXT_SIZE - 1U));
  for (char c : buffer) {
    TEST_ASSERT_EQUAL_CHAR('x', c);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_catalog_ascends_without_duplicates);
  RUN_TEST(test_project_aliases_keep_their_wire_values);
  RUN_TEST(test_every_project_code_has_a_description);
  RUN_TEST(test_descriptions_are_the_catalogue_texts);
  RUN_TEST(test_unknown_codes_have_no_description);
  RUN_TEST(test_format_code_prints_letter_and_hex_digits);
  RUN_TEST(test_format_code_rejects_missing_or_short_buffer);
  return UNITY_END();
}

// The bench application in logic.cpp, driven through its HAL app-entry
// points on the host mock. The cases form one boot scenario and share the
// application state, so their order in main() is the scenario.
#include "hal/impl/.mock/hal_mock.h"
#include "logic.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_app_start_renders_the_whole_screen(void) {
  hal_mock_set_millis(0);
  app_start();
  // The demand-pot line is rendered last; the ADC reads 0 before injection.
  TEST_ASSERT_EQUAL_STRING("0 %", hal_mock_display_last_print());
}

void test_first_task1_pass_starts_the_pump_once(void) {
  const uint32_t before = hal_millis();
  app_task1();
  // No Adjustometer on the bus: the start waits the full baseline window.
  TEST_ASSERT_EQUAL_INT(VP37_INIT_BASELINE_NOT_READY, benchLogicStartStatus());
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(VP37_ADJUSTOMETER_BASELINE_WAIT_MS,
                                      hal_millis() - before);

  // The next pass runs the control step only: no second baseline wait.
  const uint32_t started = hal_millis();
  app_task1();
  TEST_ASSERT_EQUAL_INT(VP37_INIT_BASELINE_NOT_READY, benchLogicStartStatus());
  TEST_ASSERT_LESS_THAN_UINT32(100U, hal_millis() - started);
}

void test_adjustometer_connection_follows_the_bus(void) {
  // A failing transfer means no Adjustometer. The mock signals an absent
  // device through a busy bus; the mock bus itself answers every read.
  hal_mock_i2c_set_busy(true);
  hal_delay_ms(1000U);
  app_task1();
  TEST_ASSERT_FALSE(benchLogicAdjConnected());

  // The bus answering again marks the Adjustometer connected.
  hal_mock_i2c_set_busy(false);
  hal_delay_ms(1000U);
  app_task1();
  TEST_ASSERT_TRUE(benchLogicAdjConnected());
}

void test_display_names_the_start_status(void) {
  // The first tick after begin() only arms a SmartTimer; the display fires
  // on the next one. The only status line that changed since app_start is
  // the start status ("no output" before the start, "no baseline" after),
  // so the tick redraws it and leaves every unchanged line alone.
  app_task0();
  hal_delay_ms(250U);
  app_task0();
  TEST_ASSERT_EQUAL_STRING("no baseline", hal_mock_display_last_print());
}

void test_display_follows_the_demand_pot(void) {
  hal_mock_adc_inject(VP37_SCAN_AUX_ADC_PIN, 2048);
  hal_delay_ms(250U);
  app_task0();
  TEST_ASSERT_EQUAL_STRING("50 %", hal_mock_display_last_print());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_app_start_renders_the_whole_screen);
  RUN_TEST(test_first_task1_pass_starts_the_pump_once);
  RUN_TEST(test_adjustometer_connection_follows_the_bus);
  RUN_TEST(test_display_names_the_start_status);
  RUN_TEST(test_display_follows_the_demand_pot);
  return UNITY_END();
}

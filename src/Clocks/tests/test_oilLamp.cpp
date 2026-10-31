#include "clocksState.h"
#include "hal/impl/.mock/hal_mock.h"
#include "oilLamp.h"
#include "unity.h"

static OilLamp lamp;
static uint32_t s_now = 0u;

static bool sample(bool senderFault, bool valid, float bar) {
  OilLampInput input;
  input.nowMs = s_now;
  input.senderFault = senderFault;
  input.valid = valid;
  input.bar = bar;
  return lamp.process(input);
}

void setUp(void) {
  lamp.reset();
  s_now = 1000u;
}

void tearDown(void) {}

void test_lit_before_the_first_reading(void) {
  for (uint32_t ms = 0u; ms < 4u * OIL_LAMP_BLINK_HALF_MS; ms++) {
    TEST_ASSERT_TRUE(sample(false, false, 0.0f));
    s_now++;
  }
}

void test_lit_without_pressure_and_out_with_it(void) {
  TEST_ASSERT_TRUE(sample(false, true, 0.0f));
  TEST_ASSERT_TRUE(sample(false, true, (float)MIN_OIL_PRESSURE - 0.01f));
  TEST_ASSERT_FALSE(sample(false, true, (float)MIN_OIL_PRESSURE + 0.01f));
  TEST_ASSERT_FALSE(sample(false, true, 4.0f));
}

// A faulty sender used to light the lamp as if there were no pressure.
void test_faulty_sender_blinks_at_1hz(void) {
  for (int period = 0; period < 4; period++) {
    for (uint32_t ms = 0u; ms < 2u * OIL_LAMP_BLINK_HALF_MS; ms++) {
      TEST_ASSERT_EQUAL(ms < OIL_LAMP_BLINK_HALF_MS, sample(true, false, 0.0f));
      s_now++;
    }
  }
}

void test_lamp_follows_the_pressure_once_the_fault_ends(void) {
  (void)sample(true, false, 0.0f);
  s_now += OIL_LAMP_BLINK_HALF_MS; // dark half
  TEST_ASSERT_FALSE(sample(true, false, 0.0f));
  TEST_ASSERT_FALSE(sample(false, true, 3.0f));
  TEST_ASSERT_TRUE(sample(false, true, 0.0f));
}

void test_next_fault_blinks_from_its_start(void) {
  (void)sample(true, false, 0.0f);
  s_now += OIL_LAMP_BLINK_HALF_MS;
  TEST_ASSERT_FALSE(sample(true, false, 0.0f));
  s_now++;
  TEST_ASSERT_FALSE(sample(false, true, 3.0f));
  s_now++;
  TEST_ASSERT_TRUE(sample(true, false, 0.0f));
}

// GP11 follows the measurements core 1 publishes: HIGH lights the lamp.
void test_output_follows_the_published_measurements(void) {
  hal_mock_set_millis(s_now);
  clocksStateInit();
  initOilLamp();
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(JH_HW_NODE_OIL_LAMP_PIN_OUT));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(JH_HW_NODE_OIL_LAMP_PIN_OUT));

  clocks_local_snapshot_t local = {};
  local.oilValid = true;
  local.oilBar = 3.0f;
  local.oilUpdatedMs = s_now;
  clocksStatePublishLocal(&local);
  updateOilLamp();
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(JH_HW_NODE_OIL_LAMP_PIN_OUT));

  local.oilValid = false;
  local.oilBar = 0.0f;
  local.oilSenderFault = true;
  clocksStatePublishLocal(&local);
  updateOilLamp();
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(JH_HW_NODE_OIL_LAMP_PIN_OUT));
  hal_mock_set_millis(s_now + OIL_LAMP_BLINK_HALF_MS);
  updateOilLamp();
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(JH_HW_NODE_OIL_LAMP_PIN_OUT));
  hal_mock_set_millis(s_now + 2u * OIL_LAMP_BLINK_HALF_MS);
  updateOilLamp();
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(JH_HW_NODE_OIL_LAMP_PIN_OUT));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_lit_before_the_first_reading);
  RUN_TEST(test_lit_without_pressure_and_out_with_it);
  RUN_TEST(test_faulty_sender_blinks_at_1hz);
  RUN_TEST(test_lamp_follows_the_pressure_once_the_fault_ends);
  RUN_TEST(test_next_fault_blinks_from_its_start);
  RUN_TEST(test_output_follows_the_published_measurements);
  return UNITY_END();
}

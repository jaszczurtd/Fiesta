#include "can.h"
#include "chargeLamp.h"
#include "hal/impl/.mock/hal_mock.h"
#include "unity.h"

// The ECU values and their freshness as can.cpp would report them.
static float s_rpm = 0.0f;
static float s_volts = 0.0f;
static bool s_rpmFresh = false;
static bool s_voltsFresh = false;

float canGetValue(int idx) {
  return (idx == F_RPM) ? s_rpm : (idx == F_VOLTS) ? s_volts : 0.0f;
}

bool canValueFresh(int idx, uint32_t, uint32_t maxAgeMs) {
  TEST_ASSERT_EQUAL_UINT32(CHARGE_LAMP_DATA_TIMEOUT_MS, maxAgeMs);
  return (idx == F_RPM) ? s_rpmFresh : (idx == F_VOLTS) ? s_voltsFresh : false;
}

static ChargeLamp lamp;
static uint32_t s_now = 0u;

static const int kRunning = RPM_MIN;
static const int32_t kCharging = CHARGE_LAMP_CHARGING_MV + 1;
static const int32_t kLow = CHARGE_LAMP_LOW_MV - 1;
static const int32_t kOver = CHARGE_LAMP_OVERVOLTAGE_MV + 1;

static bool sample(int rpm, int32_t mv) {
  ChargeLampInput input;
  input.nowMs = s_now;
  input.rpmFresh = true;
  input.voltsFresh = true;
  input.rpm = rpm;
  input.millivolts = mv;
  return lamp.process(input);
}

// Keep the inputs for @p ms, one sample per millisecond; returns the state of
// the last sample.
static bool hold(int rpm, int32_t mv, uint32_t ms) {
  bool lit = sample(rpm, mv);
  for (uint32_t i = 0u; i < ms; i++) {
    s_now++;
    lit = sample(rpm, mv);
  }
  return lit;
}

// Start the engine and let the voltage settle at the start; the next sample
// is the first one judged.
static void startEngine(void) {
  TEST_ASSERT_TRUE(sample(0, kCharging));
  s_now++;
  for (uint32_t ms = 0u; ms < CHARGE_LAMP_START_SETTLE_MS; ms++) {
    TEST_ASSERT_TRUE(sample(kRunning, kCharging));
    s_now++;
  }
}

// Start the engine and confirm charging: the lamp goes out.
static void startCharging(void) {
  startEngine();
  TEST_ASSERT_FALSE(hold(kRunning, kCharging, CHARGE_LAMP_CONFIRM_MS));
}

void setUp(void) {
  lamp.reset();
  s_now = 1000u;
}

void tearDown(void) {}

void test_lit_until_both_values_are_fresh(void) {
  ChargeLampInput input = {s_now, false, true, kRunning, kCharging};
  for (uint32_t i = 0u; i < 2u * CHARGE_LAMP_START_SETTLE_MS; i++) {
    input.nowMs = s_now + i;
    input.rpmFresh = false;
    input.voltsFresh = true;
    TEST_ASSERT_TRUE(lamp.process(input));
    input.rpmFresh = true;
    input.voltsFresh = false;
    TEST_ASSERT_TRUE(lamp.process(input));
  }
}

void test_low_rpm_keeps_the_lamp_lit_at_any_voltage(void) {
  TEST_ASSERT_TRUE(hold(RPM_MIN - 1, kCharging, 10000u));
  TEST_ASSERT_TRUE(hold(RPM_MIN - 1, kOver, 10000u));
  TEST_ASSERT_TRUE(hold(0, kLow, 10000u));
}

void test_voltage_is_not_judged_right_after_the_start(void) {
  startEngine();
  // Charging counts from the end of the start time.
  TEST_ASSERT_TRUE(hold(kRunning, kCharging, CHARGE_LAMP_CONFIRM_MS - 1u));
  s_now++;
  TEST_ASSERT_FALSE(sample(kRunning, kCharging));
}

void test_a_break_restarts_the_charging_confirmation(void) {
  startEngine();
  TEST_ASSERT_TRUE(hold(kRunning, kCharging, CHARGE_LAMP_CONFIRM_MS - 1u));
  s_now++;
  TEST_ASSERT_TRUE(sample(kRunning, CHARGE_LAMP_CHARGING_MV));
  s_now++;
  TEST_ASSERT_TRUE(hold(kRunning, kCharging, CHARGE_LAMP_CONFIRM_MS - 1u));
  s_now++;
  TEST_ASSERT_FALSE(sample(kRunning, kCharging));
}

void test_lost_charging_lights_the_lamp_after_confirmation(void) {
  startCharging();
  s_now++;
  TEST_ASSERT_FALSE(hold(kRunning, kLow, CHARGE_LAMP_CONFIRM_MS - 1u));
  s_now++;
  TEST_ASSERT_TRUE(sample(kRunning, kLow));
  // It stays lit while the voltage stays low with the engine running.
  TEST_ASSERT_TRUE(hold(kRunning, kLow, 10000u));
}

void test_short_dip_keeps_the_lamp_out(void) {
  startCharging();
  s_now++;
  TEST_ASSERT_FALSE(hold(kRunning, kLow, CHARGE_LAMP_CONFIRM_MS - 1u));
  s_now++;
  TEST_ASSERT_FALSE(sample(kRunning, CHARGE_LAMP_LOW_MV));
  s_now++;
  TEST_ASSERT_FALSE(hold(kRunning, kLow, CHARGE_LAMP_CONFIRM_MS - 1u));
}

void test_hysteresis_band_and_its_limits_keep_the_state(void) {
  const int32_t band[] = {CHARGE_LAMP_LOW_MV,
                          (CHARGE_LAMP_LOW_MV + CHARGE_LAMP_CHARGING_MV) / 2,
                          CHARGE_LAMP_CHARGING_MV};
  for (int32_t mv : band) {
    lamp.reset();
    startEngine();
    TEST_ASSERT_TRUE(hold(kRunning, mv, 10000u));

    lamp.reset();
    startCharging();
    s_now++;
    TEST_ASSERT_FALSE(hold(kRunning, mv, 10000u));
  }
}

void test_overvoltage_blinks_at_2hz_after_confirmation(void) {
  startCharging();
  s_now++;
  TEST_ASSERT_FALSE(
      hold(kRunning, kOver, CHARGE_LAMP_OVERVOLTAGE_CONFIRM_MS - 1u));
  s_now++;
  for (int period = 0; period < 4; period++) {
    for (uint32_t ms = 0u; ms < 2u * CHARGE_LAMP_BLINK_HALF_MS; ms++) {
      TEST_ASSERT_EQUAL(ms < CHARGE_LAMP_BLINK_HALF_MS,
                        sample(kRunning, kOver));
      s_now++;
    }
  }
}

void test_overvoltage_limit_itself_is_no_alarm(void) {
  startCharging();
  s_now++;
  TEST_ASSERT_FALSE(hold(kRunning, CHARGE_LAMP_OVERVOLTAGE_MV, 10000u));
}

void test_lamp_shows_charging_again_when_overvoltage_ends(void) {
  startCharging();
  s_now++;
  (void)hold(kRunning, kOver, CHARGE_LAMP_OVERVOLTAGE_CONFIRM_MS + 100u);
  s_now++;
  // No blinking in the hysteresis band either.
  TEST_ASSERT_FALSE(hold(kRunning, CHARGE_LAMP_CHARGING_MV, 2000u));
  s_now++;
  TEST_ASSERT_FALSE(sample(kRunning, kCharging));
}

void test_overvoltage_from_the_start_confirms_charging_too(void) {
  startEngine();
  const uint32_t longer =
      (CHARGE_LAMP_CONFIRM_MS > CHARGE_LAMP_OVERVOLTAGE_CONFIRM_MS)
          ? CHARGE_LAMP_CONFIRM_MS
          : CHARGE_LAMP_OVERVOLTAGE_CONFIRM_MS;
  (void)hold(kRunning, kOver, longer + 100u);
  s_now++;
  TEST_ASSERT_FALSE(sample(kRunning, kCharging));
}

void test_low_rpm_wins_over_overvoltage(void) {
  startCharging();
  s_now++;
  (void)hold(kRunning, kOver, CHARGE_LAMP_OVERVOLTAGE_CONFIRM_MS + 100u);
  s_now++;
  TEST_ASSERT_TRUE(hold(RPM_MIN - 1, kOver, 2u * CHARGE_LAMP_BLINK_HALF_MS));
}

void test_stopped_engine_must_confirm_charging_again(void) {
  startCharging();
  s_now++;
  TEST_ASSERT_TRUE(sample(0, kCharging));
  startCharging();
}

void test_lost_data_light_the_lamp_and_restart_the_confirmation(void) {
  startCharging();
  s_now++;
  ChargeLampInput input = {s_now, true, false, kRunning, kCharging};
  TEST_ASSERT_TRUE(lamp.process(input));
  s_now++;
  // Back with the engine running: start time and confirmation once more.
  TEST_ASSERT_TRUE(
      hold(kRunning, kCharging,
           CHARGE_LAMP_START_SETTLE_MS + CHARGE_LAMP_CONFIRM_MS - 1u));
  s_now++;
  TEST_ASSERT_FALSE(sample(kRunning, kCharging));
}

// GP26 follows the ECU values: HIGH lights the lamp.
void test_output_is_lit_from_init_and_follows_the_ecu_values(void) {
  hal_gpio_write(JH_HW_NODE_CHARGE_LAMP_PIN_OUT, false);
  hal_mock_set_millis(s_now);
  initChargeLamp();
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(JH_HW_NODE_CHARGE_LAMP_PIN_OUT));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(JH_HW_NODE_CHARGE_LAMP_PIN_OUT));

  s_rpm = (float)kRunning;
  s_volts = (float)kCharging / 1000.0f;
  s_rpmFresh = s_voltsFresh = true;
  const uint32_t untilOut =
      CHARGE_LAMP_START_SETTLE_MS + CHARGE_LAMP_CONFIRM_MS;
  for (uint32_t ms = 0u; ms <= untilOut; ms++) {
    hal_mock_set_millis(s_now + ms);
    updateChargeLamp();
    TEST_ASSERT_EQUAL(ms < untilOut,
                      hal_mock_gpio_get_state(JH_HW_NODE_CHARGE_LAMP_PIN_OUT));
  }

  s_voltsFresh = false;
  updateChargeLamp();
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(JH_HW_NODE_CHARGE_LAMP_PIN_OUT));
}

// Volts decoded from the ECU tenths at the low limit are the limit itself,
// inside the band: charging stays.
void test_ecu_volts_at_the_low_limit_keep_charging(void) {
  hal_mock_set_millis(s_now);
  initChargeLamp();
  s_rpm = (float)kRunning;
  s_volts = (float)kCharging / 1000.0f;
  s_rpmFresh = s_voltsFresh = true;
  uint32_t ms = 0u;
  for (; ms <= CHARGE_LAMP_START_SETTLE_MS + CHARGE_LAMP_CONFIRM_MS; ms++) {
    hal_mock_set_millis(s_now + ms);
    updateChargeLamp();
  }
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(JH_HW_NODE_CHARGE_LAMP_PIN_OUT));

  s_volts = fiesta_can_join_decimal_tenths(CHARGE_LAMP_LOW_MV / 1000,
                                           (CHARGE_LAMP_LOW_MV % 1000) / 100);
  const uint32_t until = ms + 2u * CHARGE_LAMP_CONFIRM_MS;
  for (; ms <= until; ms++) {
    hal_mock_set_millis(s_now + ms);
    updateChargeLamp();
  }
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(JH_HW_NODE_CHARGE_LAMP_PIN_OUT));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_lit_until_both_values_are_fresh);
  RUN_TEST(test_low_rpm_keeps_the_lamp_lit_at_any_voltage);
  RUN_TEST(test_voltage_is_not_judged_right_after_the_start);
  RUN_TEST(test_a_break_restarts_the_charging_confirmation);
  RUN_TEST(test_lost_charging_lights_the_lamp_after_confirmation);
  RUN_TEST(test_short_dip_keeps_the_lamp_out);
  RUN_TEST(test_hysteresis_band_and_its_limits_keep_the_state);
  RUN_TEST(test_overvoltage_blinks_at_2hz_after_confirmation);
  RUN_TEST(test_overvoltage_limit_itself_is_no_alarm);
  RUN_TEST(test_lamp_shows_charging_again_when_overvoltage_ends);
  RUN_TEST(test_overvoltage_from_the_start_confirms_charging_too);
  RUN_TEST(test_low_rpm_wins_over_overvoltage);
  RUN_TEST(test_stopped_engine_must_confirm_charging_again);
  RUN_TEST(test_lost_data_light_the_lamp_and_restart_the_confirmation);
  RUN_TEST(test_output_is_lit_from_init_and_follows_the_ecu_values);
  RUN_TEST(test_ecu_volts_at_the_low_limit_keep_charging);
  return UNITY_END();
}

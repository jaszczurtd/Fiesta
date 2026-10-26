#include "engineFuel.h"
#include "hal/impl/.mock/hal_mock.h"
#include "unity.h"

#include <cstring>

// engineFuel.cpp reads the display view of clocksState.cpp and the alert
// blinkers of logic.cpp; the bar tests below need no blinking.
bool alertSwitch(void) { return false; }
bool seriousAlertSwitch(void) { return false; }

static EngineFuelGauge gauge;

// The display reports a rejected rectangle through hal_derr(), which the
// mock serial port keeps as its last line.
static bool display_refused_a_rectangle(void) {
  return strstr(hal_mock_serial_last_line(), "invalid size") != NULL;
}

void setUp(void) {
  hal_mock_display_reset();
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_display_configure_ex(SCREEN_W, SCREEN_H, 0, false, false));
  gauge.init();
  hal_mock_serial_reset();
}

void tearDown(void) {}

void test_full_tank_draws_no_empty_remainder(void) {
  const int full = gauge.getWidth() - 1;
  gauge.drawChangeableFuelContent(full, FUEL_GAUGE_HEIGHT, gauge.getGaugePos());

  TEST_ASSERT_FALSE(display_refused_a_rectangle());
  int x, y, w, h;
  uint16_t color;
  hal_mock_display_get_last_fill_rect(&x, &y, &w, &h, &color);
  TEST_ASSERT_EQUAL_INT(full, w);
  TEST_ASSERT_EQUAL_UINT16(FUEL_FILL_COLOR, color);
}

void test_empty_tank_draws_no_fuel_bar(void) {
  gauge.drawChangeableFuelContent(1, FUEL_GAUGE_HEIGHT, gauge.getGaugePos());
  hal_mock_serial_reset();

  gauge.drawChangeableFuelContent(0, FUEL_GAUGE_HEIGHT, gauge.getGaugePos());

  TEST_ASSERT_FALSE(display_refused_a_rectangle());
  int x, y, w, h;
  uint16_t color;
  hal_mock_display_get_last_fill_rect(&x, &y, &w, &h, &color);
  TEST_ASSERT_EQUAL_INT(gauge.getWidth() - 1, w);
  TEST_ASSERT_EQUAL_UINT16(HAL_COLOR(BLACK), color);
}

void test_partial_tank_draws_both_bars(void) {
  const int half = gauge.getWidth() / 2;
  gauge.drawChangeableFuelContent(half, FUEL_GAUGE_HEIGHT, gauge.getGaugePos());

  TEST_ASSERT_FALSE(display_refused_a_rectangle());
  int x, y, w, h;
  uint16_t color;
  hal_mock_display_get_last_fill_rect(&x, &y, &w, &h, &color);
  TEST_ASSERT_EQUAL_INT(gauge.getBaseX() + half, x);
  TEST_ASSERT_EQUAL_INT(gauge.getWidth() - half - 1, w);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_full_tank_draws_no_empty_remainder);
  RUN_TEST(test_empty_tank_draws_no_fuel_bar);
  RUN_TEST(test_partial_tank_draws_both_bars);
  return UNITY_END();
}

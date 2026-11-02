#include "clocksState.h"
#include "hal/impl/.mock/hal_mock.h"
#include "simpleGauge.h"
#include "unity.h"

// simpleGauge.cpp reads the display view of clocksState.cpp, ERR and the
// alert blinkers of logic.cpp and the throttle of peripherials.cpp.
const char *err = "ERR";
static bool s_seriousAlert = false;
bool alertSwitch(void) { return false; }
bool seriousAlertSwitch(void) { return s_seriousAlert; }
int getThrottlePercentage(void) { return 0; }

extern SimpleGauge rpm_g;
extern SimpleGauge gps_g;
extern SimpleGauge ecu_g;

static void publishEcu(bool connected, int rpm, bool rpmValid = true) {
  clocks_ecu_snapshot_t ecu = {};
  ecu.ecuConnected = connected;
  ecu.values[F_RPM] = (float)rpm;
  ecu.rpmValid = rpmValid;
  clocksStatePublishEcu(&ecu);
  (void)clocksStateRefreshView();
}

// Draw @p gauge, cover its texts, redraw it and draw it once more.
static void drawCoverAndRedraw(SimpleGauge &gauge) {
  gauge.redraw();
  gauge.showSimpleGauge();
  hal_display_print("covered");
  gauge.redraw();
  gauge.showSimpleGauge();
}

void setUp(void) {
  hal_mock_display_reset();
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_display_configure_ex(SCREEN_W, SCREEN_H, 0, false, false));
  clocksStateInit();
  s_seriousAlert = false;
}

void tearDown(void) {}

// The artifact: a full redraw covered N/A with the GPS icon, and N/A did not
// change to be drawn again.
void test_redraw_draws_gps_na_again(void) {
  publishEcu(true, 0);
  TEST_ASSERT_FALSE(isGPSLinked());
  drawCoverAndRedraw(gps_g);
  TEST_ASSERT_EQUAL_STRING("N/A", hal_mock_display_last_print());
}

void test_redraw_draws_an_unchanged_value_again(void) {
  publishEcu(true, 800);
  drawCoverAndRedraw(rpm_g);
  TEST_ASSERT_EQUAL_STRING("800", hal_mock_display_last_print());
}

void test_rpm_loss_and_recovery_redraw_without_periodic_tick(void) {
  publishEcu(true, 922);
  showRpmGauge();
  TEST_ASSERT_EQUAL_STRING("920", hal_mock_display_last_print());
  publishEcu(true, 0, false);
  showRpmGauge();
  TEST_ASSERT_EQUAL_STRING("N/A", hal_mock_display_last_print());
  drawCoverAndRedraw(rpm_g);
  TEST_ASSERT_EQUAL_STRING("N/A", hal_mock_display_last_print());
  publishEcu(true, 922);
  showRpmGauge();
  TEST_ASSERT_EQUAL_STRING("920", hal_mock_display_last_print());
}

void test_zero_rpm_and_no_rpm_have_different_readouts(void) {
  publishEcu(true, 0);
  showRpmGauge();
  TEST_ASSERT_EQUAL_STRING("0", hal_mock_display_last_print());
  publishEcu(true, 0, false);
  showRpmGauge();
  TEST_ASSERT_EQUAL_STRING("N/A", hal_mock_display_last_print());
  publishEcu(true, 0);
  showRpmGauge();
  TEST_ASSERT_EQUAL_STRING("0", hal_mock_display_last_print());
}

// A redraw in the red half of the alert blinker left a red ECU dot while the
// ECU was connected.
void test_redraw_leaves_no_ecu_dot_while_connected(void) {
  publishEcu(true, 0);
  ecu_g.redraw();
  ecu_g.showSimpleGauge();
  s_seriousAlert = true;
  ecu_g.redraw();
  hal_display_fill_rect(0, 0, 1, 1, HAL_COLOR(RED));
  ecu_g.showSimpleGauge();

  int x, y, w, h;
  uint16_t color;
  hal_mock_display_get_last_fill_rect(&x, &y, &w, &h, &color);
  const int cx = ecu_g.getBaseX();
  const int cy = ecu_g.getBaseY();
  TEST_ASSERT_EQUAL_HEX16(ICONS_BG_COLOR, color);
  TEST_ASSERT_TRUE(x <= cx - ECU_CONNECTION_RADIUS &&
                   y <= cy - ECU_CONNECTION_RADIUS &&
                   x + w >= cx + ECU_CONNECTION_RADIUS &&
                   y + h >= cy + ECU_CONNECTION_RADIUS);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_redraw_draws_gps_na_again);
  RUN_TEST(test_redraw_draws_an_unchanged_value_again);
  RUN_TEST(test_rpm_loss_and_recovery_redraw_without_periodic_tick);
  RUN_TEST(test_zero_rpm_and_no_rpm_have_different_readouts);
  RUN_TEST(test_redraw_leaves_no_ecu_dot_while_connected);
  return UNITY_END();
}

#include "hal/display/drivers/Fonts/FreeSansBold9pt7b.h"
#include "hal/impl/.mock/hal_mock.h"
#include "pressureGauge.h"
#include "unity.h"

// pressureGauge.cpp reads the display view of clocksState.cpp, ERR and the
// alert blinker of logic.cpp.
const char *err = "ERR";
bool seriousAlertSwitch(void) { return false; }

extern PressureGauge oilp_g;

// Right edge of @p text in the bold font, as the panel draws it; the mock
// display counts every font as 6 px per character.
static int boldWidth(const char *text) {
  int x = 0;
  int right = 0;
  for (const char *c = text; *c != '\0'; c++) {
    const GFXglyph &glyph =
        FreeSansBold9pt7bGlyphs[(uint8_t)*c - FreeSansBold9pt7b.first];
    const int edge = x + glyph.xOffset + glyph.width;
    right = (edge > right) ? edge : right;
    x += glyph.xAdvance;
  }
  return right;
}

static const int kDefaultCharWidth = 6; // 5x7 glyph in a 6x8 cell
static const int kDefaultCharHeight = 8;
static const int kBoldAscent = 12; // rows above the baseline

static void showOil(bool valid, float bar) {
  clocks_local_snapshot_t local = {};
  local.oilValid = valid;
  local.oilBar = bar;
  local.oilUpdatedMs = hal_millis();
  clocksStatePublishLocal(&local);
  oilp_g.showPressureGauge();
}

static void cursor(int *x, int *y) { hal_mock_display_get_cursor(x, y); }

static bool clearedArea(int left, int top, int right, int bottom) {
  int x, y, w, h;
  uint16_t color;
  hal_mock_display_get_last_fill_rect(&x, &y, &w, &h, &color);
  return color == ICONS_BG_COLOR && x <= left && y <= top && x + w >= right &&
         y + h >= bottom;
}

void setUp(void) {
  hal_mock_display_reset();
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_display_configure_ex(SCREEN_W, SCREEN_H, 0, false, false));
  clocksStateInit();
  oilp_g.redraw();
  showOil(true, 0.0f); // the icon
}

void tearDown(void) {}

void test_faulty_sender_shows_a_red_err_without_the_unit(void) {
  showOil(false, 0.0f);
  TEST_ASSERT_EQUAL_STRING(err, hal_mock_display_last_println());
  TEST_ASSERT_EQUAL_HEX16(HAL_COLOR(RED), hal_mock_display_get_text_color());
}

// The artifact: the value after ERR cleared only its own width, so the end
// of ERR stayed next to the digits, over the unit label.
void test_value_after_err_clears_all_of_err_and_the_unit(void) {
  showOil(false, 0.0f);
  int errX, errY;
  cursor(&errX, &errY);

  showOil(true, 1.6f);
  TEST_ASSERT_EQUAL_STRING("BAR", hal_mock_display_last_println());
  int unitX, unitY;
  cursor(&unitX, &unitY);
  TEST_ASSERT_TRUE(
      clearedArea(errX, errY - kBoldAscent, errX + boldWidth(err), errY + 1));
  TEST_ASSERT_TRUE(clearedArea(unitX, unitY, unitX + 3 * kDefaultCharWidth,
                               unitY + kDefaultCharHeight));
}

void test_widest_value_is_cleared_by_the_next_one(void) {
  showOil(false, 0.0f);
  int valueX, valueY;
  cursor(&valueX, &valueY); // ERR starts where the value does
  showOil(true, 10.0f);
  showOil(true, 2.0f);
  TEST_ASSERT_TRUE(clearedArea(valueX, valueY - kBoldAscent,
                               valueX + boldWidth("10.0"), valueY + 1));
}

void test_redraw_draws_an_unchanged_value_again(void) {
  showOil(true, 3.4f);
  hal_display_println("covered");
  oilp_g.redraw();
  showOil(true, 3.4f); // the icon, over the old texts
  showOil(true, 3.4f);
  TEST_ASSERT_EQUAL_STRING("BAR", hal_mock_display_last_println());
}

void test_redraw_draws_err_again(void) {
  showOil(false, 0.0f);
  hal_display_println("covered");
  oilp_g.redraw();
  showOil(false, 0.0f);
  showOil(false, 0.0f);
  TEST_ASSERT_EQUAL_STRING(err, hal_mock_display_last_println());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_faulty_sender_shows_a_red_err_without_the_unit);
  RUN_TEST(test_value_after_err_clears_all_of_err_and_the_unit);
  RUN_TEST(test_widest_value_is_cleared_by_the_next_one);
  RUN_TEST(test_redraw_draws_an_unchanged_value_again);
  RUN_TEST(test_redraw_draws_err_again);
  return UNITY_END();
}

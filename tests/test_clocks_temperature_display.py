"""Check Clocks temperature transitions with the production GFX font renderer."""

from pathlib import Path
import tempfile
import unittest

from native_build import run_clocks_harness

ROOT = Path(__file__).resolve().parents[1]
HAL = ROOT / "src/JaszczurHAL"
MODULE = ROOT / "src/Clocks"


class ClocksTemperatureDisplayTests(unittest.TestCase):
    def test_error_to_temperature_leaves_no_pixels(self):
        with tempfile.TemporaryDirectory(prefix="clocks-temperature-") as temp:
            build = Path(temp)
            harness = build / "display.cpp"
            harness.write_text(r'''
#include "tempGauge.h"
#include "hal/display/drivers/jh_gfx.h"
#include "hal/display/drivers/Fonts/FreeSansBold9pt7b.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static JHGfxCanvas16 actual(SCREEN_W, SCREEN_H);
static JHGfxCanvas16 expected(SCREEN_W, SCREEN_H);
const char *err = "ERR";

// Only the display surface is replaced; glyph rasterization stays in HAL GFX.
bool hal_display_set_sans_bold_with_pos_and_color(int x, int y, uint16_t color) {
  actual.setFont(&FreeSansBold9pt7b);
  actual.setCursor(x, y);
  actual.setTextSize(1);
  actual.setTextColor(color);
  return true;
}
bool hal_display_fill_rect(int x, int y, int w, int h, uint16_t color) {
  actual.fillRect(x, y, w, h, color);
  return true;
}
bool hal_display_set_text_color(uint16_t color) {
  actual.setTextColor(color);
  return true;
}
bool hal_display_println(const char *text) {
  actual.print(text);
  actual.print("\n");
  return true;
}
bool hal_display_println_prepared_text(char *text) {
  return hal_display_println(text);
}
int hal_display_prepare_text(char *text, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int length = vsnprintf(text, size, format, args);
  va_end(args);
  return length;
}
bool hal_display_set_default_font(void) {
  actual.setFont(NULL);
  actual.setTextSize(1);
  return true;
}

static void prepare_scene(JHGfxCanvas16 &canvas, int baseX, int neighborX) {
  canvas.fillScreen(HAL_COLOR(GREEN));
  canvas.fillRect(baseX, 0, neighborX - baseX, SCREEN_H, ICONS_BG_COLOR);
}

int main(void) {
  const int modes[] = {TEMP_GAUGE_COOLANT, TEMP_GAUGE_OIL};
  for (int mode : modes) {
    TempGauge gauge(mode);
    int x = gauge.getBaseX() + (mode == TEMP_GAUGE_COOLANT ? 40 : 36);
    int y = gauge.getBaseY() + 38;
    // The next indicators start at oil X=88 and turbo pressure X=166.
    int neighborX = mode == TEMP_GAUGE_COOLANT ? 88 : 166;
    for (int value = TEMP_LOWEST; value <= TEMP_HIGHEST; ++value) {
      prepare_scene(actual, gauge.getBaseX(), neighborX);
      gauge.drawTempValue(x, y, TEMP_LOWEST - 1);
      gauge.drawTempValue(x, y, value);

      prepare_scene(expected, gauge.getBaseX(), neighborX);
      expected.setFont(&FreeSansBold9pt7b);
      expected.setCursor(x, y);
      expected.setTextColor(TEXT_COLOR);
      char text[DISPLAY_TXT_SIZE];
      snprintf(text, sizeof(text), "%d", value);
      expected.print(text);
      assert(memcmp(actual.getBuffer(), expected.getBuffer(),
                    SCREEN_W * SCREEN_H * sizeof(uint16_t)) == 0);
    }
  }
}
''')
            run_clocks_harness(ROOT, [MODULE / "tempGauge.cpp", harness,
                                      HAL / "src/hal/display/drivers/jh_gfx.cpp"],
                               build)


if __name__ == "__main__":
    unittest.main()

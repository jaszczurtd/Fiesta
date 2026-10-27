"""Check the Clocks GPS and EGT readouts drawn by the production gauge code."""

from pathlib import Path
import tempfile
import unittest

from native_build import run_clocks_harness

ROOT = Path(__file__).resolve().parents[1]
HAL = ROOT / "src/JaszczurHAL"
MODULE = ROOT / "src/Clocks"


class ClocksSimpleGaugeTests(unittest.TestCase):
    def test_gps_link_and_egt_dpf_turns(self):
        with tempfile.TemporaryDirectory(prefix="clocks-simple-gauges-") as temp:
            build = Path(temp)
            harness = build / "gauges.cpp"
            harness.write_text(r'''
#include "simpleGauge.h"
#include "clocksState.h"
#include "peripherials.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>

const char *err = "ERR";
float valueFields[F_LAST];

// Module state seen by the gauges, set by each scenario.
static bool s_fix, s_linked = true, s_alert, s_serious;
static bool s_egtValid[EGT_CHANNELS] = {true, true};
static bool s_dpfConnected, s_dpfRegenerating;
bool isGPSAvailable(void) { return s_fix; }
bool isGPSLinked(void) { return s_linked; }
int getGPSSpeed(void) { return 42; }
bool alertSwitch(void) { return s_alert; }
bool seriousAlertSwitch(void) { return s_serious; }
bool isEgtValid(int channel) { return s_egtValid[channel]; }
bool isDPFConnected(void) { return s_dpfConnected; }
bool isDPFRegenerating(void) { return s_dpfRegenerating; }
bool isEcuConnected(void) { return true; }
int getThrottlePercentage(void) { return 0; }
int getEngineRPM(void) { return 0; }

// Only the display surface is replaced: the last text and the dot colour.
static char s_text[32];
static uint16_t s_dot;
bool hal_display_print_at(int x, int y, const char *s) {
  (void)x;
  (void)y;
  if (strcmp(s, "km/h") != 0) {
    snprintf(s_text, sizeof(s_text), "%s", s);
  }
  return true;
}
bool hal_display_fill_circle(int x, int y, int r, uint16_t color) {
  (void)x;
  (void)y;
  (void)r;
  s_dot = color;
  return true;
}
int hal_display_prepare_text_v(char *text, size_t size, const char *format,
                               va_list args) {
  return vsnprintf(text, size, format, args);
}
int hal_display_prepare_text(char *text, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(text, size, format, args);
  va_end(args);
  return length;
}
int hal_display_text_width(const char *text) { return (int)strlen(text) * 6; }
bool hal_display_draw_circle(int, int, int, uint16_t) { return true; }
bool hal_display_draw_image(int, int, int, int, uint16_t, uint16_t *) {
  return true;
}
bool hal_display_fill_rect(int, int, int, int, uint16_t) { return true; }
bool hal_display_println_prepared_text(char *) { return true; }
bool hal_display_set_default_font(void) { return true; }
bool hal_display_set_sans_bold_with_pos_and_color(int, int, uint16_t) {
  return true;
}
bool hal_display_set_serif9pt_with_color(uint16_t) { return true; }

static void gps(bool alert, bool serious) {
  s_alert = alert;
  s_serious = serious;
  showGPSGauge();
}

static const char *egtText(void) {
  showEGTGauge();
  return s_text;
}

int main(void) {
  // Receiver talks, no fix: the speed is shown, the dot blinks with the
  // one-second alert.
  gps(true, false);
  assert(strcmp(s_text, "42") == 0 && s_dot == HAL_COLOR(RED));
  gps(false, true);
  assert(s_dot == ICONS_BG_COLOR);

  // No communication: N/A instead of the speed, the dot blinks with the
  // twice faster serious alert.
  s_linked = false;
  gps(false, true);
  assert(strcmp(s_text, "N/A") == 0 && s_dot == HAL_COLOR(RED));
  gps(true, false);
  assert(s_dot == ICONS_BG_COLOR);

  // A fix shows green whatever the blink phase; the speed comes back.
  s_linked = true;
  s_fix = true;
  gps(false, false);
  assert(strcmp(s_text, "42") == 0 && s_dot == HAL_COLOR(GREEN));

  // Without a regeneration only EGT is shown, both channels measuring.
  valueFields[F_EGT] = 300.0f;
  valueFields[F_DPF_TEMP] = 500.0f;
  for (int turn = 0; turn < 3; ++turn) {
    changeEGT();
    assert(strcmp(egtText(), "300") == 0);
  }
  // A regeneration reported by a connected module stays only EGT too.
  s_dpfRegenerating = true;
  changeEGT();
  assert(strcmp(egtText(), "300") == 0);
  // The DPF module connected and regenerating: EGT and DPF take turns.
  s_dpfConnected = true;
  changeEGT();
  assert(strcmp(egtText(), "R/500") == 0);
  changeEGT();
  assert(strcmp(egtText(), "300") == 0);
  changeEGT();
  assert(strcmp(egtText(), "R/500") == 0);
  // Regeneration over, or its module silent: back to EGT at the next turn.
  s_dpfRegenerating = false;
  changeEGT();
  assert(strcmp(egtText(), "300") == 0);
  changeEGT();
  assert(strcmp(egtText(), "300") == 0);
  s_dpfRegenerating = true;
  changeEGT();
  assert(strcmp(egtText(), "R/500") == 0);
  s_dpfConnected = false;
  changeEGT();
  assert(strcmp(egtText(), "300") == 0);
  // Only the DPF channel measuring, no regeneration: EGT stays, as N/A.
  s_egtValid[EGT_PRE_DPF] = false;
  changeEGT();
  assert(strcmp(egtText(), "N/A") == 0);
  return 0;
}
''')
            run_clocks_harness(ROOT, [MODULE / "simpleGauge.cpp", harness,
                                      HAL / "src/hal/core/hal_math.cpp"], build)


if __name__ == "__main__":
    unittest.main()

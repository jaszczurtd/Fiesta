// VP37TestBench: core 0 runs the timers and the display, core 1 starts the
// pump through the bench adapter and runs its control step. The demand comes
// from the bench potentiometer; core 0 reads only the published status.

#include "logic.h"

#include <cstdio>
#include <cstring>

static void callAtEverySecond(void);
static void updateDisplay(void);
static void updateValsForDebug(void);
static void updateAdjConnection(void);
static void drawAdjConnectionDot(void);

static unsigned long lastThreadSeconds = 0;

static SmartTimers timerSecond;
static SmartTimers timerDisplay;
static SmartTimers timerDebug;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

// The one pump of the bench, owned by core 1 after its start there.
static VP37Pump s_pump;

// Blink phase for alerts, toggled once a second (the Clocks pattern).
// Core 0 only.
static bool s_alertBlink = false;

// Pump start result and Adjustometer connection: written by core 1, shown
// by core 0. One word each through relaxed HAL_ATOMIC_* - neither word
// publishes any other data, so no ordering is needed (the pump state
// itself crosses the cores through the module's own snapshot).
static uint32_t s_pumpStartStatusWord = (uint32_t)VP37_INIT_OUTPUT_UNAVAILABLE;
static uint32_t s_adjConnectedWord = 0U;

static VP37InitStatus benchStartStatus(void) {
  return (VP37InitStatus)HAL_ATOMIC_LOAD(&s_pumpStartStatusWord,
                                         HAL_ATOMIC_RELAXED);
}

static bool benchAdjConnected(void) {
  return HAL_ATOMIC_LOAD(&s_adjConnectedWord, HAL_ATOMIC_RELAXED) != 0U;
}

// The connection is re-judged once a second, like the CAN device dots on
// Clocks judge their message counters.
#define ADJ_CONNECTION_CHECK_MS 1000U

#ifdef UNIT_TEST
VP37InitStatus benchLogicStartStatus(void) { return benchStartStatus(); }
bool benchLogicAdjConnected(void) { return benchAdjConnected(); }
#endif

static int *wValues = NULL;
static int wSize = 0;
void executeByWatchdog(int *values, int size) {
  wValues = values;
  wSize = size;
}

static void setupTimers(void) {
  watchdog_feed();
  timerSecond.begin(callAtEverySecond, SECOND);
  watchdog_feed();
  timerDisplay.begin(updateDisplay, DISPLAY_UPDATE);
  watchdog_feed();
  timerDebug.begin(updateValsForDebug, DEBUG_UPDATE);
}

void app_start(void) {

  hal_debug_init_default();
  hal_debug_set_module_prefix(SC_MODULE_TOKEN_VP37_TESTBENCH);

  bool rebooted = hal_watchdog_caused_reboot();
  if (!rebooted) {
    statusVariable0 = statusVariable1 = 0;
  }
  setupWatchdog(executeByWatchdog, WATCHDOG_TIME);

  setupOnboardLed();
  initSPI();
  initI2C();
  watchdog_feed();

  hal_display_set_font(HAL_FONT_DEFAULT);
  hal_display_set_text_size(2);
  hal_display_set_cursor(8, 8);
  hal_display_set_text_color(ST77XX_CYAN);
  hal_display_print("VP37 TestBench");

  updateDisplay();
  updateValsForDebug();
  watchdog_feed();
  setupTimers();

  setStartedCore0();
}

void app_task0(void) {

  statusVariable0 = 0;
  updateWatchdogCore0();

  if (!isEnvironmentStarted()) {
    statusVariable0 = -1;
    hal_delay_ms(CORE_OPERATION_DELAY);
    hal_idle();
    return;
  }

  statusVariable0 = 1;

  timerSecond.tick();
  timerDisplay.tick();
  timerDebug.tick();

  if (lastThreadSeconds < hal_get_seconds()) {
    lastThreadSeconds = hal_get_seconds() + THREAD_CONTROL_SECONDS;
    deb("thread is alive");
  }
  statusVariable0 = 2;

  hal_delay_ms(CORE_OPERATION_DELAY);
  hal_idle();
}

void app_task1(void) {
  // HAL app-entry does not expose a per-core setup1 hook. The first pass
  // marks core 1 as started, then takes the pump: the adapter creates the
  // drive outputs and the scan here, because the scan's completion interrupt
  // belongs to the core that consumes its blocks.
  static bool core1Started = false;
  if (!core1Started) {
    setStartedCore1();
    core1Started = true;
  }

  updateWatchdogCore1();

  if (!isEnvironmentStarted()) {
    statusVariable1 = -1;
    hal_delay_ms(CORE_OPERATION_DELAY);
    hal_idle();
    return;
  }

  static bool pumpStarted = false;
  if (!pumpStarted) {
    pumpStarted = true;
    const VP37InitStatus startStatus = vp37BenchAdapterStart(&s_pump);
    HAL_ATOMIC_STORE(&s_pumpStartStatusWord, (uint32_t)startStatus,
                     HAL_ATOMIC_RELAXED);
    deb("VP37 bench pump start: %s (%d)", VP37_initStatusName(startStatus),
        (int)startStatus);
  }
  statusVariable1 = 1;

  if (VP37_isReady(&s_pump)) {
    (void)VP37_setPositionDemandPercentage(&s_pump, vp37BenchDemandPercent());
  }
  VP37_process(&s_pump);
  updateAdjConnection();

  statusVariable1 = 2;
  hal_delay_ms(CORE_OPERATION_DELAY);
  hal_idle();
}

/** @brief The Clocks pattern for a device dot: the frame counter moving
 * since the last check means the Adjustometer talks. With the pump stopped
 * no frames flow, so core 1 probes the status register itself - it is the
 * bench bus's only user, which is why the adapter transfer has no mutex.
 * Runs on core 1, once per ADJ_CONNECTION_CHECK_MS. */
static void updateAdjConnection(void) {
  static uint32_t lastCheckMs = 0;
  static uint32_t lastReadCount = 0;
  const uint32_t now = hal_millis();
  if ((now - lastCheckMs) < ADJ_CONNECTION_CHECK_MS) {
    return;
  }
  lastCheckMs = now;

  bool connected = false;
  VP37Status status;
  if ((VP37_readStatus(&s_pump, &status) == HAL_OK) &&
      (status.readCount != lastReadCount)) {
    lastReadCount = status.readCount;
    connected = true;
  } else {
    uint8_t probe = 0;
    connected = vp37BenchCallbacks()->adjustometerTransfer(
                    ADJUSTOMETER_REG_STATUS, &probe, 1U) == HAL_OK;
  }
  HAL_ATOMIC_STORE(&s_adjConnectedWord, connected ? 1U : 0U,
                   HAL_ATOMIC_RELAXED);
}

// timer functions
static void callAtEverySecond(void) {
  s_alertBlink = !s_alertBlink;
  hal_gpio_write(PIN_ONBOARD_LED, s_alertBlink);
}

#define DISPLAY_LINES 6
#define DISPLAY_VALUE_LEN 24
#define ADJ_CONNECTION_RADIUS 4

/** @brief The Adjustometer dot in the top-right corner, the Clocks device
 * dot look: steady green while connected, blinking red while not. Redrawn
 * only when its colour changes. */
static void drawAdjConnectionDot(void) {
  // Any value outside the dot's palette forces the first draw.
  static uint16_t lastColor = 1;
  const uint16_t color = benchAdjConnected()
                             ? ST77XX_GREEN
                             : (s_alertBlink ? ST77XX_RED : ST77XX_BLACK);
  if (color == lastColor) {
    return;
  }
  lastColor = color;
  hal_display_fill_circle(SCREEN_WIDTH - 10 - ADJ_CONNECTION_RADIUS,
                          10 + ADJ_CONNECTION_RADIUS, ADJ_CONNECTION_RADIUS,
                          color);
}

/** @brief One line of the status screen, value column at a fixed x. The
 * label goes on the screen once; the value only when it differs from what
 * the line already shows, so an unchanged screen costs no redraw. */
static void displayLine(int line, const char *label, const char *value) {
  static char lastValue[DISPLAY_LINES][DISPLAY_VALUE_LEN];
  static bool labelDrawn[DISPLAY_LINES];
  const int y = 48 + (line * 28);

  if (!labelDrawn[line]) {
    labelDrawn[line] = true;
    hal_display_set_cursor(8, y);
    hal_display_set_text_color(ST77XX_WHITE);
    hal_display_print(label);
  }
  if (strcmp(lastValue[line], value) == 0) {
    return;
  }
  (void)snprintf(lastValue[line], sizeof(lastValue[line]), "%s", value);
  hal_display_fill_rect(200, y, SCREEN_WIDTH - 200, 24, ST77XX_BLACK);
  hal_display_set_cursor(200, y);
  hal_display_set_text_color(ST77XX_GREEN);
  hal_display_print(value);
}

static void updateDisplay(void) {
  char text[DISPLAY_VALUE_LEN];
  VP37Status status;
  const bool fresh = VP37_readStatus(&s_pump, &status) == HAL_OK;

  displayLine(0, "pump",
              !fresh               ? "starting"
              : status.initialized ? "running"
                                   : "stopped");
  displayLine(1, "start status", VP37_initStatusName(benchStartStatus()));
  (void)snprintf(text, sizeof(text), "%lu",
                 (unsigned long)(fresh ? status.readCount : 0U));
  displayLine(2, "frames", text);
  (void)snprintf(text, sizeof(text), "%.1f C", fresh ? status.fuelTempC : 0.0);
  displayLine(3, "fuel temp", text);
  (void)snprintf(text, sizeof(text), "%.1f V",
                 fresh ? status.supplyVolts : 0.0);
  displayLine(4, "supply", text);
  (void)snprintf(text, sizeof(text), "%.0f %%", vp37BenchDemandPercent());
  displayLine(5, "demand pot", text);

  drawAdjConnectionDot();
}

static void updateValsForDebug(void) {
  VP37Status status;
  if (VP37_readStatus(&s_pump, &status) == HAL_OK) {
    deb("VP37 bench init:%d frames:%lu ft:%.1f V:%.1f pot:%.0f",
        status.initialized ? 1 : 0, (unsigned long)status.readCount,
        status.fuelTempC, status.supplyVolts, vp37BenchDemandPercent());
  } else {
    deb("VP37 bench pump not published yet");
  }
}

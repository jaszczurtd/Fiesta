
#include "logic.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "buzzerStrategy.h"
#include "chargeLamp.h"
#include "egt.h"
#include "gps.h"
#include "oilPressure.h"
#include "speed.h"
#include "timingDiag.h"
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <utils/multicoreWatchdog.h>
#include <utils/tools_common_defs.h>

// Core 0 receives CAN, measures the ABS speed, drives both mechanical gauges,
// the lamps and the buzzer. Core 1 draws the TFT, reads the oil pressure and
// both EGT and GPS and serves the configurator session. Core 0 starts before
// the logo and does not wait for core 1.

const char *err = "ERR";

void callAtEverySecond(void);
void callAtEveryHalfSecond(void);
void callAtEveryHalfHalfSecond(void);
void updateValsForDebug(void);
void processAlerts(void);
void processTemperatureBuzzerAlerts(const clocks_local_snapshot_t *local);
void readLocalOilPressure(void);
void readLocalEgt(void);

void drawLowImportanceValues(void);
void drawMediumImportanceValues(void);
void drawMediumMediumImportanceValues(void);
void drawHighImportanceValuesIfChanged(void);

static unsigned long alertsStartSecond = 0;
static unsigned long lastThreadSeconds = 0;
static bool startBeepDone = false;
static Cluster cluster;
static BuzzerStrategy buzzerStrategy;

static hal_soft_timer_t timerCANCheck = NULL;
static hal_soft_timer_t timerAlerts = NULL;

static hal_soft_timer_t timerEverySecond = NULL;
static hal_soft_timer_t timerHalfSecond = NULL;
static hal_soft_timer_t timerQuarterSecond = NULL;
static hal_soft_timer_t timerEGT = NULL;
static hal_soft_timer_t timerDebug = NULL;
static hal_soft_timer_t timerOilPressure = NULL;
static hal_soft_timer_t timerThermocouples = NULL;

// Measurements of core 1, published after every reading.
static clocks_local_snapshot_t localMeasurements;

static const hal_soft_timer_table_entry_t canTimerTable[] = {
    {&timerCANCheck, canCheckConnection, (uint32_t)CAN_CHECK_CONNECTION},
    {&timerAlerts, processAlerts, (uint32_t)(SECOND / 2)}};

static const hal_soft_timer_table_entry_t displayTimerTable[] = {
    {&timerEverySecond, callAtEverySecond, (uint32_t)SECOND},
    {&timerHalfSecond, callAtEveryHalfSecond, (uint32_t)(SECOND / 2)},
    {&timerQuarterSecond, callAtEveryHalfHalfSecond, (uint32_t)(SECOND / 4)},
    {&timerEGT, changeEGT, (uint32_t)DPF_SHOW_TIME_INTERVAL},
    {&timerDebug, updateValsForDebug, (uint32_t)DEBUG_UPDATE},
    {&timerOilPressure, readLocalOilPressure,
     (uint32_t)OIL_PRESSURE_READ_INTERVAL},
    {&timerThermocouples, readLocalEgt, (uint32_t)THERMOCOUPLE_READ_INTERVAL}};

NOINIT int statusVariable0;
NOINIT int statusVariable1;

#ifdef DEBUG_SCREEN
void debugFunc(void) { deb("Debug function called"); }
#endif

static int *wValues = NULL;
static int wSize = 0;
void executeByWatchdog(int *values, int size) {
  wValues = values;
  wSize = size;
}

static void initializeCore0(void) {
  timingDiagPaintStack(0u);

  hal_debug_init_default();
  hal_debug_set_module_prefix(SC_MODULE_TOKEN_CLOCKS);

  deb("Setup started");

  setupOnboardLed();
  initBasicPIO();
  initChargeLamp();
  buzzerStrategy.reset();

  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  if (!rebooted) {
    statusVariable0 = statusVariable1 = 0;
  }

  initSPI();
  clocksStateInit();
  cluster.init();
  const bool absReady = setupSpeedometer();
  canSetAbsReady(absReady);
  if (!absReady) {
    derr("cannot setup the ABS speedometer");
  }

  hal_rgb_led_set_color(canInit() == HAL_OK ? HAL_RGB_LED_GREEN
                                            : HAL_RGB_LED_RED);
  canCheckConnection();
  updateCANrecipients();

  hal_soft_timer_setup_table(canTimerTable, COUNTOF(canTimerTable),
                             hal_watchdog_feed, CORE_OPERATION_DELAY);
  (void)canSetupBroadcastTimers();

  setStartedCore0();
}

// Wait for the next CAN frame or the end of one loop period, whichever
// comes first; a pending frame never waits for a sleep to end.
static void waitForCanOrLoopPeriod(void) {
  const uint32_t started = hal_micros();
  while (!canRxPending() &&
         !hal_elapsed_u32(hal_micros(), started,
                          (uint32_t)CORE_OPERATION_DELAY * 1000u)) {
    hal_idle();
  }
}

static void runCore0(void) {
  static uint32_t lastCanPollMs = 0u;

  statusVariable0 = 0;
  updateWatchdogCore0();
  statusVariable0 = 1;
  const uint32_t loopStart = timingDiagLoopStart();

  if (canRxPending() || hal_millis_interval_elapsed_now(
                            &lastCanPollMs, CAN_MAIN_LOOP_READ_INTERVAL)) {
    canMainLoop();
  }

  hal_soft_timer_tick_table(canTimerTable, COUNTOF(canTimerTable));
  canTickBroadcastTimers();
  if (canRetryInit()) {
    hal_rgb_led_set_color(HAL_RGB_LED_GREEN);
  }
  updateChargeLamp();

  if (!startBeepDone && isEnvironmentStarted()) {
    startBeepDone = true;
    startBuzzer(BUZZER_SHORT);
  }
  loopBuzzers();

  onImpulseTranslating();
  updateCluster();

  timingDiagLoopEnd(0u, loopStart);
  statusVariable0 = 2;
  waitForCanOrLoopPeriod();
}

extern "C" void app_task0(void) { runCore0(); }

void seriousAlertsDrawFunctions() {
#ifndef DEBUG_SCREEN
  drawFuelEmpty();

#endif
}

static bool alertBlink = false, seriousAlertBlink = false;
bool alertSwitch(void) { return alertBlink; }
bool seriousAlertSwitch(void) { return seriousAlertBlink; }

// timer functions
void callAtEverySecond(void) {
  alertBlink = (alertBlink) ? false : true;

#if SYSTEM_TEMP
  deb("System temperature: %f", hal_read_chip_temp());
#endif

  // regular draw - low importance values
  drawLowImportanceValues();
}

void callAtEveryHalfSecond(void) {
  timingDiagDisplayTick();

  seriousAlertBlink = (seriousAlertBlink) ? false : true;

  // draw changes of medium importance values
  drawMediumImportanceValues();
}

// Core 0: the oil lamp and the temperature buzzer follow the ECU state of
// core 0 and the local measurements of core 1. A faulty oil sender keeps the
// lamp on.
void processAlerts(void) {
  clocks_local_snapshot_t local;
  clocksStateCopyLocal(&local);
  enableOilLamp(!local.oilValid || local.oilBar < MIN_OIL_PRESSURE);
  processTemperatureBuzzerAlerts(&local);
}

void processTemperatureBuzzerAlerts(const clocks_local_snapshot_t *local) {
  BuzzerStrategyInput input;
  input.engineRunning = (int(canGetValue(F_RPM)) != 0);
  input.nowMs = hal_millis();
  input.coolantTemp = int(canGetValue(F_COOLANT_TEMP));
  input.oilTemp = int(canGetValue(F_OIL_TEMP));
  input.egtTemp =
      local->egtValid[EGT_PRE_DPF] ? int(local->egtC[EGT_PRE_DPF]) : 0;

  switch (buzzerStrategy.process(input)) {
  case BUZZER_STRATEGY_MIDDLE:
    startBuzzer(BUZZER_MIDDLE);
    break;
  case BUZZER_STRATEGY_LONG:
    startBuzzer(BUZZER_LONG);
    break;
  }
}

void callAtEveryHalfHalfSecond(void) {
  if (alertsStartSecond <= hal_get_seconds()) {
    seriousAlertsDrawFunctions();
  }
  drawMediumMediumImportanceValues();
}

static bool highImportanceValueChanged = false;
void triggerDrawHighImportanceValue(bool state) {
  highImportanceValueChanged = state;
}

// Core 0: called from the RPM and speed frames and once per loop pass.
void updateCluster(void) {
  cluster.update((unsigned int)canGetValue(F_ABS_CAR_SPEED),
                 (unsigned int)canGetValue(F_RPM));
}

void drawLowImportanceValues(void) {
#ifndef DEBUG_SCREEN
  showSimpleGauges();
  showFuelAmount();
#endif
}

void drawHighImportanceValues(void) {
#ifndef DEBUG_SCREEN
  showEngineLoadGauge();
  showPressureGauges();
#endif
}

void drawMediumImportanceValues(void) {
#ifndef DEBUG_SCREEN
  showTempGauges();
  showEGTGauge();
  showECUConnectionGauge();
  drawHighImportanceValues();
#endif
}

void drawMediumMediumImportanceValues(void) {
#ifndef DEBUG_SCREEN
  showGPSGauge();
#endif
}

void drawHighImportanceValuesIfChanged(void) {
  // draw changes of high importance values
  if (highImportanceValueChanged) {
    drawHighImportanceValues();
    triggerDrawHighImportanceValue(false);
  }
}

// Core 1: one reading of the oil pressure sender.
void readLocalOilPressure(void) {
  float bar = 0.0f;
  const bool valid = (readOilPressure(&bar) == OIL_SENDER_OK);
  localMeasurements.oilValid = valid;
  localMeasurements.oilBar = valid ? bar : 0.0f;
  if (valid) {
    localMeasurements.oilUpdatedMs = hal_millis();
  }
  clocksStatePublishLocal(&localMeasurements);
}

// Core 1: one reading of both thermocouples.
void readLocalEgt(void) {
  const uint32_t started = timingDiagLoopStart();
  egtRead(&localMeasurements, hal_millis());
  timingDiagEgtRead(started);
  clocksStatePublishLocal(&localMeasurements);
}

// Poll every pass, publish on new data or a change of validity or speed.
static void readLocalGPS(void) {
  if (clocksGpsReadChanged(&localMeasurements.gps)) {
    clocksStatePublishLocal(&localMeasurements);
  }
}

static void initializeCore1(void) {
  timingDiagPaintStack(1u);
  initTFT();
  (void)setupOilPressure();
  egtInit();
  initGPS();

  int sec = hal_get_seconds();
  const int secDest = sec + FIESTA_INTRO_TIME;

#ifndef DEBUG_SCREEN
  hal_display_fill_screen(HAL_COLOR(WHITE));
  const int x = (SCREEN_W - FIESTA_LOGO_WIDTH) / 2;
  const int y = (SCREEN_H - FIESTA_LOGO_HEIGHT) / 2;
  hal_display_draw_image(x, y, FIESTA_LOGO_WIDTH, FIESTA_LOGO_HEIGHT,
                         HAL_COLOR(WHITE), (uint16_t *)FiestaLogo);
#endif // DEBUG_SCREEN

  while (sec < secDest) {
    updateWatchdogCore1();
    readLocalGPS();
    hal_idle();
    sec = hal_get_seconds();
  }

  hal_display_fill_screen(ICONS_BG_COLOR);

  initFuelMeasurement();
  (void)clocksStateRefreshView();

#ifdef DEBUG_SCREEN
  debugFunc();
#else
  triggerDrawHighImportanceValue(true);
  redrawAllGauges();
#endif

  alertsStartSecond = hal_get_seconds() + SERIOUS_ALERTS_DELAY_TIME;

  callAtEverySecond();
  callAtEveryHalfSecond();
  callAtEveryHalfHalfSecond();
  updateValsForDebug();

  hal_soft_timer_setup_table(displayTimerTable, COUNTOF(displayTimerTable),
                             hal_watchdog_feed, CORE_OPERATION_DELAY);
  configSessionInit();

  setStartedCore1();
  deb("Setup finished");
}

static void runCore1(void) {
  statusVariable1 = 0;
  updateWatchdogCore1();
  statusVariable1 = 1;
  const uint32_t loopStart = timingDiagLoopStart();
  readLocalGPS();

  if (clocksStateRefreshView()) {
    triggerDrawHighImportanceValue(true);
  }

  hal_soft_timer_tick_table(displayTimerTable, COUNTOF(displayTimerTable));
  if (lastThreadSeconds < hal_get_seconds()) {
    lastThreadSeconds = hal_get_seconds() + THREAD_CONTROL_SECONDS;

    deb("thread is alive");
  }
  statusVariable1 = 2;
  drawHighImportanceValuesIfChanged();
  statusVariable1 = 3;
  configSessionTick();

  if (isEcuConnected()) {
    hal_rgb_led_set_color(HAL_RGB_LED_GREEN);
  } else {
    hal_rgb_led_set_color(alertSwitch() ? HAL_RGB_LED_NONE : HAL_RGB_LED_RED);
  }

  if (timingDiagRedrawDue()) {
    redrawAllGauges();
    triggerDrawHighImportanceValue(true);
  }
  timingDiagLoopEnd(1u, loopStart);

  hal_delay_ms(CORE_OPERATION_DELAY);
  hal_idle();
}

void updateValsForDebug(void) {

  deb("ECU:%s", isEcuConnected() ? "on" : "off");
  deb("DPF:%s", isDPFConnected() ? "on" : "off");
  deb("current speed:%d Km/h current rpm:%d RPM", getCurrentCarSpeed(),
      getEngineRPM());
  deb("EGT: %dC%s DPF: %dC%s oil: %.1fbar%s", int(valueFields[F_EGT]),
      isEgtValid(EGT_PRE_DPF) ? "" : " (n/a)", int(valueFields[F_DPF_TEMP]),
      isEgtValid(EGT_IN_DPF) ? "" : " (n/a)", (double)getOilPressure(),
      isOilPressureValid() ? "" : " (sender fault)");
}

extern "C" void app_start(void) {
  initializeCore0();
#if !HAL_TARGET_IS_RP
  initializeCore1();
#endif
}

extern "C" void app_task1(void) {
#if HAL_TARGET_IS_RP
  static bool initialized = false;
  if (!initialized) {
    initializeCore1();
    initialized = true;
  }
#endif
  runCore1();
}

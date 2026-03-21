
#include "logic.h"

const char *err = (char*)F("ERR");

void callAtEverySecond(void);
void callAtEveryHalfSecond(void);
void callAtEveryHalfHalfSecond(void);
void updateValsForDebug(void);

void drawLowImportanceValues(void);
void drawMediumImportanceValues(void);
void drawMediumMediumImportanceValues(void);
void drawHighImportanceValuesIfChanged(void);

static unsigned long alertsStartSecond = 0;
static unsigned long lastThreadSeconds = 0;
static SmartTimers timerEverySecond;
static SmartTimers timerHalfSecond;
static SmartTimers timerQuarterSecond;
static SmartTimers timerSoftInit;
static SmartTimers timerEGT;
static SmartTimers timerDebug;
static SmartTimers timerCANUpdate;
static SmartTimers timerCANCheck;
static Cluster cluster;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

void setupTimers(void) {
  timerEverySecond.begin(callAtEverySecond, SECOND);           m_delay(CORE_OPERATION_DELAY);
  timerHalfSecond.begin(callAtEveryHalfSecond, SECOND / 2);   m_delay(CORE_OPERATION_DELAY);
  timerQuarterSecond.begin(callAtEveryHalfHalfSecond, SECOND / 4); m_delay(CORE_OPERATION_DELAY);
  timerSoftInit.begin(softInitDisplay, DISPLAY_SOFTINIT_TIME); m_delay(CORE_OPERATION_DELAY);
  timerEGT.begin(changeEGT, DPF_SHOW_TIME_INTERVAL);          m_delay(CORE_OPERATION_DELAY);
  timerDebug.begin(updateValsForDebug, DEBUG_UPDATE);          m_delay(CORE_OPERATION_DELAY);
  timerCANUpdate.begin(updateCANrecipients, CAN_UPDATE_RECIPIENTS); m_delay(CORE_OPERATION_DELAY);
  timerCANCheck.begin(canCheckConnection, CAN_CHECK_CONNECTION);
}

static int *wValues = NULL;
static int wSize = 0;
void executeByWatchdog(int *values, int size) {
  wValues = values;
  wSize = size;
}

void setup_a(void) {

  debugInit();
  setDebugPrefix("Clocks:");
  setupOnboardLed();
  initBasicPIO();

  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  if(!rebooted) {
    statusVariable0 = statusVariable1 = 0;
  }

  initSPI();

  TFT *tft = initTFT();

  int sec = getSeconds();
  const int secDest = sec + FIESTA_INTRO_TIME;

  #ifndef DEBUG_SCREEN
  tft->fillScreen(COLOR(WHITE));
  const int x = (SCREEN_W - FIESTA_LOGO_WIDTH) / 2;
  const int y = (SCREEN_H - FIESTA_LOGO_HEIGHT) / 2;
  tft->drawImage(x, y, FIESTA_LOGO_WIDTH, FIESTA_LOGO_HEIGHT, 0xffff, (unsigned short*)FiestaLogo);
#endif //DEBUG_SCREEN

  hal_watchdog_feed();

  hal_rgb_led_set_color(canInit() ? HAL_RGB_LED_RED : HAL_RGB_LED_GREEN);

  while(sec < secDest) {
    hal_watchdog_feed();
    sec = getSeconds();
  }

  softInitDisplay();
  tft->fillScreen(ICONS_BG_COLOR);

  initFuelMeasurement();
  canCheckConnection();

  #ifdef DEBUG_SCREEN
  debugFunc();
  #else  
  triggerDrawHighImportanceValue(true);
  redrawAllGauges();
  #endif

  alertsStartSecond = getSeconds() + SERIOUS_ALERTS_DELAY_TIME;

  callAtEverySecond();
  callAtEveryHalfSecond();
  callAtEveryHalfHalfSecond();
  updateValsForDebug();

  updateCANrecipients();
  canMainLoop();

  hal_watchdog_feed();

  setupTimers();

  setStartedCore0();

  startBuzzer(BUZZER_SHORT);
}

void loop_a(void) {

  statusVariable0 = 0;
  updateWatchdogCore0();

  if(!isEnvironmentStarted()) {
    statusVariable0 = -1;
    m_delay(CORE_OPERATION_DELAY);  
    hal_idle();
    return;
  }

  statusVariable0 = 1;

  timerEverySecond.tick();
  timerHalfSecond.tick();
  timerQuarterSecond.tick();
  timerSoftInit.tick();
  timerEGT.tick();
  timerDebug.tick();
  timerCANUpdate.tick();
  timerCANCheck.tick();
  if(lastThreadSeconds < getSeconds()) {
    lastThreadSeconds = getSeconds() + THREAD_CONTROL_SECONDS;

    deb("thread is alive");
  }
  statusVariable0 = 2;
  drawHighImportanceValuesIfChanged();
  statusVariable0 = 3;

  if(isEcuConnected()) {
    hal_rgb_led_set_color(HAL_RGB_LED_GREEN);
  } else {
    hal_rgb_led_set_color(alertSwitch() ? HAL_RGB_LED_NONE : HAL_RGB_LED_RED);
  }

  loopBuzzers();

  canMainLoop();
  cluster.update(getCurrentCarSpeed(), getEngineRPM());

  m_delay(CORE_OPERATION_DELAY);
  hal_idle();
}

void seriousAlertsDrawFunctions() {
  #ifndef DEBUG_SCREEN
  drawFuelEmpty();

  #endif
}

static bool alertBlink = false, seriousAlertBlink = false;
bool alertSwitch(void) {
  return alertBlink;
}
bool seriousAlertSwitch(void) {
  return seriousAlertBlink;
}

//timer functions
void callAtEverySecond(void) {
  alertBlink = (alertBlink) ? false : true;

#if SYSTEM_TEMP
  deb("System temperature: %f", hal_read_chip_temp());
#endif

  //regular draw - low importance values
  drawLowImportanceValues();
}

void callAtEveryHalfSecond(void) {

  enableOilLamp((getOilPressure() < MIN_OIL_PRESSURE));

  seriousAlertBlink = (seriousAlertBlink) ? false : true;

  //draw changes of medium importance values
  drawMediumImportanceValues();
}

void callAtEveryHalfHalfSecond(void) {
  if(alertsStartSecond <= getSeconds()) {
    seriousAlertsDrawFunctions();
  }
  drawMediumMediumImportanceValues();
}

static bool highImportanceValueChanged = false;
void triggerDrawHighImportanceValue(bool state) {
  highImportanceValueChanged = state;
}

void updateCluster(void) {
  cluster.update(getCurrentCarSpeed(), getEngineRPM());
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
  //draw changes of high importance values
  if(highImportanceValueChanged) {
    drawHighImportanceValues();
    triggerDrawHighImportanceValue(false);
  }
}

void setup_b(void) {
  setStartedCore1();
}

void loop_b(void) {
  updateWatchdogCore1();

  if(!isEnvironmentStarted()) {
    statusVariable1 = -1;
    m_delay(CORE_OPERATION_DELAY);  
    hal_idle();
    return;
  }
  statusVariable1 = 1;

  m_delay(CORE_OPERATION_DELAY);
  hal_idle();
}

void updateValsForDebug(void) {

  deb("ECU:%s", isEcuConnected() ? "on" : "off");
  deb("oil & speed module:%s", isOilSpeedModuleConnected() ? "on" : "off");
  deb("current speed:%d Km/h current rpm:%d RPM", getCurrentCarSpeed(), getEngineRPM());
}
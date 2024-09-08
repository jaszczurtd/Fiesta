
#include "logic.h"

const char *err = (char*)F("ERR");

bool callAtEverySecond(void *arg);
bool callAtEveryHalfSecond(void *arg);
bool callAtEveryHalfHalfSecond(void *arg);
bool updateValsForDebug(void *arg);

void drawLowImportanceValues(void);
void drawMediumImportanceValues(void);
void drawMediumMediumImportanceValues(void);
void drawHighImportanceValuesIfChanged(void);

static unsigned long alertsStartSecond = 0;
static unsigned long lastThreadSeconds = 0;
static Timer generalTimer;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

void setupTimerWith(unsigned long time, bool(*function)(void *argument)) {
  //watchdog_feed();
  generalTimer.every(time, function);
  m_delay(CORE_OPERATION_DELAY);
}

void setupTimers(void) {

  generalTimer = timer_create_default();

  setupTimerWith(SECOND, callAtEverySecond);
  setupTimerWith(SECOND / 2, callAtEveryHalfSecond);
  setupTimerWith(SECOND / 4, callAtEveryHalfHalfSecond);
  setupTimerWith(DISPLAY_SOFTINIT_TIME, softInitDisplay);
  setupTimerWith(DPF_SHOW_TIME_INTERVAL, changeEGT);
  setupTimerWith(DEBUG_UPDATE, updateValsForDebug);
  setupTimerWith(CAN_UPDATE_RECIPIENTS, updateCANrecipients);
  setupTimerWith(CAN_MAIN_LOOP_READ_INTERVAL, canMainLoop);
  setupTimerWith(CAN_CHECK_CONNECTION, canCheckConnection);  
}

static int *wValues = NULL;
static int wSize = 0;
void executeByWatchdog(int *values, int size) {
  wValues = values;
  wSize = size;
}

void setup_a(void) {

  debugInit();
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
  #ifdef INC_FREERTOS_H
  tft->drawRGBBitmap(SCREEN_W - FREERTOS_WIDTH - 1, SCREEN_H - FREERTOS_HEIGHT - 1, 
                      (unsigned short*)freertos, FREERTOS_WIDTH, FREERTOS_HEIGHT);
  #endif //INC_FREERTOS_H
  #endif //DEBUG_SCREEN

  watchdog_feed();

  setLEDColor(canInit() ? RED: GREEN);

  while(sec < secDest) {
    watchdog_feed();
    sec = getSeconds();
  }

  softInitDisplay(NULL);
  tft->fillScreen(ICONS_BG_COLOR);

  canCheckConnection(NULL);

  #ifdef DEBUG_SCREEN
  debugFunc();
  #else  
  triggerDrawHighImportanceValue(true);
  redrawAllGauges();
  #endif

  alertsStartSecond = getSeconds() + SERIOUS_ALERTS_DELAY_TIME;

  updateCANrecipients(NULL);
  canMainLoop(NULL);
  callAtEverySecond(NULL);
  callAtEveryHalfSecond(NULL);
  callAtEveryHalfHalfSecond(NULL);
  updateValsForDebug(NULL);

  watchdog_feed();

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
    tight_loop_contents();
    return;
  }

  statusVariable0 = 1;

  generalTimer.tick();
  if(lastThreadSeconds < getSeconds()) {
    lastThreadSeconds = getSeconds() + THREAD_CONTROL_SECONDS;

    deb("thread is alive, active tasks: %d", generalTimer.size());
  }
  statusVariable0 = 2;
  drawHighImportanceValuesIfChanged();
  statusVariable0 = 3;

  if(isEcuConnected()) {
    setLEDColor(GREEN);
  } else {
    setLEDColor(alertSwitch() ? NONE : RED);
  }

  loopBuzzers();

  m_delay(CORE_OPERATION_DELAY);  
  tight_loop_contents();
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
bool callAtEverySecond(void *arg) {
  alertBlink = (alertBlink) ? false : true;

#if SYSTEM_TEMP
  deb("System temperature: %f", analogReadTemp());
#endif

  //regular draw - low importance values
  drawLowImportanceValues();
  return true;
}

bool callAtEveryHalfSecond(void *arg) {
  seriousAlertBlink = (seriousAlertBlink) ? false : true;

  //draw changes of medium importance values
  drawMediumImportanceValues();
  return true;
}

bool callAtEveryHalfHalfSecond(void *arg) {
  if(alertsStartSecond <= getSeconds()) {
    seriousAlertsDrawFunctions();
  }
  drawMediumMediumImportanceValues();
  return true;
}

static bool highImportanceValueChanged = false;
void triggerDrawHighImportanceValue(bool state) {
  highImportanceValueChanged = state;
}

void drawLowImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showSimpleGauges();
  showFuelAmount((int)valueFields[F_FUEL], FUEL_MIN - FUEL_MAX);
  #endif
}

void drawHighImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showEngineLoadGauge();
  #endif
}

void drawMediumImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showTempGauges();
  showEGTGauge();
  showECUConnectionGauge();
  #endif
}

void drawMediumMediumImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showPressureGauges();
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
    tight_loop_contents();
    return;
  }
  statusVariable1 = 1;

  m_delay(CORE_OPERATION_DELAY);  
  tight_loop_contents();
}

bool updateValsForDebug(void *arg) {

  deb("ECU:%s", isEcuConnected() ? "on" : "off");

  return true;
}
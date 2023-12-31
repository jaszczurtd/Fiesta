
#include "start.h"

static unsigned long alertsStartSecond = 0;
static unsigned long lastThreadSeconds = 0;
static Timer generalTimer;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

void setupTimerWith(unsigned long time, bool(*function)(void *argument)) {
  watchdog_update();
  generalTimer.every(time, function);
}

void setupTimers(void) {

  generalTimer = timer_create_default();

  int time = SECOND;
  setupTimerWith(time, callAtEverySecond);
  setupTimerWith(time / 2, callAtEveryHalfSecond);
  setupTimerWith(time / 4, callAtEveryHalfHalfSecond);
  setupTimerWith(DISPLAY_SOFTINIT_TIME, softInitDisplay);
  setupTimerWith(time / MEDIUM_TIME_ONE_SECOND_DIVIDER, readMediumValues);
  setupTimerWith(time / FREQUENT_TIME_ONE_SECOND_DIVIDER, readHighValues);
  setupTimerWith(DPF_SHOW_TIME_INTERVAL, changeEGT);
  setupTimerWith(GPS_UPDATE, getGPSData);
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

void initialization(void) {

  debugInit();
 
  initTests();

  //adafruit LCD driver is messing up something with i2c on rpi pin 0 & 1
  //this has to be invoked as soon as possible, and twice
  initI2C();
  pcf8574_init();
  Wire.end();

  TFT *tft = initTFT();
  initSPI();

  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  if(!rebooted) {
    statusVariable0 = statusVariable1 = 0;
    initGPSDateAndTime();
  }

  initI2C();

  #ifdef RESET_EEPROM
  resetEEPROM();
  #endif

  initSDLogger(SD_CARD_CS); 
  if (!isSDLoggerInitialized()) {
    deb("SD Card failed, or not present");
  } else {
    deb("SD Card initialized");
  }

  if(wValues != NULL) {
    char dateAndTime[GPS_TIME_DATE_BUFFER_SIZE * 2];
    memset(dateAndTime, 0, sizeof(dateAndTime));

    bool validDateAndTime = isValidString(getGPSDate(), GPS_TIME_DATE_BUFFER_SIZE) && 
      isValidString(getGPSTime(), GPS_TIME_DATE_BUFFER_SIZE);

    if(validDateAndTime) {
      snprintf(dateAndTime, sizeof(dateAndTime) - 1, "%s-%s", 
        getGPSDate(), getGPSTime());
    }

    watchdog_update();
    initCrashLogger(dateAndTime, SD_CARD_CS);
    if(validDateAndTime) {
      crashReport("date:%s time:%s", getGPSDate(), getGPSTime());
    }
    crashReport("core0 started: %d", wValues[0]);
    crashReport("core0 was running: %d", wValues[1]);
    crashReport("core1 started: %d", wValues[2]);
    crashReport("core1 was running: %d", wValues[3]);

    crashReport("sv0: %d", statusVariable0);
    crashReport("sv1: %d", statusVariable1);

    saveCrashLoggerAndClose();
    watchdog_update();

    wSize = 0;
    wValues = NULL;
  }

  initBasicPIO();

  #ifdef I2C_SCANNER
  i2cScanner();
  #endif

  initSensors();

  float coolant = readCoolantTemp();
  valueFields[F_COOLANT_TEMP] = coolant;

  if(coolant <= TEMP_LOWEST) {
    coolant = TEMP_LOWEST;
  }
  initGlowPlugsTime(coolant);

  watchdog_update();

  #ifndef DEBUG_SCREEN
  tft->fillScreen(COLOR(WHITE));
  int x = (SCREEN_W - FIESTA_LOGO_WIDTH) / 2;
  int y = (SCREEN_H - FIESTA_LOGO_HEIGHT) / 2;
  tft->drawImage(x, y, FIESTA_LOGO_WIDTH, FIESTA_LOGO_HEIGHT, 0xffff, (unsigned short*)FiestaLogo);
  #endif

  watchdog_update();

  int sec = getSeconds();
  int secDest = sec + FIESTA_INTRO_TIME;
  vp37Calibrate();
  while(sec < secDest) {
    glowPlugsMainLoop();
    sec = getSeconds();
    watchdog_update();
  }

  tft->fillScreen(ICONS_BG_COLOR);

  canInit(CAN_RETRIES);
  obdInit(CAN_RETRIES);

  valueFields[F_VOLTS] = readVolts();
  TEST_ASSERT_TRUE(valueFields[F_VOLTS] > 0);

  #ifdef PICO_W
  scanNetworks(WIFI_SSID);
  #endif

  initHeatedWindow();
  initFuelMeasurement();
  
  #ifdef DEBUG_SCREEN
  debugFunc();
  #else  
  redrawAllGauges();
  #endif

  alertsStartSecond = getSeconds() + SERIOUS_ALERTS_DELAY_TIME;

  canCheckConnection(NULL);
  updateCANrecipients(NULL);
  canMainLoop(NULL);
  callAtEverySecond(NULL);
  callAtEveryHalfSecond(NULL);
  callAtEveryHalfHalfSecond(NULL);
  updateValsForDebug(NULL);

  setupTimers();

  deb("System temperature:%.1fC", rroundf(analogReadTemp()));
  
  setStartedCore0();

  deb("Fiesta MTDDI started: %s\n", isEnvironmentStarted() ? "yes" : "no");

  startTests();
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
  #endif
}

void drawMediumMediumImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showPressureGauges();
  showGPSGauge();
  #endif
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
  digitalWrite(LED_BUILTIN, alertBlink);

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

  digitalWrite(PIO_DPF_LAMP, isDPFRegenerating());
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

void drawHighImportanceValuesIfChanged(void) {
  //draw changes of high importance values
  if(highImportanceValueChanged) {
    drawHighImportanceValues();
    triggerDrawHighImportanceValue(false);
  }
}

void looper(void) {
  statusVariable0 = 0;
  updateWatchdogCore0();

  if(!isEnvironmentStarted()) {
    statusVariable0 = -1;
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
  obdLoop();
  statusVariable0 = 3;
  glowPlugsMainLoop();
  statusVariable0 = 4;
  fanMainLoop();
  statusVariable0 = 5;
  engineHeaterMainLoop();
  statusVariable0 = 6;
  heatedWindowMainLoop();
  statusVariable0 = 7;

  delay(CORE_OPERATION_DELAY);  
}

void initialization1(void) {
  initRPMCount();

  setStartedCore1();
  
  deb("Second core initialized");
}

//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------

void looper1(void) {

  statusVariable1 = 0;
  updateWatchdogCore1();

  if(!isEnvironmentStarted()) {
    statusVariable1 = -1;
    return;
  }

  statusVariable1 = 1;
  turboMainLoop();
  statusVariable1 = 2;
#ifdef VP37
  vp37Process();
#else
  stabilizeRPM();
#endif
  statusVariable1 = 3;
  delay(CORE_OPERATION_DELAY);  
}


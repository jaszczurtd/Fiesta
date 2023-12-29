
#include "start.h"

static unsigned long alertsStartSecond = 0;
static unsigned long lastThreadSeconds = 0;
//static Timer generalTimer;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

void setupTimerWith(const char *name, unsigned long time, void(*function)(TimerHandle_t xTimer)) {
  TimerHandle_t xTimer = xTimerCreate(name, pdMS_TO_TICKS(time), pdTRUE, 0, function);
  if (xTimer != NULL) {
    if (xTimerStart(xTimer, 0) == pdPASS) {
        deb("%s started!\n", name);
    }
  }
}

void setupTimers(void) {

  int time = SECOND;
  setupTimerWith("one_second", time, callAtEverySecond);
  setupTimerWith("half_second", time / 2, callAtEveryHalfSecond);
  setupTimerWith("one_quarter_second", time / 4, callAtEveryHalfHalfSecond);
  setupTimerWith("softinit_display", DISPLAY_SOFTINIT_TIME, softInitDisplay);

  /*
  setupTimerWith(UNSYNCHRONIZE_TIME, time / MEDIUM_TIME_ONE_SECOND_DIVIDER, readMediumValues);
  setupTimerWith(UNSYNCHRONIZE_TIME, time / FREQUENT_TIME_ONE_SECOND_DIVIDER, readHighValues);
  setupTimerWith(UNSYNCHRONIZE_TIME, CAN_UPDATE_RECIPIENTS, updateCANrecipients);
  setupTimerWith(UNSYNCHRONIZE_TIME, CAN_MAIN_LOOP_READ_INTERVAL, canMainLoop);
  setupTimerWith(UNSYNCHRONIZE_TIME, CAN_CHECK_CONNECTION, canCheckConnection);  
  setupTimerWith(UNSYNCHRONIZE_TIME, DPF_SHOW_TIME_INTERVAL, changeEGT);
  setupTimerWith(UNSYNCHRONIZE_TIME, GPS_UPDATE, getGPSData);
  setupTimerWith(UNSYNCHRONIZE_TIME, DEBUG_UPDATE, updateValsForDebug);
  */
}

static int *wValues = NULL;
static int wSize = 0;
void executeByWatchdog(int *values, int size) {
  wValues = values;
  wSize = size;
}

void initialization(void) {

  Serial.begin(9600);
 
  initTests();

  //adafruit LCD driver is messing up something with i2c on rpi pin 0 & 1
  //this has to be invoked as soon as possible, and twice
  initI2C();
  pcf8574_init();
  Wire.end();

  TFT *tft = initTFT();
  initSPI();

  //generalTimer = timer_create_default();
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

  init4051();
  initSensors();

  analogWriteFreq(PWM_FREQUENCY_HZ);
  analogWriteResolution(PWM_WRITE_RESOLUTION);

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
  enableVP37(true);

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
void callAtEverySecond(TimerHandle_t xTimer) {
  alertBlink = (alertBlink) ? false : true;
  digitalWrite(LED_BUILTIN, alertBlink);

#if SYSTEM_TEMP
  deb("System temperature: %f", analogReadTemp());
#endif

  //regular draw - low importance values
  //drawLowImportanceValues();
}

void callAtEveryHalfSecond(TimerHandle_t xTimer) {
  seriousAlertBlink = (seriousAlertBlink) ? false : true;

  //draw changes of medium importance values
  drawMediumImportanceValues();

  digitalWrite(PIO_DPF_LAMP, isDPFRegenerating());
}

void callAtEveryHalfHalfSecond(TimerHandle_t xTimert) {
  if(alertsStartSecond <= getSeconds()) {
    seriousAlertsDrawFunctions();
  }
  drawMediumMediumImportanceValues();
}

static bool highImportanceValueChanged = false;
void triggerDrawHighImportanceValue(bool state) {
  highImportanceValueChanged = state;
}

void looper(void) {
  statusVariable0 = 0;
  updateWatchdogCore0();

  if(!isEnvironmentStarted()) {
    statusVariable0 = -1;
    return;
  }

  //generalTimer.tick();

  statusVariable0 = 1;
  //draw changes of high importance values
  if(highImportanceValueChanged) {
    drawHighImportanceValues();
    statusVariable0 = 2;
    triggerDrawHighImportanceValue(false);
  }

  statusVariable0 = 3;
  obdLoop();

  statusVariable0 = 4;

  if(lastThreadSeconds < getSeconds()) {
    lastThreadSeconds = getSeconds() + THREAD_CONTROL_SECONDS;

    //deb("thread is alive, active tasks: %d", generalTimer.size());
  }
    
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
  glowPlugsMainLoop();
  statusVariable1 = 2;
  fanMainLoop();
  statusVariable1 = 3;
  engineHeaterMainLoop();
  statusVariable1 = 4;
  heatedWindowMainLoop();

  statusVariable1 = 5;
  turboMainLoop();
  statusVariable1 = 6;
#ifdef VP37
  vp37Process();
#else
  stabilizeRPM();
#endif

  statusVariable1 = 7;
  delay(CORE_OPERATION_DELAY);  
}


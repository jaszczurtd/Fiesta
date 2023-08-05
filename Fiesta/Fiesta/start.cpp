
#include "start.h"

static unsigned long alertsStartSecond = 0;
static Timer generalTimer;

void setupTimerWith(unsigned long ut, unsigned long time, bool(*function)(void *argument)) {
  watchdog_update();
  generalTimer.every(time, function);
  delay(ut);
  watchdog_update();
}

void setupTimers(void) {
  int time = SECOND;

  setupTimerWith(UNSYNCHRONIZE_TIME, time, callAtEverySecond);
  setupTimerWith(UNSYNCHRONIZE_TIME, time / 2, callAtEveryHalfSecond);
  setupTimerWith(UNSYNCHRONIZE_TIME, time / 4, callAtEveryHalfHalfSecond);
  setupTimerWith(UNSYNCHRONIZE_TIME, time / MEDIUM_TIME_ONE_SECOND_DIVIDER, readMediumValues);
  setupTimerWith(UNSYNCHRONIZE_TIME, time / FREQUENT_TIME_ONE_SECOND_DIVIDER, readHighValues);
  setupTimerWith(UNSYNCHRONIZE_TIME, CAN_UPDATE_RECIPIENTS, updateCANrecipients);
  setupTimerWith(UNSYNCHRONIZE_TIME, CAN_MAIN_LOOP_READ_INTERVAL, canMainLoop);
  setupTimerWith(UNSYNCHRONIZE_TIME, CAN_CHECK_CONNECTION, canCheckConnection);  
  setupTimerWith(UNSYNCHRONIZE_TIME, DPF_SHOW_TIME_INTERVAL, changeEGT);
  #ifdef ECU_V2
  setupTimerWith(UNSYNCHRONIZE_TIME, GPS_UPDATE, getGPSData);
  #endif
  setupTimerWith(UNSYNCHRONIZE_TIME, DEBUG_UPDATE, updateValsForDebug);
}

void executeByWatchdog(int *values, int size) {
  #ifdef ECU_V2

  char dateAndTime[32];
  memset(dateAndTime, 0, sizeof(dateAndTime));
  if(isGPSAvailable()) {
    snprintf(dateAndTime, sizeof(dateAndTime) - 1, "%s-%s", 
      getGPSDate(), getGPSTime());
  }

  initCrashLogger(dateAndTime, SD_CARD_CS);
  //for(int a = 0; a < size; a++) {
  //  crashReport("value %d from watchdog: %d", a, values[a]);
  //}
  //saveCrashLoggerAndClose();

  #endif
}

void initialization(void) {

  Serial.begin(9600);
 
  initTests();

  //adafruit LCD driver is messing up something with i2c on rpi pin 0 & 1
  //this has to be invoked as soon as possible, and twice
  initI2C();
  pcf8574_init();
  Wire.end();

  initSPI();

  generalTimer = timer_create_default();
  setupWatchdog(executeByWatchdog, WATCHDOG_TIME);  

  initGraphics();

  initI2C();
 
  #ifdef ECU_V2
  initSDLogger(SD_CARD_CS); 
  if (!isSDLoggerInitialized()) {
    deb("SD Card failed, or not present");
  } else {
    deb("SD Card initialized");
  }
  #endif

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
  showLogo();
  watchdog_update();

  int sec = getSeconds();
  int secDest = sec + FIESTA_INTRO_TIME;
  while(sec < secDest) {
    glowPlugsMainLoop();
    sec = getSeconds();
  }

  fillScreenWithColor(COLOR(BLACK));

  canInit(CAN_RETRIES);
  obdInit(CAN_RETRIES);

  #ifdef DEBUG_SCREEN
  debugFunc();
  #else  
  initHeatedWindow();
  initFuelMeasurement();
  redrawFuel();
  redrawTemperature();
  redrawOil();
  redrawPressure();
  redrawIntercooler();
  redrawEngineLoad();
  redrawRPM();
  redrawEGT();
  #endif

  valueFields[F_VOLTS] = readVolts();
  TEST_ASSERT_TRUE(valueFields[F_VOLTS] > 0);

  alertsStartSecond = getSeconds() + SERIOUS_ALERTS_DELAY_TIME;

  setupTimers();

  canCheckConnection(NULL);
  updateCANrecipients(NULL);
  canMainLoop(NULL);
  callAtEverySecond(NULL);
  callAtEveryHalfSecond(NULL);
  callAtEveryHalfHalfSecond(NULL);
  updateValsForDebug(NULL);

  deb("System temperature:%.1fC", rroundf(analogReadTemp()));
  
  setStartedCore0();
  enableVP37(true);

  deb("Fiesta MTDDI started: %s\n", isEnvironmentStarted() ? "yes" : "no");

  startTests();
}

void drawLowImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showFuelAmount((int)valueFields[F_FUEL], FUEL_MIN - FUEL_MAX);
  showICTemperatureAmount((int)valueFields[F_INTAKE_TEMP]);
  showVolts(valueFields[F_VOLTS]);
  showRPMamount((int)valueFields[F_RPM]);
  #endif
}

void drawHighImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showEngineLoadAmount((int)valueFields[F_THROTTLE_POS]);
  #endif
}

void drawMediumImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showTemperatureAmount((int)valueFields[F_COOLANT_TEMP], TEMP_MAX);
  showOilAmount((int)valueFields[F_OIL_TEMP], TEMP_OIL_MAX);
  showEGTTemperatureAmount();
  #endif
}

void drawMediumMediumImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showPressureAmount(valueFields[F_PRESSURE]);
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
bool callAtEverySecond(void *argument) {
  alertBlink = (alertBlink) ? false : true;
  digitalWrite(LED_BUILTIN, alertBlink);

#if SYSTEM_TEMP
  deb("System temperature: %f", analogReadTemp());
#endif

  //regular draw - low importance values
  drawLowImportanceValues();
  
  return true; 
}

bool callAtEveryHalfSecond(void *argument) {
  seriousAlertBlink = (seriousAlertBlink) ? false : true;

  //draw changes of medium importance values
  drawMediumImportanceValues();

  digitalWrite(PIO_DPF_LAMP, isDPFRegenerating());

  return true; 
}

bool callAtEveryHalfHalfSecond(void *argument) {
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

void looper(void) {
  updateWatchdogCore0();

  if(!isEnvironmentStarted()) {
    return;
  }

  generalTimer.tick();

  //draw changes of high importance values
  if(highImportanceValueChanged) {
    drawHighImportanceValues();
    triggerDrawHighImportanceValue(false);
  }

  obdLoop();

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

  updateWatchdogCore1();

  if(!isEnvironmentStarted()) {
    return;
  }

  glowPlugsMainLoop();
  fanMainLoop();
  engineHeaterMainLoop();
  heatedWindowMainLoop();

  turboMainLoop();
  stabilizeRPM();

  delay(CORE_OPERATION_DELAY);  
}



#include "start.h"

static unsigned long alertsStartSecond = 0;

Timer generalTimer;

void setupTimerWith(unsigned long ut, unsigned long time, bool(*function)(void *argument)) {
  watchdog_update();
  generalTimer.every(time, function);
  delay(ut);
  watchdog_update();
}

void setupTimers(void) {
  int time = 1000;

  setupTimerWith(UNSYNCHRONIZE_TIME, time, callAtEverySecond);
  setupTimerWith(UNSYNCHRONIZE_TIME, time / 2, callAtEveryHalfSecond);
  setupTimerWith(UNSYNCHRONIZE_TIME, time / 4, callAtEveryHalfHalfSecond);
  setupTimerWith(UNSYNCHRONIZE_TIME, time / 12, readMediumValues);
  setupTimerWith(UNSYNCHRONIZE_TIME, time / 16, readHighValues);
  setupTimerWith(UNSYNCHRONIZE_TIME, 200, updateCANrecipients);
  setupTimerWith(UNSYNCHRONIZE_TIME, CAN_MAIN_LOOP_READ_INTERVAL, canMainLoop);
  setupTimerWith(UNSYNCHRONIZE_TIME, CAN_CHECK_CONNECTION, canCheckConnection);  
  setupTimerWith(UNSYNCHRONIZE_TIME, DPF_SHOW_TIME_INTERVAL, changeEGT);
  setupTimerWith(UNSYNCHRONIZE_TIME, GPS_UPDATE * 1000, getGPSData);
  setupTimerWith(UNSYNCHRONIZE_TIME, DEBUG_UPDATE * 1000, updateValsForDebug);
}

void initialization(void) {

  Serial.begin(9600);
 
  //adafruit is messing up something with i2c on rbpi pin 0 & 1
  //this has to be invoked as soon as possible
  Wire.setSDA(0);
  Wire.setSCL(1);
  Wire.setClock(I2C_SPEED);
  Wire.begin();
  pcf8574_init();
  Wire.end();

  //SPI init
  SPI.setRX(16); //MISO
  SPI.setTX(19); //MOSI
  SPI.setSCK(18); //SCK

  generalTimer = timer_create_default();
  setupWatchdog(&generalTimer, WATCHDOG_TIME);  

  initGraphics();

  Wire.setSDA(0);
  Wire.setSCL(1);
  Wire.setClock(I2C_SPEED);
  Wire.begin();
 
  initSDLogger(SD_CARD_CS); 
  if (!isSDLoggerInitialized()) {
    deb("SD Card failed, or not present");
  } else {
    deb("SD Card initialized");
  }

  initBasicPIO();

  #ifdef I2C_SCANNER
  i2cScanner();
  #endif

  init4051();
  initSensors();

  analogWriteFreq(100);
  analogWriteResolution(PWM_WRITE_RESOLUTION);

  float coolant = readCoolantTemp();
  valueFields[F_COOLANT_TEMP] = coolant;

  deb("System temperature: %f", roundz(analogReadTemp(), 1));
  
  if(coolant <= TEMP_LOWEST) {
    coolant = TEMP_LOWEST;
  }
  initGlowPlugsTime(coolant);

  watchdog_update();
  showLogo();
  watchdog_update();

  bool sdCardInit = false;
  int sec = getSeconds();
  int secDest = sec + FIESTA_INTRO_TIME;
  while(sec < secDest) {
    glowPlugsMainLoop();
    sec = getSeconds();
  }

  Adafruit_ST7735 tft = returnReference();
  tft.fillScreen(ST7735_BLACK);

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
  
  alertsStartSecond = getSeconds() + SERIOUS_ALERTS_DELAY_TIME;

  setupTimers();

  canCheckConnection(NULL);
  updateCANrecipients(NULL);
  canMainLoop(NULL);
  callAtEverySecond(NULL);
  callAtEveryHalfSecond(NULL);
  callAtEveryHalfHalfSecond(NULL);
  updateValsForDebug(NULL);

  setStartedCore0();

  deb("Fiesta MTDDI started: %s\n", isEnvironmentStarted() ? "yes" : "no");
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
  showEngineLoadAmount((int)valueFields[F_ENGINE_LOAD]);
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

#ifdef DEBUG_SCREEN
void debugFunc(void) {

  Adafruit_ST7735 tft = returnReference();

  int x = 0;
  int y = 0;
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(x, y); y += 9;
  tft.println(glowPlugsTime);

}
#endif


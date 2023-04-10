
#include "start.h"

static unsigned long alertsStartSecond = 0;
static bool started0 = false, started1 = false;

Timer generalTimer;

bool isEnvironmentStarted(void) {
  return started0 && started1;
}

void initialization(void) {

  Serial.begin(9600);
 
  if (watchdog_caused_reboot()) {
      deb("Rebooted by Watchdog!\n");
  } else {
      deb("Clean boot\n");
  }

  watchdog_enable(WATCHDOG_TIME, false);
  
  //adafruit is messing up something with i2c on rbpi pin 0 & 1
  Wire.setSDA(0);
  Wire.setSCL(1);
  Wire.begin();
  pcf8574_init();
  Wire.end();

  initGraphics();

  Wire.setSDA(0);
  Wire.setSCL(1);
  Wire.begin();
 
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
    sec = getSeconds();
    glowPlugsMainLoop();
  }

  Adafruit_ST7735 tft = returnReference();
  tft.fillScreen(ST7735_BLACK);

  canInit();

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

  alertsStartSecond = getSeconds() + SERIOUS_ALERTS_DELAY_TIME;

  generalTimer = timer_create_default();

  int time = 1000;

  generalTimer.every(time, callAtEverySecond);
  generalTimer.every(time / 2, callAtEveryHalfSecond);
  generalTimer.every(time / 4, callAtEveryHalfHalfSecond);
  generalTimer.every(time / 12, readMediumValues);
  generalTimer.every(time / 16, readHighValues);
  generalTimer.every(200, updateCANrecipients);
  generalTimer.every(CAN_MAIN_LOOP_READ_INTERVAL, canMainLoop);
  generalTimer.every(CAN_CHECK_CONNECTION, canCheckConnection);  
  generalTimer.every(DPF_SHOW_TIME_INTERVAL, changeEGT);

  canCheckConnection(NULL);
  updateCANrecipients(NULL);
  canMainLoop(NULL);
  callAtEverySecond(NULL);
  callAtEveryHalfSecond(NULL);
  callAtEveryHalfHalfSecond(NULL);

  started0 = true;

  deb("Fiesta MTDDI started\n");
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

  digitalWrite(PIO_DPF_LAMP, !isDPFRegenerating());

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
  watchdog_update();

  if(!isEnvironmentStarted()) {
    return;
  }

  generalTimer.tick();

  //draw changes of high importance values
  if(highImportanceValueChanged) {
    drawHighImportanceValues();
    triggerDrawHighImportanceValue(false);
  }

}

void initialization1(void) {
  initRPMCount();

  started1 = true;
  
  deb("Second core initialized");
}

//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------

void looper1(void) {

  if(!isEnvironmentStarted()) {
    return;
  }

  glowPlugsMainLoop();
  fanMainLoop();
  engineHeaterMainLoop();
  heatedWindowMainLoop();

  turboMainLoop();
  stabilizeRPM();

  delay(1);  
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


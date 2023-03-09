
#include "start.h"

static unsigned long alertsStartSecond = 0;
static bool started = false;

Timer generalTimer;

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
 
  pinMode(LED_BUILTIN, OUTPUT);

  #ifdef I2C_SCANNER
  i2cScanner();
  #endif

  init4051();
  initSensors();
  
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

  int time = 500;

  generalTimer.every(time, callAtEverySecond);
  generalTimer.every(time / 3, callAtEveryHalfSecond);
  generalTimer.every(time / 4, callAtEveryHalfHalfSecond);
  generalTimer.every(time / 6, readMediumValues);
  generalTimer.every(time / 8, readHighValues);
  generalTimer.every(200, updateCANrecipients);

  updateCANrecipients(NULL);
  callAtEverySecond(NULL);
  callAtEveryHalfSecond(NULL);
  callAtEveryHalfHalfSecond(NULL);

  started = true;

  deb("Fiesta MTDDI started\n");
}

void drawLowImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showFuelAmount((int)valueFields[F_FUEL], FUEL_MIN - FUEL_MAX);
  showTemperatureAmount((int)valueFields[F_COOLANT_TEMP], TEMP_MAX);
  showOilAmount((int)valueFields[F_OIL_TEMP], TEMP_OIL_MAX);
  showICTemperatureAmount((int)valueFields[F_INTAKE_TEMP]);
  showEGTTemperatureAmount((int)valueFields[F_EGT]);
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
  //
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

  generalTimer.tick();

  //draw changes of high importance values
  if(highImportanceValueChanged) {
    drawHighImportanceValues();
    triggerDrawHighImportanceValue(false);
  }
  canMainLoop();

}

void initialization1(void) {
  initRPMCount();

  deb("Second core initialized");
}

//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------

void looper1(void) {

  if(!started) {
    return;
  }

  glowPlugsMainLoop();
  fanMainLoop();
  engineHeaterMainLoop();
  heatedWindowMainLoop();

  turboMainLoop();
  stabilizeRPM();

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


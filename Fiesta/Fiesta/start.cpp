
#include "start.h"

float valueFields[F_LAST];
float reflectionValueFields[F_LAST];

static unsigned long alertsStartSecond = 0;
static bool highImportanceValueChanged = false;
static bool started = false;

Timer generalTimer;

void initialization(void) {

  //adafruit is messing up something with i2c on rbpi pin 0 & 1
  Wire.setSDA(0);
  Wire.setSCL(1);
  Wire.begin();
  pcf857_init();
  Wire.end();

  initGraphics();

  Wire.setSDA(0);
  Wire.setSCL(1);
  Wire.begin();
 
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
 
  #ifdef I2C_SCANNER
  i2cScanner();
  #endif

  init4051();

  for(int a = 0; a < F_LAST; a++) {
    valueFields[a] = 0.0;
  }
  float coolant = readCoolantTemp();
  valueFields[F_COOLANT_TEMP] = coolant;
  
  if(coolant <= TEMP_LOWEST) {
    coolant = TEMP_LOWEST;
  }
  initGlowPlugsTime(coolant);

  showLogo();

  int sec = getSeconds();
  int secDest = sec + FIESTA_INTRO_TIME;
  while(sec < secDest) {
    sec = getSeconds();
    glowPlugsMainLoop();
  }

  Adafruit_ST7735 tft = returnReference();
  tft.fillScreen(ST7735_BLACK);

  #ifdef DEBUG_SCREEN
  debugFunc();
  #else  
  initHeatedWindow();
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
  generalTimer.every(time / 2, callAtEveryHalfSecond);
  generalTimer.every(time / 4, callAtEveryHalfHalfSecond);
  generalTimer.every(time / 6, readMediumValues);
  generalTimer.every(time / 8, readHighValues);

  callAtEverySecond(NULL);
  callAtEveryHalfSecond(NULL);
  callAtEveryHalfHalfSecond(NULL);

  started = true;

  Serial.println("Fiesta MTDDI started\n");
}

void drawLowImportanceValues(void) {
  #ifndef DEBUG_SCREEN
  showFuelAmount((int)valueFields[F_FUEL], FUEL_MIN - FUEL_MAX);
  showTemperatureAmount((int)valueFields[F_COOLANT_TEMP], 120);
  showOilAmount((int)valueFields[F_OIL_TEMP], 150);
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
  showPressureAmount(valueFields[F_PRESSURE]);
  #endif
}

void seriousAlertsDrawFunctions() {
  #ifndef DEBUG_SCREEN
  drawFuelEmpty();

  #endif
}

int getEnginePercentageLoad(void) {
  return percentToGivenVal((float)( ( (valueFields[F_ENGINE_LOAD]) * 100) / PWM_RESOLUTION), 100);  
}

static bool alertBlink = false, seriousAlertBlink = false;
bool alertSwitch(void) {
  return alertBlink;
}
bool seriousAlertSwitch(void) {
  return seriousAlertBlink;
}

//timer functions

static unsigned char lowCurrentValue = 0;
bool readMediumValues(void *argument) {
  switch(lowCurrentValue) {
    case F_COOLANT_TEMP:
      valueFields[F_COOLANT_TEMP] = readCoolantTemp();
      break;
    case F_OIL_TEMP:
      valueFields[F_OIL_TEMP] = readOilTemp();
      break;
    case F_INTAKE_TEMP:
      valueFields[F_INTAKE_TEMP] = readAirTemperature();
      break;
    case F_VOLTS:
      valueFields[F_VOLTS] = readVolts();
      break;
    case F_FUEL:
      valueFields[F_FUEL] = readFuel();
      break;
    case F_EGT:
      valueFields[F_EGT] = readEGT();
      break;
  }
  if(lowCurrentValue++ > F_LAST) {
    lowCurrentValue = 0;
  }

  return true;
}

bool readHighValues(void *argument) {
  for(int a = 0; a < F_LAST; a++) {
    switch(a) {
      case F_ENGINE_LOAD:
        valueFields[a] = readThrottle();
        break;
      case F_PRESSURE:
        valueFields[a] = readBarPressure();
        break;
    }
    if(reflectionValueFields[a] != valueFields[a]) {
      reflectionValueFields[a] = valueFields[a];

      highImportanceValueChanged = true;
    }
  }

  return true;
}

bool callAtEverySecond(void *argument) {
  alertBlink = (alertBlink) ? false : true;
  digitalWrite(LED_BUILTIN, alertBlink);

#if SYSTEM_TEMP
    Serial.print("System temperature:");
    float systemTemp = analogReadTemp();
    Serial.println(systemTemp);
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
  return true; 
}

void looper(void) {

  generalTimer.tick();

  //draw changes of high importance values
  if(highImportanceValueChanged) {
    drawHighImportanceValues();
    highImportanceValueChanged = false;
  }

}

void initialization1(void) {
  initRPMCount();

  Serial.println("Second core initialized");
}

void looper1(void) {

  if(!started) {
    return;
  }

  glowPlugsMainLoop();
  fanMainLoop();
  engineHeaterMainLoop();
  heatedWindowMainLoop();

  engineMainLoop();
  stabilizeRPM();
}


void glowPlugs(bool enable) {
  pcf8574_write(O_GLOW_PLUGS, enable);
}

void glowPlugsLamp(bool enable) {
  pcf8574_write(O_GLOW_PLUGS_LAMP, enable);
}

void fan(bool enable) {
  pcf8574_write(O_FAN, enable);
}

void heater(bool enable, int level) {
  pcf8574_write(level, enable);
}

void heatedWindow(bool enable, int side) {
  pcf8574_write(side, enable);
}


//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// glow plugs
//-----------------------------------------------------------------------------

static int glowPlugsTime = 0;
static int glowPlugsLampTime = 0;
static int lastSecond = 0;

bool isGlowPlugsHeating(void) {
  return (glowPlugsTime > 0);
}

void initGlowPlugsTime(float temp) {

  if(temp < TEMP_MINIMUM_FOR_GLOW_PLUGS) {
    glowPlugsTime = int((-(temp) + 60.0) / 5.0);
    if(glowPlugsTime < 0) {
      glowPlugsTime = 0;
    }
  } else {
    glowPlugsTime = 0;
  }
  if(glowPlugsTime > 0) {
    glowPlugs(true);
    glowPlugsLamp(true);

    float divider = 3.0;
    if(temp >= 5.0) {
      divider = 8.0;
    }

    glowPlugsLampTime = int((float)glowPlugsTime / divider);

    lastSecond = getSeconds();
  }
}

void glowPlugsMainLoop(void) {
  if(glowPlugsTime >= 0) {
    if(getSeconds() != lastSecond) {
      lastSecond = getSeconds();

      if(glowPlugsTime-- <= 0) {
        glowPlugs(false);

        Serial.println("glow plugs disabled");
      }

      if(glowPlugsLampTime >= 0 && glowPlugsLampTime-- <= 0) {
        glowPlugsLamp(false);

        Serial.println("glow plugs lamp off");
      }
    }
  }  
}

//-----------------------------------------------------------------------------
// fan
//-----------------------------------------------------------------------------

static bool fanEnabled = false;
static bool lastFanStatus = false;

bool isFanEnabled(void) {
  return fanEnabled;  
}

void fanMainLoop(void) {

  float coolant = valueFields[F_COOLANT_TEMP];
  //works only if the temp. sensor is plugged
  if(coolant > TEMP_LOWEST) {

    if(fanEnabled && coolant <= TEMP_FAN_STOP) {
      fanEnabled = false;
    }

    if(!fanEnabled && coolant >= TEMP_FAN_START) {
      fanEnabled = true;
    }

  } else {
    //temp sensor read fail, fan enabled by default
    //but only if engine works
    if(valueFields[F_RPM] > RPM_MIN) {
      fanEnabled = true;
    }
  }

  if(lastFanStatus != fanEnabled) {
    fan(fanEnabled);
    lastFanStatus = fanEnabled;     

    Serial.println("fan enabled:" + fanEnabled);     
  }

}

//-----------------------------------------------------------------------------
// engine heater
//-----------------------------------------------------------------------------

static bool heaterLoEnabled = false;
static bool heaterHiEnabled = false;
static bool lastHeaterLoEnabled = false;
static bool lastHeaterHiEnabled = false;

void engineHeaterMainLoop(void) {
  float coolant = valueFields[F_COOLANT_TEMP];
  float volts = valueFields[F_VOLTS];

  if(coolant > TEMP_HEATER_STOP ||
    isFanEnabled() ||
    isGlowPlugsHeating() ||
    volts < MINIMUM_VOLTS_AMOUNT) {
    heaterLoEnabled = false;
    heaterHiEnabled = false;
  } else {

    if(coolant <= (TEMP_HEATER_STOP / 2)) {
      heaterLoEnabled = heaterHiEnabled = true;
    } else {
      heaterLoEnabled = true;
      heaterHiEnabled = false;
    }

  }

  if(lastHeaterHiEnabled != heaterHiEnabled) {
    heater(heaterHiEnabled, O_HEATER_HI);
    lastHeaterHiEnabled = heaterHiEnabled;
  }
  if(lastHeaterLoEnabled != heaterLoEnabled) {
    heater(heaterLoEnabled, O_HEATER_LO);
    lastHeaterLoEnabled = heaterLoEnabled;
  }

}

//-----------------------------------------------------------------------------
// heated window
//-----------------------------------------------------------------------------

static bool heatedWindowLEnabled = false;
static bool heatedWindowPEnabled = false;
static bool lastHeatedWindowLEnabled = false;
static bool lastHeatedWindowPEnabled = false;
static bool waitingForUnpress = false;

static int heatedWindowsOverallTimer = 0;
static int heatedWindowsSwitchTimer = 0;
static int lastHeatedWindowsSecond = 0;

void initHeatedWindow(void) {
  pinMode(HEATED_WINDOWS_PIN, INPUT_PULLUP);
}
bool isHeatedButtonPressed(void) {
  return digitalRead(HEATED_WINDOWS_PIN);
}

bool isHeatedWindowEnabled(void) {
  return (heatedWindowLEnabled || heatedWindowPEnabled);
}

static void disableHeatedWindows(void) {
  heatedWindowLEnabled = heatedWindowPEnabled = false;
  heatedWindowsOverallTimer = heatedWindowsSwitchTimer = 0;
  lastHeatedWindowsSecond = 0;
}

void heatedWindowMainLoop(void) {

  if(waitingForUnpress) {
    if(isHeatedButtonPressed()) {
      waitingForUnpress = false;
    }
    return;
  } else {

    bool pressed = false;

    if(!isHeatedButtonPressed()) {
      pressed = true;
      waitingForUnpress = true;
    }

    if(pressed) {

      if(isHeatedWindowEnabled()) {
        disableHeatedWindows();
      } else {
        heatedWindowsOverallTimer = HEATED_WINDOWS_TIME;
        heatedWindowsSwitchTimer = HEATED_WINDOWS_SWITCH_TIME;
        lastHeatedWindowsSecond = getSeconds();

        //start from the left
        heatedWindowLEnabled = true;
        heatedWindowPEnabled = false;
      }

      pressed = false;
      return;
    }

    if(isHeatedWindowEnabled()) {
      if(lastHeatedWindowsSecond != getSeconds()) {
        lastHeatedWindowsSecond = getSeconds();

        if(heatedWindowsSwitchTimer-- <= 0) {
          heatedWindowsSwitchTimer = HEATED_WINDOWS_SWITCH_TIME;
          heatedWindowLEnabled = !heatedWindowLEnabled;
          heatedWindowPEnabled = !heatedWindowPEnabled;
        }

        if(heatedWindowsOverallTimer-- <= 0) {
          disableHeatedWindows();
        }

      }
    }

    //if not enough energy, disable heated windows
    float volts = valueFields[F_VOLTS];
    if(volts < MINIMUM_VOLTS_AMOUNT) {
      disableHeatedWindows();
    }

    //execute action
    if(heatedWindowLEnabled != lastHeatedWindowLEnabled) {
      lastHeatedWindowLEnabled = heatedWindowLEnabled;
      heatedWindow(heatedWindowLEnabled, O_HEATED_WINDOW_L);
    }

    if(heatedWindowPEnabled != lastHeatedWindowPEnabled) {
      lastHeatedWindowPEnabled = heatedWindowPEnabled;
      heatedWindow(heatedWindowPEnabled, O_HEATED_WINDOW_P);
    }
  }
}

static int lastLoad = 0;

void engineMainLoop(void) {

  int load = (int)valueFields[F_ENGINE_LOAD];
  if(load != lastLoad) {
    lastLoad = load;

    valToPWM(10, load);
  }

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


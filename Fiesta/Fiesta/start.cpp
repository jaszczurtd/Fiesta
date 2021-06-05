
#include "start.h"

float valueFields[F_LAST];
float reflectionValueFields[F_LAST];

static unsigned long alertsStartSecond = 0;
static bool highImportanceValueChanged = false;

void initialization(void) {

  Wire.begin();
  pcf857_init();

  Serial.begin(9600);
 
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

  initGraphics();

  int sec = getSeconds();
  int secDest = sec + FIESTA_INTRO_TIME;
  while(sec < secDest) {
    sec = getSeconds();
    glowPlugsMainLoop();
  }

  Adafruit_ST7735 tft = returnReference();
  tft.fillScreen(ST7735_BLACK);

  #ifdef DEBUG
  debugFunc();
  #else  
  initHeatedWindow();
  initRPMCount();
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

  Serial.println("\nFiesta MTDDI started\n");
}

void drawLowImportanceValues(void) {
  #ifndef DEBUG
  showFuelAmount((int)valueFields[F_FUEL], 1023);
  showTemperatureAmount((int)valueFields[F_COOLANT_TEMP], 120);
  showOilAmount((int)valueFields[F_OIL_TEMP], 150);
  showICTemperatureAmount((unsigned char)valueFields[F_INTAKE_TEMP]);
  showEGTTemperatureAmount((int)valueFields[F_EGT]);
  showVolts(valueFields[F_VOLTS]);
  showRPMamount((int)valueFields[F_RPM]);
  #endif
}

void drawHighImportanceValues(void) {
  #ifndef DEBUG
  showEngineLoadAmount((unsigned char)valueFields[F_ENGINE_LOAD]);
  #endif
}

void drawMediumImportanceValues(void) {
  #ifndef DEBUG
  showPressureAmount(valueFields[F_PRESSURE]);
  #endif
}

static unsigned char lowReadCycles = 0;
static unsigned char lowCurrentValue = 0;
static unsigned char highReadCycles = 0;
static unsigned char mediumReadCycles = 0;

void readValues(void) {
  if(lowReadCycles++ > LOW_READ_CYCLES_AMOUNT) {
    lowReadCycles = 0;

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
  }

  if(highReadCycles++ > HIGH_READ_CYCLES_AMOUNT) {
    highReadCycles = 0;

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
  }

  readRPM();
}

void seriousAlertsDrawFunctions() {
  #ifndef DEBUG
  drawFuelEmpty();

  #endif
}

static bool draw = false, seriousAlertDraw = false;
static bool mediumDraw = false;

static bool alertBlink = false, seriousAlertBlink = false;
bool alertSwitch(void) {
  return alertBlink;
}
bool seriousAlertSwitch(void) {
  return seriousAlertBlink;
}

static long lastSec = -1, lastHalfSec = -1, lastHalfHalfSec = -1;

void looper(void) {

  long msec = millis();

  int sec = (msec % 1000 > 500);
  int halfsec = (msec % 500 > 250);
  int halfhalfsec = (msec % 250 > 125);

  if(lastHalfHalfSec != halfhalfsec) {
    lastHalfHalfSec = halfhalfsec;
    mediumDraw = true;
  }

  if(lastHalfSec != halfsec) {
    lastHalfSec = halfsec;
    seriousAlertBlink = (seriousAlertBlink) ? false : true;
    seriousAlertDraw = true;
  }

  if(lastSec != sec) {
    lastSec = sec;
    alertBlink = (alertBlink) ? false : true;
    draw = true;
  }

  //regular draw - low importance values
  if(draw) {
    drawLowImportanceValues();
    draw = false;
  }

  //draw changes of high importance values
  if(highImportanceValueChanged) {
    drawHighImportanceValues();
    highImportanceValueChanged = false;
  }

  //draw changes of medium importance values
  if(mediumDraw) {
    drawMediumImportanceValues();
    mediumDraw = false;
  }

  if(seriousAlertDraw) {
    if(alertsStartSecond <= getSeconds()) {
      seriousAlertsDrawFunctions();
    }
    seriousAlertDraw = false;
  }

  readValues();
  glowPlugsMainLoop();
  fanMainLoop();
  engineHeaterMainLoop();
  heatedWindowMainLoop();

}

#define engineCylinders 4    
#define engineCycles 4  
#define refreshInterval 750

static unsigned long previousMillis = 0;
static volatile int RPMpulses = 0;
static volatile unsigned long shortPulse = 0;
static volatile unsigned long lastPulse = 0;

void countRPM(void) {
  unsigned long now = micros();
  unsigned long nowPulse = now - lastPulse;
  
  lastPulse = now;

  if((nowPulse >> 1) > shortPulse){ 
    RPMpulses++;
    shortPulse = nowPulse; 
  } else { 
    shortPulse = nowPulse;
  }
}

void initRPMCount(void) {
  pinMode(INTERRUPT_HALL, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_HALL), countRPM, FALLING);  
}

void readRPM(void) {
  if(millis() - previousMillis > refreshInterval) {
    previousMillis = millis();

    int RPM = int(RPMpulses * (60000.0 / float(refreshInterval)) * engineCycles / engineCylinders / 2.0 ); 
    RPMpulses = 0; 
    valueFields[F_RPM] = min(99999, RPM); 
  }  
}

void glowPlugs(bool enable) {
  pcf8574(O_GLOW_PLUGS, enable);
}

void glowPlugsLamp(bool enable) {
  pcf8574(O_GLOW_PLUGS_LAMP, enable);
}

void fan(bool enable) {
  pcf8574(O_FAN, enable);
}

void heater(bool enable, int level) {
  pcf8574(level, enable);
}

void heatedWindow(bool enable, int side) {
  pcf8574(side, enable);
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
      }

      if(glowPlugsLampTime >= 0 && glowPlugsLampTime-- <= 0) {
        glowPlugsLamp(false);
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
    fanEnabled = true;
  }

  if(lastFanStatus != fanEnabled) {
    fan(fanEnabled);
    lastFanStatus = fanEnabled;          
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
  pinMode(A3, INPUT_PULLUP);
}
bool isHeatedButtonPressed(void) {
  return digitalRead(A3);
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



#ifdef DEBUG
void debugFunc(void) {

  Adafruit_ST7735 tft = returnReference();

  int x = 0;
  int y = 0;
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(x, y); y += 9;
  tft.println(glowPlugsTime);

}
#endif


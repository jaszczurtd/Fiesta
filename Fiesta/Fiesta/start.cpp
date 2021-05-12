
#include "start.h"

static int readCycles = 0;
static int currentValue = 0;

float valueFields[F_LAST];

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

  //coolant sensor failure - fan enabled in that case at start (forever)
  if(coolant < TEMP_LOWEST) {
    coolant = TEMP_LOWEST;
    fan(true);          
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

  initRPMCount();
  redrawFuel();
  redrawTemperature();
  redrawOil();
  redrawPressure();
  redrawIntercooler();
  redrawEngineLoad();
  redrawRPM();
  redrawEGT();
  
  Serial.println("\nFiesta MTDDI\n");

  #ifdef DEBUG
  tft.fillScreen(ST7735_BLACK);
  debugFunc();
  #endif

}

void drawFunctions(void) {
  #ifndef DEBUG
  showFuelAmount((int)valueFields[F_FUEL], 1024);
  showTemperatureAmount((int)valueFields[F_COOLANT_TEMP], 120);
  showOilAmount((int)valueFields[F_OIL_TEMP], 150);
  showPressureAmount(valueFields[F_PRESSURE]);
  showICTemperatureAmount((unsigned char)valueFields[F_INTAKE_TEMP]);
  showEngineLoadAmount((unsigned char)valueFields[F_ENGINE_LOAD]);
  showRPMamount((int)valueFields[F_RPM]);
  showEGTTemperatureAmount((int)valueFields[F_EGT]);
  showVolts(valueFields[F_VOLTS]);
  #endif
}

void readValues(void) {
  if(readCycles++ > READ_CYCLES_AMOUNT) {
    readCycles = 0;

    switch(currentValue) {
      case F_COOLANT_TEMP:
        valueFields[F_COOLANT_TEMP] = readCoolantTemp();
        break;
      case F_OIL_TEMP:
        valueFields[F_OIL_TEMP] = readOilTemp();
        break;
      case F_ENGINE_LOAD:
        valueFields[F_ENGINE_LOAD] = readThrottle();
        break;
      case F_INTAKE_TEMP:
        valueFields[F_INTAKE_TEMP] = readAirTemperature();
        break;
      case F_VOLTS:
        valueFields[F_VOLTS] = readVolts();
        break;
    }
    if(currentValue++ > F_LAST) {
      currentValue = 0;
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

static bool alertBlink = false, seriousAlertBlink = false;
bool alertSwitch(void) {
  return alertBlink;
}
bool seriousAlertSwitch(void) {
  return seriousAlertBlink;
}

static long lastSec = -1, lastHalfSec = -1;

void looper(void) {

  long msec = millis();

  int sec = (msec % 1000 > 500);
  int halfsec = (msec % 500 > 250);

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

  if(draw) {
    drawFunctions();
    draw = false;
  }

  if(seriousAlertDraw) {
    seriousAlertsDrawFunctions();
    seriousAlertDraw = false;
  }

  readValues();
  glowPlugsMainLoop();

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
  pinMode(4, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(4), countRPM, FALLING);  
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

void heatedGlass(bool enable, int side) {
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


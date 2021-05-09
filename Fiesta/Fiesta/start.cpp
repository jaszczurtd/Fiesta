
#include "start.h"

static int readCycles = 0;
static int currentValue = 0;

float valueFields[F_LAST];

void initialization(void) {

  Wire.begin();
  Serial.begin(9600);
 
  init4051();

  #ifdef I2C_SCANNER
  i2cScanner();
  #endif

  for(int a = 0; a < F_LAST; a++) {
    valueFields[a] = 0.0;
  }

  initRPMCount();

  initGraphics();
  redrawFuel();
  redrawTemperature();
  redrawOil();
  redrawPressure();
  redrawIntercooler();
  redrawEngineLoad();
  redrawRPM();
  redrawEGT();
  
  Serial.println("\nFiesta MTDDI");
}

void drawFunctions(void) {
  showFuelAmount((int)valueFields[F_FUEL], 1024);
  showTemperatureAmount((int)valueFields[F_COOLANT_TEMP], 120);
  showOilAmount((int)valueFields[F_OIL_TEMP], 150);
  showPressureAmount(valueFields[F_PRESSURE]);
  showICTemperatureAmount((unsigned char)valueFields[F_INTAKE_TEMP]);
  showEngineLoadAmount((unsigned char)valueFields[F_ENGINE_LOAD]);
  showRPMamount((int)valueFields[F_RPM]);
  showEGTTemperatureAmount((int)valueFields[F_EGT]);
  showVolts(valueFields[F_VOLTS]);
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
  drawFuelEmpty();

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

  pcf8574(0, alertBlink);
  pcf8574(1, seriousAlertBlink);

  if(draw) {
    drawFunctions();
    draw = false;
  }

  if(seriousAlertDraw) {
    seriousAlertsDrawFunctions();
    seriousAlertDraw = false;
  }

  readValues();

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
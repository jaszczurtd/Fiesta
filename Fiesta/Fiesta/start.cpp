
#include "start.h"

float valueFields[F_LAST];
float reflectionValueFields[F_LAST];

static unsigned long alertsStartSecond = 0;
static bool highImportanceValueChanged = false;
static bool started = false;

Timer generalTimer;
Timer importanceTimer;

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
  return percentToWidth((float)( ( (valueFields[F_ENGINE_LOAD]) * 100) / PWM_RESOLUTION), 100);  
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
  importanceTimer = timer_create_default();

  Serial.println("Second core initialized");
}

static bool shortTimeTrigger = false;
static long lastShortTime = -1;

void looper1(void) {

  if(!started) {
    return;
  }

  long msec = millis();
  int shortTime = (msec % 62 > 31);

  if(lastShortTime != shortTime) {
    lastShortTime = shortTime;
    shortTimeTrigger = true;

  }

  //Serial.print("Short trigger:");
  //Serial.println(msec);

  glowPlugsMainLoop();
  fanMainLoop();
  engineHeaterMainLoop();
  heatedWindowMainLoop();

  engineMainLoop();
  stabilizeRPM();

  if(shortTimeTrigger) {
    shortTimeTrigger = false;
  }
}


static unsigned long previousMillis = 0;
static volatile int RPMpulses = 0;
static volatile unsigned long shortPulse = 0;
static volatile unsigned long lastPulse = 0;
static long rpmAliveTime = 0;

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

  rpmAliveTime = millis() + RESET_RPM_WATCHDOG_TIME;

  if(millis() - previousMillis > RPM_REFRESH_INTERVAL) {
    previousMillis = millis();

    int RPM = int(RPMpulses * (60000.0 / float(RPM_REFRESH_INTERVAL)) * 4 / 4 / 32.0) - 100; 
    if(RPM < 0) {
      RPM = 0;
    }
    RPMpulses = 0; 

    RPM = min(99999, RPM);
    RPM = ((RPM / 10) * 10);

    valueFields[F_RPM] = RPM; 
  }  

}

void initRPMCount(void) {
  pinMode(INTERRUPT_HALL, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_HALL), countRPM, CHANGE);  
  resetRPMEngine();
}

static int currentRPMSolenoid = 0;
static long startRPMTime = 0;

static boolean tooLow = false;
static boolean tooHigh = false;
static boolean determined = false;
static boolean suckingDone = false;
static int rpmTime = 0; 

void resetRPMEngine(void) {
  rpmTime = 0;
  tooLow = tooHigh = determined = suckingDone = false;
  startRPMTime = 0;
  setMaxRPM();
}

void setMaxRPM(void) {
    currentRPMSolenoid = MAX_RPM_PWM;
    valToPWM(9, currentRPMSolenoid);
}

void stabilizeRPM(void) {

  if(rpmAliveTime < millis()) {
    rpmAliveTime = millis() + RESET_RPM_WATCHDOG_TIME;
    valueFields[F_RPM] = 0;
    resetRPMEngine();
  }

  int engineLoad = getEnginePercentageLoad();
  if(engineLoad > 5) {  //percent
    return;
  }

  int rpm = (int)valueFields[F_RPM];
  if(rpm > 0) {

    if(!suckingDone){ 
      if(startRPMTime < 1) {
        startRPMTime = millis() + 20;
        setMaxRPM();
        rpmTime = ADD_RPM_TIME_VALUE;
      }
      if(startRPMTime < millis()) {
        startRPMTime = 0;
        suckingDone = true;
      }
      return;
    }

    if(startRPMTime < 1) {
      startRPMTime = millis() + rpmTime;
    }

    if(startRPMTime < millis()) {
      startRPMTime = 0;

      if(!determined) {
        if(rpm != NOMINAL_RPM_VALUE) {
          if(rpm < NOMINAL_RPM_VALUE) {
            if(NOMINAL_RPM_VALUE - rpm > MAX_RPM_DIFFERENCE) {
              tooLow = true;
              determined = true;
              rpmTime = ADD_RPM_TIME_VALUE;
            }
          }

          if(rpm > NOMINAL_RPM_VALUE) {
            if(rpm - NOMINAL_RPM_VALUE > MAX_RPM_DIFFERENCE) {
              tooHigh = true;
              determined = true;
              rpmTime = SUB_RPM_TIME_VALUE;
            }
          }
        }
      }
      if(determined) {
        if(tooHigh) {
          currentRPMSolenoid -= 1;
          determined = tooHigh = tooLow = false;
        }
        if(tooLow) {
          currentRPMSolenoid += 1;
          determined = tooHigh = tooLow = false;
        }
      }

   }
    if(currentRPMSolenoid > MAX_RPM_PWM) {
      currentRPMSolenoid = MAX_RPM_PWM;
    }
    if(currentRPMSolenoid < MIN_RPM_PWM) {
      currentRPMSolenoid = MIN_RPM_PWM;
    }

  } else {
    currentRPMSolenoid = 0;
  }

  valToPWM(9, currentRPMSolenoid);

#if DEBUG
  char buf[128];
  snprintf(buf, sizeof(buf) - 1, "rpm:%d current:%d engineLoad:%d", rpm, currentRPMSolenoid, engineLoad);
  Serial.println(buf);
#endif
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


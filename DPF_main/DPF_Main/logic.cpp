#include "logic.h"

Timer generalTimer;
Timers logicTimer;

static bool started0 = false, started1 = false;

static int volatile state = STATE_MAIN;
static int volatile newState = STATE_MAIN;
static int volatile DPFOperation = DPF_OPERATION_IDLE;
static int volatile startMode = DPF_MODE_START_NONE;

static bool ivert = false;
static bool firstColdStart = false;
static int coldInjectionsCounter = 0;

bool callAtEverySecond(void *argument);
bool displayUpdate(void *argument);
void theNormalFlow(void);
void theColdFlow(void);
void stopDPF(void);
void startDPF(int mode);
bool checkFirstColdStartConditions(void *argument);
void checkAutomaticStartConditions(void);

static bool leftP = false, rightP = false;

void initialization(void) {
    Serial.begin(9600);

    bool rebooted = false;
    if (watchdog_caused_reboot()) {
      rebooted = true;
      deb("Rebooted by Watchdog!\n");
    } else {
      deb("Clean boot\n");
    }

    pinMode(LED_BUILTIN, OUTPUT);

    watchdog_enable(WATCHDOG_TIME, false);

    displayInit();
    canInit();
    hardwareInit();

    if(rebooted) {
      stopDPF();
    }

    generalTimer = timer_create_default();
    
    generalTimer.every(500, callAtHalfSecond);
    generalTimer.every(SECS(1), callAtEverySecond);
    generalTimer.every(400, readPeripherals);
    generalTimer.every(CAN_MAIN_LOOP_READ_INTERVAL, canMainLoop);  
    generalTimer.every(CAN_CHECK_CONNECTION, canCheckConnection);  
#ifdef COLD_START_SUPPORTED    
    generalTimer.in(SECS(CAN_CHECK_COLD_START_CONDITIONS), checkFirstColdStartConditions);
#endif
    generalTimer.every(25, displayUpdate);

    readPeripherals(NULL);
    canMainLoop(NULL);
    canCheckConnection(NULL);
    displayUpdate(NULL);

    started0 = true;
}

bool isEnvironmentStarted(void) {
  return started0 && started1;
}

void looper(void) {
    watchdog_update();

    if(!isEnvironmentStarted()) {
        return;
    }
    generalTimer.tick();
}

void showECUEngineValues(void) {
  if(isEcuConnected()) {
    quickDisplay(3, M_WHOLE, "Engine load:%d%%", int(valueFields[F_ENGINE_LOAD]));
    quickDisplay(4, M_WHOLE, "Coolant temp:%dC", int(valueFields[F_COOLANT_TEMP]));
    quickDisplay(5, M_WHOLE, "RPM:%d EGT:%dC", int(valueFields[F_RPM]), 
      int(valueFields[F_EGT]));
  } else {
    clearLine(3, M_WHOLE);
    quickDisplay(4, M_WHOLE, "ECU is not connected");
    clearLine(5, M_WHOLE);
  }
}

void showDPFValues(void) {
  int hi, lo;

  floatToDec(valueFields[F_DPF_PRESSURE], &hi, &lo);
  quickDisplay(1, M_WHOLE, "DPF pressure:%d.%d BAR", hi, lo);

  int temp = int(valueFields[F_DPF_TEMP]);
  if(temp > MAX_DPF_TEMP) {
    quickDisplay(2, M_WHOLE, "DPF temp ERROR");
  } else {
    quickDisplay(2, M_WHOLE, "DPF temp:%dC", temp);
  }
}

void displayOperatingStatus(void) {
  char status[32];
  memset(status, 0, sizeof(status));
  snprintf(status, sizeof(status) -1,  "M:%d %s / %s",
                    startMode,
                    (DPFOperation & DPF_OPERATION_HEATING_START) ? "HEATING" : "NO_HEATING",
                    (DPFOperation & DPF_OPERATION_INJECT_START) ? "INJECTION" : "DRY");

  quickDisplay(0, M_WHOLE, status);
}

bool displayUpdate(void *argument) {

  int hi, lo;

  if(newState != state) {
    clearDisplay();
    state = newState;    
  }

  switch(state) {
    case STATE_MAIN:
      displayOptions("START", NULL);

      floatToDec(valueFields[F_VOLTS], &hi, &lo);
      quickDisplay(0, M_WHOLE, "Power supply:%d.%dV", hi, lo);

      showDPFValues();
      showECUEngineValues();
      break;    

    case STATE_OPERATING:
      displayOptions("STOP", NULL);

      displayOperatingStatus();
      showDPFValues();
      showECUEngineValues();
      break;

    case STATE_QUESTION:     
      displayScreenFrom("Are you sure you", "want to start the", "procedure?", NULL);
      displayOptions("NO", "YES");
      break;
 
    case STATE_ERROR_NOT_CONNECTED:
      displayScreenFrom("Can't start. ECU", "is not responding.", NULL);
      displayOptions("BACK", NULL);
      break;

    case STATE_ERROR_NO_CONDITIONS:
      displayScreenFrom("Can't start. Engine", "conditions not met.", NULL);
      displayOptions("BACK", NULL);
      break;

    default:
      clearDisplay();
      break;
  }

  return true;
}

void stopDPF(void) {
  DPFOperation = DPF_OPERATION_IDLE;
  logicTimer.abort();

  //just to be sure that heater and valves are OFF
  enableHeater(false);
  enableValves(false);

  newState = STATE_MAIN;
  startMode = DPF_MODE_START_NONE;
}

void startDPF(int mode) {

  deb("start DPF with mode: %d", mode);

  startMode = mode;
  switch(startMode) {

#ifdef COLD_START_SUPPORTED    
    case DPF_MODE_START_COLD:
      newState = STATE_OPERATING;
      logicTimer.begin(theColdFlow, SECS(HEATER_TIME_BEFORE_INJECT));
      DPFOperation |= DPF_OPERATION_HEATING_START;
      break;
#endif

    case DPF_MODE_START_NORMAL:
      firstColdStart = true;
      newState = STATE_OPERATING;
      logicTimer.begin(theNormalFlow, SECS(HEATER_TIME_BEFORE_INJECT));
      DPFOperation |= DPF_OPERATION_HEATING_START;
      break;

    case DPF_MODE_START_NONE:
      stopDPF();
      break;

    default:
      deb("unknown start mode");
      break;
  }
}

bool isDPFOperating(void) {
  return state == STATE_OPERATING;
}

void performLogic(void) {

  while(!digitalRead(S_LEFT)) {
    leftP = true;
    delay(5);
  }
  while(!digitalRead(S_RIGHT)) {
    rightP = true;
    delay(5);
  }

  switch(state) {
    case STATE_MAIN:
      if(leftP) {
        if(isEcuConnected()) {
          if(valueFields[F_RPM] < MINIMUM_RPM ||
            valueFields[F_VOLTS] < MINIMUM_VOLTS_TO_OPERATE) {
              newState = STATE_ERROR_NO_CONDITIONS;
            } else {
              newState = STATE_QUESTION;
            }          
        } else {
          newState = STATE_ERROR_NOT_CONNECTED;
        }
      }
      break;

    case STATE_ERROR_NO_CONDITIONS:
    case STATE_ERROR_NOT_CONNECTED:
      if(leftP) {
        newState = STATE_MAIN;
      }
      break;
      
    case STATE_QUESTION:
      if(leftP) {
        newState = STATE_MAIN;
      }
      if(rightP) {
        startDPF(DPF_MODE_START_NORMAL);
      }        
      break;

    case STATE_OPERATING:
      if(leftP) {
        stopDPF();        
      }
      break;
  }

  checkAutomaticStartConditions();

  logicTimer.tick();

  enableHeater(DPFOperation & DPF_OPERATION_HEATING_START);
  enableValves(DPFOperation & DPF_OPERATION_INJECT_START);

  leftP = rightP = false;
}

void manageDPFOperation(void) {
  if(DPFOperation & DPF_OPERATION_HEATING_START) {
    DPFOperation ^= DPF_OPERATION_INJECT_START;

    uint32_t time;  
    if(DPFOperation & DPF_OPERATION_INJECT_START) {
      time = FUEL_INJECT_TIME;
    } else {
      time = FUEL_INJECT_IDLE;
    }

    logicTimer.time(time);
  }
}

//---------------- for cold start mode -------------------
bool coldStartConditionsStillExists(void) {
  if(int(valueFields[F_COOLANT_TEMP]) <= TEMP_COLD_ENGINE &&
    valueFields[F_RPM] > RPM_MIN &&
    valueFields[F_DPF_TEMP] < MIN_DPF_TEMP) {
      return true;
  }
  return false;
}

bool checkFirstColdStartConditions(void *argument) {
  deb("check for cold start conditions");
  
  if(!firstColdStart) {
    if(state == STATE_OPERATING) {
      return false;
    }
    if(valueFields[F_DPF_TEMP] >= STOP_DPF_TEMP) {
      return false;
    }
    if(!isEcuConnected()) {
      return false;
    }
    deb("initial conditions have been met");

    if(coldStartConditionsStillExists()) {
      startDPF(DPF_MODE_START_COLD);
    } else {
      deb("cold start conditions not met");
    }
    firstColdStart = true;
  }

  return false;
}

void theColdFlow(void) {
  if(valueFields[F_DPF_TEMP] >= STOP_DPF_TEMP) {
    stopDPF();
    return;
  }

  if(!isDPFOperating()) {
    return;    
  }

  if(coldInjectionsCounter >= (COLD_INJECTIONS_AMOUNT * 2)) {
    stopDPF();
    return;
  }

  coldInjectionsCounter++;
  manageDPFOperation();  
}

//---------------- for normal start mode ---------------------

void theNormalFlow(void) {

  if(valueFields[F_DPF_TEMP] >= STOP_DPF_TEMP) {
    stopDPF();
    return;
  }

  if(valueFields[F_RPM] > STOP_DPF_RPM &&
    valueFields[F_DPF_PRESSURE] < STOP_DPF_PRESSURE) {
    
    stopDPF();
    return;
  }

  if(!isDPFOperating()) {
    return;    
  }
  manageDPFOperation();
}

//---------------- automatic start ---------------------------

void checkAutomaticStartConditions(void) {
  if(state == STATE_OPERATING) {
    return;    
  }
  if(valueFields[F_DPF_TEMP] >= STOP_DPF_TEMP) {
    return;
  }
  if(valueFields[F_RPM] < START_DPF_RPM || 
    valueFields[F_DPF_PRESSURE] < START_DPF_PRESSURE) {
      return;
    }

  startDPF(DPF_MODE_START_NORMAL);
}

bool callAtEverySecond(void *argument) {
  ivert = !ivert;
  digitalWrite(LED_BUILTIN, ivert);
  return true;
}

void initialization1(void) {
  started1 = true;
}

void looper1(void) {

    if(!isEnvironmentStarted()) {
        return;
    }

    performLogic();
}


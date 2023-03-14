#include "api/Common.h"
#include "logic.h"

Timer generalTimer;
Timers logicTimer;

static bool started0 = false, started1 = false;
static int state = STATE_MAIN;
static int newState = STATE_MAIN;

bool callAtEverySecond(void *argument);
bool displayUpdate(void *argument);

static bool leftP = false, rightP = false;

void initialization(void) {
    Serial.begin(9600);

    if (watchdog_caused_reboot()) {
        deb("Rebooted by Watchdog!\n");
    } else {
        deb("Clean boot\n");
    }

    pinMode(LED_BUILTIN, OUTPUT);

    watchdog_enable(WATCHDOG_TIME, false);

    displayInit();
    canInit();
    hardwareInit();

    generalTimer = timer_create_default();
    
    generalTimer.every(500, callAtHalfSecond);
    generalTimer.every(1000, callAtEverySecond);
    generalTimer.every(400, readPeripherals);
    generalTimer.every(CAN_MAIN_LOOP_READ_INTERVAL, canMainLoop);  
    generalTimer.every(CAN_CHECK_CONNECTION, canCheckConnection);  
    generalTimer.every(100, displayUpdate);

    readPeripherals(NULL);
    canCheckConnection(NULL);
    displayUpdate(NULL);

    started0 = true;
}

bool isStarted(void) {
  return started0 && started1;
}

void looper(void) {
    watchdog_update();

    if(!isStarted()) {
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
        newState = STATE_OPERATING;
      }        
      break;

    case STATE_OPERATING:
      if(leftP) {
        newState = STATE_MAIN;
        delay(1000);
        enableHeater(false);
        enableValves(false);
      }
      break;
  }

  leftP = rightP = false;
}

static bool ivert = false;
bool callAtEverySecond(void *argument) {
    ivert = !ivert;
    digitalWrite(LED_BUILTIN, ivert);
    return true;
}

void initialization1(void) {
  started1 = true;
}

void looper1(void) {

    if(!isStarted()) {
        return;
    }

    performLogic();
}


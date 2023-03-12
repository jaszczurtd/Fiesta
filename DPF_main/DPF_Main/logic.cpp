#include "api/Common.h"
#include "logic.h"

Timer generalTimer;

static bool started0 = false, started1 = false;
static int state = STATE_MAIN;
static int newState = STATE_MAIN;

bool callAtEverySecond(void *argument);
bool displayUpdate(void *argument);

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
    readPeripherals(NULL);

    generalTimer = timer_create_default();
    
    generalTimer.every(500, callAtHalfSecond);
    generalTimer.every(1000, callAtEverySecond);
    generalTimer.every(400, readPeripherals);
    generalTimer.every(CAN_MAIN_LOOP_READ_INTERVAL, canMainLoop);  
    generalTimer.every(CAN_CHECK_CONNECTION, canCheckConnection);  
    generalTimer.every(100, displayUpdate);

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

bool displayUpdate(void *argument) {

  int hi, lo;

  if(newState != state) {
    clearDisplay();
    state = newState;    
  }

  switch(state) {
    case STATE_MAIN: {
      displayOptions("START", NULL);

      floatToDec(valueFields[F_VOLTS], &hi, &lo);
      quickDisplay(0, M_WHOLE, "Power supply:%d.%dV", hi, lo);
      floatToDec(valueFields[F_DPF_PRESSURE], &hi, &lo);
      quickDisplay(1, M_WHOLE, "DPF pressure:%d.%d BAR", hi, lo);

      int temp = int(valueFields[F_DPF_TEMP]);
      if(temp > MAX_DPF_TEMP) {
        quickDisplay(2, M_WHOLE, "DPF temp ERROR");
      } else {
        quickDisplay(2, M_WHOLE, "DPF temp:%dC", temp);
      }

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

      break;    
    }

    case STATE_QUESTION: {        
      displayScreenFrom("Are you sure you", "want to start the", "procedure?", NULL);
      displayOptions("YES", "NO");
      break;
    }

    case STATE_ERROR_NOT_CONNECTED:
      displayScreenFrom("Can't start. ECU", "is not responding.", NULL);
      displayOptions("BACK", NULL);
      break;

    default:
      clearDisplay();
      break;
  }

  return true;
}

static bool leftP = false, rightP = false;
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
          newState = STATE_QUESTION;
        } else {
          newState = STATE_ERROR_NOT_CONNECTED;
        }
      }
      break;
    
    case STATE_ERROR_NOT_CONNECTED:
      if(leftP) {
        newState = STATE_MAIN;
      }
      break;

    case STATE_QUESTION:
      if(rightP) {
        newState = STATE_MAIN;
      }
      break;
  }

  leftP = rightP = false;

  //digitalWrite(VALVES, !digitalRead(S_LEFT));
  //digitalWrite(HEATER, !digitalRead(S_RIGHT));

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


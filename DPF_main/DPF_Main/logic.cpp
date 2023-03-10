#include "api/Common.h"
#include "logic.h"

Timer generalTimer;

static bool started0 = false;
static bool started1 = false;

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
    generalTimer.every(100, displayUpdate);

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
  floatToDec(valueFields[F_VOLTS], &hi, &lo);
  quickDisplay(0, "Power supply:%d.%dV", hi, lo);
  floatToDec(valueFields[F_DPF_PRESSURE], &hi, &lo);
  quickDisplay(1, "DPF pressure:%d.%d BAR", hi, lo);

  int temp = int(valueFields[F_DPF_TEMP]);
  if(temp > MAX_DPF_TEMP) {
    quickDisplay(2, "DPF temp ERROR");
  } else {
    quickDisplay(2, "DPF temp:%dC", temp);
  }

  quickDisplay(3, "Engine load:%d%%", int(valueFields[F_ENGINE_LOAD]));
  quickDisplay(4, "Coolant temp:%dC", int(valueFields[F_COOLANT_TEMP]));
  quickDisplay(5, "Engine RPM:%d", int(valueFields[F_RPM]));
  quickDisplay(6, "Engine EGT:%dC", int(valueFields[F_EGT]));

  return true;
}

bool state = false;
bool callAtEverySecond(void *argument) {
    state = !state;
    digitalWrite(LED_BUILTIN, state);
    return true;
}

void initialization1(void) {
  started1 = true;
}

void looper1(void) {

    if(!isStarted()) {
        return;
    }

    digitalWrite(VALVES, !digitalRead(S_LEFT));
    digitalWrite(HEATER, !digitalRead(S_RIGHT));

}


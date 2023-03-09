#include "api/Common.h"
#include "logic.h"

Timer generalTimer;

static bool started = false;

bool callAtEverySecond(void *argument);

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
    
    generalTimer.every(400, readPeripherals);
    generalTimer.every(500, callAtHalfSecond);
    generalTimer.every(1000, callAtEverySecond);
    
    started = true;
}

void looper(void) {
    watchdog_update();

    if(!started) {
        return;
    }
    generalTimer.tick();

    canMainLoop();

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

    digitalWrite(VALVES, !digitalRead(S_LEFT));
    digitalWrite(HEATER, !digitalRead(S_RIGHT));
}

bool state = false;
bool callAtEverySecond(void *argument) {

    state = !state;

    //digitalWrite(HEATER, state);
    //digitalWrite(VALVES, state);

    return true;
}
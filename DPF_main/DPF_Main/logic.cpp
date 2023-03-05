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

    watchdog_enable(1000, false);

    displayInit();
    canInit();
    hardwareInit();

    generalTimer = timer_create_default();

    generalTimer.every(500, callAtSomeTime);
    generalTimer.every(1000, callAtEverySecond);
    
    started = true;
}

void looper(void) {
    if(!started) {
        return;
    }
    generalTimer.tick();

    canMainLoop();

    digitalWrite(VALVES, !digitalRead(S_LEFT));
    digitalWrite(HEATER, !digitalRead(S_RIGHT));
    
    double v = analogRead(VOLTS) * (4.75/1023);

    deb("presssure: %d termo: %d v:%f\n", 
      analogRead(PRESSURE), analogRead(THERMOC), v);

    watchdog_update();
}

bool state = false;
bool callAtEverySecond(void *argument) {

    state = !state;

    //digitalWrite(HEATER, state);
    //digitalWrite(VALVES, state);

    return true;
}
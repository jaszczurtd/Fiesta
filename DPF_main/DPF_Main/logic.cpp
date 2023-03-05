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
    watchdog_update();
}

bool state = false;
bool callAtEverySecond(void *argument) {

    state = !state;

    digitalWrite(VALVES, state);

    return true;
}
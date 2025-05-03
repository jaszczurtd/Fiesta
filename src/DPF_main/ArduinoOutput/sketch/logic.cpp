#line 1 "C:\\development\\projects_git\\fiesta\\DPF_main\\DPF_Main\\logic.cpp"
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

    watchdog_enable(3000, false);

    displayInit();
    canInit();
    hardwareInit();
    readPeripherals(NULL);

    generalTimer = timer_create_default();
    
    generalTimer.every(100, readPeripherals);
    generalTimer.every(500, callAtHalfSecond);
    generalTimer.every(1000, callAtEverySecond);
    
    started = true;
}

void looper(void) {
    if(!started) {
        return;
    }
    generalTimer.tick();

    canMainLoop();

    int hi, lo;
    floatToDec(valueFields[F_VOLTS], &hi, &lo);

    quickDisplay(0, "(V): %d.%d", hi, lo);

//    quickDisplay(3, "vals: %d %d", frameNumber, throttle);


//    digitalWrite(VALVES, !digitalRead(S_LEFT));
//    digitalWrite(HEATER, !digitalRead(S_RIGHT));
    
//    float v = getAverageValueFrom(VOLTS) * (4.75/1023);

//    deb("presssure: %d termo: %d v:%f\n", 
//      getAverageValueFrom(PRESSURE), getAverageValueFrom(THERMOC), v);

    watchdog_update();
}

bool state = false;
bool callAtEverySecond(void *argument) {

    state = !state;

    //digitalWrite(HEATER, state);
    //digitalWrite(VALVES, state);

    return true;
}
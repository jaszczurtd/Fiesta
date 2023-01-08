
#ifndef T_START
#define T_START

#include <Wire.h>
#include <arduino-timer.h>
#include <SPI.h>
#include <mcp_can.h>

#include "graphics.h"
#include "utils.h"
#include "config.h"
#include "rpm.h"
#include "turbo.h"
#include "engineFan.h"
#include "engineHeater.h"
#include "heatedWindshield.h"
#include "glowPlugs.h"
#include "engineFuel.h"
#include "can.h"

#define deb(format, ...) { \
    char buffer[100]; \
    memset (buffer, 0, sizeof(buffer)); \
    snprintf(buffer, sizeof(buffer) - 1, format, ## __VA_ARGS__); \
    Serial.println(buffer); \
    }

extern float valueFields[];

#define O_GLOW_PLUGS 0
#define O_FAN 1
#define O_HEATER_HI 2
#define O_HEATER_LO 3
#define O_GLOW_PLUGS_LAMP 4
#define O_HEATED_WINDOW_L 5
#define O_HEATED_WINDOW_P 6

#define INTERRUPT_HALL 7    //cpu pio number

//cpu pio numbers
#define A_4051 11
#define B_4051 12
#define C_4051 13

void drawMediumImportanceValues(void);
void drawHighImportanceValues(void);
void drawLowImportanceValues(void);

bool callAtEverySecond(void *argument);
bool callAtEveryHalfSecond(void *argument);
bool callAtEveryHalfHalfSecond(void *argument);

bool readMediumValues(void *argument);
bool readHighValues(void *argument);

void initialization(void);
void initialization1(void);
void looper(void);
void looper1(void);
bool seriousAlertSwitch(void);
bool alertSwitch(void);
int getEnginePercentageLoad(void);

#ifdef DEBUG
void debugFunc(void);
#endif

#endif

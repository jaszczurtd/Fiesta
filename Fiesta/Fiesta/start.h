
#ifndef T_START
#define T_START

#include <Wire.h>
#include <arduino-timer.h>

#include "graphics.h"
#include "utils.h"
#include "config.h"

extern float valueFields[];

#define O_GLOW_PLUGS 0
#define O_FAN 1
#define O_HEATER_HI 2
#define O_HEATER_LO 3
#define O_GLOW_PLUGS_LAMP 4
#define O_HEATED_WINDOW_L 5
#define O_HEATED_WINDOW_P 6

#define F_FUEL 0
#define F_COOLANT_TEMP 1
#define F_OIL_TEMP 2
#define F_INTAKE_TEMP 3
#define F_ENGINE_LOAD 4
#define F_RPM 5
#define F_EGT 6
#define F_PRESSURE 7
#define F_VOLTS 8
#define F_LAST 9

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
void setMaxRPM(void);
void resetRPMEngine(void);
bool seriousAlertSwitch(void);
bool alertSwitch(void);
void initRPMCount(void);
int getEnginePercentageLoad(void);

void glowPlugs(bool enable);
void glowPlugsLamp(bool enable);
void fan(bool enable);
void heater(bool enable, int level);
void heatedWindow(bool enable, int side);
bool isGlowPlugsHeating(void);

#ifdef DEBUG
void debugFunc(void);
#endif

void initGlowPlugsTime(float temp);
void glowPlugsMainLoop(void);
bool isFanEnabled(void);
void fanMainLoop(void);
void engineHeaterMainLoop(void);
void heatedWindowMainLoop(void);
bool isHeatedWindowEnabled(void);
void initHeatedWindow(void);
bool isHeatedButtonPressed(void);
void engineMainLoop(void);
void stabilizeRPM(void);

#endif

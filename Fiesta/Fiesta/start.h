
#ifndef T_START
#define T_START

#include "graphics.h"
#include "indicators.h"
#include "utils.h"
#include <Wire.h>

#define FIESTA_INTRO_TIME 2 //in seconds

extern float valueFields[];

#define O_GLOW_PLUGS 0
#define O_FAN 1
#define O_HEATER_HI 2
#define O_HEATER_LO 3
#define O_GLOW_PLUGS_LAMP 4
#define O_HEATED_GLASS_L 5
#define O_HEATED_GLASS_P 6

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

#define READ_CYCLES_AMOUNT 20

#define TEMP_OIL_MAX 155
#define TEMP_OIL_OK_HI 115

#define TEMP_MAX 120
#define TEMP_MIN 45

#define TEMP_OK_LO 70
#define TEMP_OK_HI 105

#define TEMP_LOWEST -100
#define TEMP_HIGHEST 170

void initialization(void);
void looper(void);
bool seriousAlertSwitch(void);
bool alertSwitch(void);
void initRPMCount(void);
void readRPM(void);

void glowPlugs(bool enable);
void glowPlugsLamp(bool enable);
void fan(bool enable);
void heater(bool enable, int level);
void heatedGlass(bool enable, int side);

#endif

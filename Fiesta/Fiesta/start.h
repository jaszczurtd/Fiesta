
#ifndef T_START
#define T_START

#include "graphics.h"
#include "indicators.h"
#include "utils.h"
#include <Wire.h>

#define F_FUEL 0
#define F_COOLANT_TEMP 1
#define F_OIL_TEMP 2
#define F_INTAKE_TEMP 3
#define F_ENGINE_LOAD 4
#define F_RPM 5
#define F_EGT 6
#define F_PRESSURE 7
#define F_LAST 8

#define READ_CYCLES_AMOUNT 20

void initialization(void);
void looper(void);
bool seriousAlertSwitch(void);
bool alertSwitch(void);

#endif

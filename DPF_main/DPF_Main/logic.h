#ifndef LOGIC_F_0
#define LOGIC_F_0

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <hardware/watchdog.h>
#include <tools.h>
#include <Timers.h>

#include "api/Common.h"

#include "can.h"
#include "peripherals.h"

#define WATCHDOG_TIME 3000
#define DISPLAY_INIT_MAX_TIME 500

#define MINIMUM_VOLTS_TO_OPERATE 10.0
#define MINIMUM_VOLTS 6.0
#define MINIMUM_RPM 700
#define MAX_DPF_TEMP 1100

extern float valueFields[];

void initialization(void);
void looper(void);
void initialization1(void);
void looper1(void);

#define STATE_MAIN                0
#define STATE_ASK                 1
#define STATE_OPERATE             2
#define STATE_ERROR               3
#define STATE_QUESTION            4
#define STATE_ERROR_NOT_CONNECTED 5
#define STATE_ERROR_NO_CONDITIONS 6
#define STATE_OPERATING           7

//miliseconds
#define FUEL_INJECT_TIME    250
#define FUEL_INJECT_IDLE    2000

//seconds
#define HEATER_TIME_BEFORE_INJECT 15

#define DPF_IDLE            1<<0
#define DPF_HEATING_START   1<<1
#define DPF_HEATING_END     1<<2
#define DPF_INJECT_START    1<<3
#define DPF_INJECT_END      1<<4

//conditions to stop DPF regeneration

#define STOP_DPF_TEMP       600   //temperature
#define START_DPF_RPM       1500
#define STOP_DPF_RPM        3500  //engine rpm  
#define STOP_DPF_PRESSURE   0.5   //bar pressure
#define START_DPF_PRESSURE  0.85  //bar pressure

#endif

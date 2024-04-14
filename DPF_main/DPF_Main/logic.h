#ifndef LOGIC_F_0
#define LOGIC_F_0

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <hardware/watchdog.h>
#include <tools.h>
#include <SmartTimers.h>
#include <multicoreWatchdog.h>

#include "api/Common.h"

#include "can.h"
#include "peripherals.h"

#define COLD_START_SUPPORTED

#define WATCHDOG_TIME 3000
#define DISPLAY_INIT_MAX_TIME 500

#define KEY_DEBOUNCE_T 5

#define MINIMUM_VOLTS_TO_OPERATE 10.0
#define MINIMUM_VOLTS 6.0
#define MINIMUM_RPM 700
#define MAX_DPF_TEMP 1100
#define MIN_DPF_TEMP 350

extern float valueFields[];

void initialization(void);
void looper(void);
void initialization1(void);
void looper1(void);
bool isDPFOperating(void);

#define STATE_MAIN                0
#define STATE_ASK                 1
#define STATE_OPERATE             2
#define STATE_ERROR               3
#define STATE_QUESTION            4
#define STATE_QUESTION_REALLY     5
#define STATE_ERROR_NOT_CONNECTED 6
#define STATE_ERROR_NO_CONDITIONS 7
#define STATE_OPERATING           8

//miliseconds
#define FUEL_INJECT_TIME    250
#define FUEL_INJECT_IDLE    4000

#define DPF_MODE_START_NONE   0
#define DPF_MODE_START_COLD   1
#define DPF_MODE_START_NORMAL 2

#define DPF_OPERATION_IDLE            1<<0
#define DPF_OPERATION_HEATING_START   1<<1
#define DPF_OPERATION_HEATING_END     1<<2
#define DPF_OPERATION_INJECT_START    1<<3
#define DPF_OPERATION_INJECT_END      1<<4

//seconds: preheating injector before start injection
#define HEATER_TIME_BEFORE_INJECT 22

#define COLD_INJECTIONS_AMOUNT 4

//conditions to start/stop DPF regeneration

#define STOP_DPF_TEMP       600   //temperature
#define START_DPF_RPM       1500
#define STOP_DPF_RPM        3000  //engine rpm  
#define STOP_DPF_PRESSURE   0.5   //bar pressure
#define START_DPF_PRESSURE  0.85  //bar pressure

#endif

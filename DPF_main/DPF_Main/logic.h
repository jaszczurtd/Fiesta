#ifndef LOGIC_F_0
#define LOGIC_F_0

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <hardware/watchdog.h>

#include "can.h"
#include "peripherals.h"
#include "tools.h"

#define WATCHDOG_TIME 3000
#define DISPLAY_INIT_MAX_TIME 500

#define MINIMUM_VOLTS 6.0
#define MAX_DPF_TEMP 1200

extern float valueFields[];

void initialization(void);
void looper(void);
void initialization1(void);
void looper1(void);

#endif

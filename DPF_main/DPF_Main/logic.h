#ifndef LOGIC_F_0
#define LOGIC_F_0

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <hardware/watchdog.h>

#include "can.h"
#include "peripherals.h"
#include "tools.h"

extern float valueFields[];

void initialization(void);
void looper(void);

#endif

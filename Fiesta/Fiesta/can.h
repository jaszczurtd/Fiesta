#ifndef T_CAN
#define T_CAN

#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>

#include "config.h"
#include "utils.h"
#include "rpm.h"

#define CAN0_GPIO 17
#define CAN0_INT 15
#define DPF_CAN_ID 0x123

void canMainLoop(void);
void canInit(void);

#endif


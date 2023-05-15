
#ifndef T_OBD
#define T_OBD

#include <mcp_can.h>
#include <SPI.h>
#include <tools.h>

#include "start.h"
#include "hardwareConfig.h"
#include "sensors.h"

//Default reply ECU ID
#define REPLY_ID 0x7E8 

void obdInit(int retries);
void obdLoop(void);

#endif

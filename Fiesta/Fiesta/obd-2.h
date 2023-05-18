
#ifndef T_OBD
#define T_OBD

#include <mcp_can.h>
#include <SPI.h>
#include <tools.h>

#include "start.h"
#include "hardwareConfig.h"
#include "sensors.h"
#include "tests.h"

//Default reply ECU ID
#define REPLY_ID 0x7E8 

#define SHOW_CURRENT_DATA 1
#define SHOW_STORED_DIAGNOSTIC_TROUBLE_CODES 3
#define CLEAR_DIAGNOSTIC_TROUBLE_CODES_AND_STORED_VALUES 4
#define REQUEST_VEHICLE_INFORMATION 9

void obdInit(int retries);
void obdLoop(void);

#endif

#ifndef T_GPS
#define T_GPS

#include <libConfig.h>
#include <tools.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

void initGPS(void);
void initGPSDateAndTime(void);
bool getGPSData(void *arg);
float getCurrentCarSpeed(void);
const char *getGPSDate(void);
const char *getGPSTime(void);
bool isGPSAvailable(void);

#endif

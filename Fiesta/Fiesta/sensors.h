
#ifndef T_SENSORS
#define T_SENSORS

#include <libConfig.h>
#include "config.h"

#include <Wire.h>
#include <tools.h>
#ifdef PICO_W
#include <WiFi.h>
#endif

#include "start.h"
#include "hardwareConfig.h"
#include "tests.h"

//in miliseconds, print values into serial
#define DEBUG_UPDATE 3 * 1000

#define GPS_TIME_DATE_BUFFER_SIZE 16

extern volatile float valueFields[];

void initI2C(void);
void initSPI(void);
void initSensors(void);
void initBasicPIO(void);
//readers
float readCoolantTemp(void);
float readOilTemp(void);
float readThrottle(void);
float readAirTemperature(void);
float readVolts(void);
float readBarPressure(void);
float readEGT(void);
int getThrottlePercentage(int currentVal);
int getPercentageEngineLoad(void);

void pcf8574_init(void);
void pcf8574_write(unsigned char pin, bool value);
bool pcf8574_read(unsigned char pin);
void valToPWM(unsigned char pin, int val);

int getEnginePercentageThrottle(void);
bool readMediumValues(void *argument);
bool readHighValues(void *argument);

void init4051(void);
void set4051ActivePin(unsigned char pin);

bool isDPFRegenerating(void);

bool updateValsForDebug(void *arg);

#endif
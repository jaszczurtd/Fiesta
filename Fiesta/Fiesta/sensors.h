
#ifndef T_SENSORS
#define T_SENSORS

#include <Wire.h>
#include <tools.h>

#include <SoftwareSerial.h>
#include <TinyGPS++.h>

#include "config.h"
#include "start.h"

#define DEBUG_UPDATE 3

extern float valueFields[];

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

void pcf8574_init(void);
void pcf8574_write(unsigned char pin, bool value);
void valToPWM(unsigned char pin, int val);

int getEnginePercentageLoad(void);
bool readMediumValues(void *argument);
bool readHighValues(void *argument);

void init4051(void);
void set4051ActivePin(unsigned char pin);

bool isDPFRegenerating(void);

bool getGPSData(void *arg);
float getCurrentCarSpeed(void);
bool isGPSAvailable(void);

bool updateValsForDebug(void *arg);

#endif
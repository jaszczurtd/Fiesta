
#ifndef T_SENSORS
#define T_SENSORS

#include <libConfig.h>
#include "config.h"

#include <tools.h>
#include "canDefinitions.h"

#include "hardwareConfig.h"
#include "tests.h"
#include "dtcManager.h"

#ifdef __cplusplus
extern "C" {
#endif

//in miliseconds, print values into serial
#define DEBUG_UPDATE 3 * 1000

#define GPS_TIME_DATE_BUFFER_SIZE 16

void  setGlobalValue(int idx, float val);
float getGlobalValue(int idx);

void initI2C(void);
void initSPI(void);
void initSensors(void);
void initBasicPIO(void);
//readers
float readCoolantTemp(void);
float readOilTemp(void);
int readThrottle(void);
float readAirTemperature(void);
float readBarPressure(void);
int readEGT(void);
int getThrottlePercentage(void);
int getPercentageEngineLoad(void);

void pcf8574_init(void);
void pcf8574_write(unsigned char pin, bool value);
bool pcf8574_read(unsigned char pin);
void valToPWM(unsigned char pin, int val);

void readMediumValues(void);
void readHighValues(void);

void init4051(void);
void set4051ActivePin(unsigned char pin);

bool isDPFRegenerating(void);

void updateValsForDebug(void);
void pwm_init(void);

float getSystemSupplyVoltage(void);
int getVP37Adjustometer(void);
float getVP37FuelTemperature(void);

#ifdef __cplusplus
}
#endif

#endif

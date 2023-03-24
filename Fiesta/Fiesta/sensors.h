#ifndef T_SENSORS
#define T_SENSORS

#include <Wire.h>
#include <tools.h>

#include "config.h"
#include "start.h"

extern float valueFields[];

#define PCF8574_ADDR 0x38

#define O_GLOW_PLUGS 0
#define O_FAN 1
#define O_HEATER_HI 2
#define O_HEATER_LO 3
#define O_GLOW_PLUGS_LAMP 4
#define O_HEATED_WINDOW_L 5
#define O_HEATED_WINDOW_P 6

#define INTERRUPT_HALL 7    //cpu pio number

//cpu pio numbers
#define A_4051 11
#define B_4051 12
#define C_4051 13

void initSensors(void);
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
#endif
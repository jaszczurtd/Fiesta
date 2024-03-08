
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

#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include <ADS1X15.h>

typedef struct {
  uint32_t pwmInitted;
  uint16_t analogWritePseudoScale;
  uint16_t analogWriteSlowScale;
  uint32_t pin;
  uint32_t analogFreq; 
  uint32_t analogScale;
} pwmConfig;

#define C_INIT_VAL 0xdeadbeef;

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

bool readMediumValues(void *argument);
bool readHighValues(void *argument);

void init4051(void);
void set4051ActivePin(unsigned char pin);

bool isDPFRegenerating(void);

bool updateValsForDebug(void *arg);
void pwm_init(void);

float getSystemSupplyVoltage(void);
int getVP37Adjustometer(void);
float getVP37FuelTemperature(void);

#endif
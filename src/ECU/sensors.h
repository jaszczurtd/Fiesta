
#ifndef T_SENSORS
#define T_SENSORS

#include <libConfig.h>
#include "config.h"

#include <tools_c.h>
#include "canDefinitions.h"

#include "hardwareConfig.h"
#include "tests.h"
#include "dtcManager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int16_t  pulseHz;       // deviation from baseline [Hz]
  uint8_t  voltageRaw;    // supply voltage in 0.1 V units
  uint8_t  fuelTempC;     // fuel temperature °C
  uint8_t  status;        // bitmask (ADJ_STATUS_*)
  bool     commOk;        // true if I2C transaction succeeded
} adjustometer_reading_t;

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
int32_t readThrottle(void);
float readAirTemperature(void);
float readBarPressure(void);
int32_t getThrottlePercentage(void);
int32_t getPercentageEngineLoad(void);

bool pcf8574_init(void);
void pcf8574_write(unsigned char pin, bool value);
bool pcf8574_read(unsigned char pin);
void valToPWM(unsigned char pin, int32_t val);

void readMediumValues(void);
void readHighValues(void);

void init4051(void);
void set4051ActivePin(unsigned char pin);

bool isDPFRegenerating(void);

void updateValsForDebug(void);
void pwm_init(void);

adjustometer_reading_t *getVP37Adjustometer(void);
bool waitForAdjustometerBaseline(void);
float getSystemSupplyVoltage(void);

#ifdef __cplusplus
}
#endif

#endif

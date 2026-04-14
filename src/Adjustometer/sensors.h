
#ifndef T_SENSORS
#define T_SENSORS

#include <libConfig.h>
#include "config.h"

#include <tools_c.h>
#include "canDefinitions.h"

#include "hardwareConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

//in miliseconds, print values into serial
#define DEBUG_UPDATE (3 * SECOND)

#define ADJUSTOMETER_SIGNAL_LOSS_MULTIPLIER 3U
#define ADJUSTOMETER_SIGNAL_LOSS_MIN_US 5000U
#define ADJUSTOMETER_SIGNAL_LOSS_MAX_US 200000U

typedef enum {
  ADJ_STATUS_OK             = 0,
  ADJ_STATUS_SIGNAL_LOST    = 1,
  ADJ_STATUS_BASELINE_PENDING = 2,
} AdjStatus;

void initI2C(void);
void initBasicPIO(void);
void initSensors(void);

int32_t  getAdjustometerPulses(void);
AdjStatus getAdjustometerStatus(void);
uint8_t  getSupplyVoltageRaw(void);
uint8_t  getFuelTemperatureRaw(void);
uint8_t  getBaselineFuelTemp(void);
int32_t  getAdaptiveCoeffX10(void);
int32_t  getDbgLastDtX256(void);
int32_t  getDbgLastRawDrift(void);
int32_t  getDbgLastNewCoeff(void);
#ifdef __cplusplus
}
#endif

#endif

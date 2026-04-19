
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
#define DEBUG_UPDATE (125)

#define ADJUSTOMETER_SIGNAL_LOSS_MULTIPLIER 3U
// Minimum timeout for signal-loss detection.  At the operating range (~37 kHz)
// the dynamic timeout (period × 3 ≈ 81 µs) is always clamped here.  10 ms gives
// safe margin against false loss during rapid frequency transients above ~100 Hz.
#define ADJUSTOMETER_SIGNAL_LOSS_MIN_US 10000U
#define ADJUSTOMETER_SIGNAL_LOSS_MAX_US 200000U

// Status register bitmask (register 0x04).
// Bit 0: oscillation signal lost.
// Bit 1: fuel temperature sensor broken (readings near 3.3V, ntcToTemp < 0).
// Bit 2: baseline calibration in progress.
// Bit 3: supply voltage out of range (below 8V or above 15V).
// All bits 0 = everything OK.
#define ADJ_STATUS_OK                0x00
#define ADJ_STATUS_SIGNAL_LOST       (1 << 0)
#define ADJ_STATUS_FUEL_TEMP_BROKEN  (1 << 1)
#define ADJ_STATUS_BASELINE_PENDING  (1 << 2)
#define ADJ_STATUS_VOLTAGE_BAD       (1 << 3)

// Supply voltage thresholds (tenths of a volt).
#define ADJ_VOLTAGE_MIN_TV  80   // 8.0 V
#define ADJ_VOLTAGE_MAX_TV  150  // 15.0 V

// Fuel temp raw == 0 means sensor reads near 3.3V / ntcToTemp returned negative.
#define ADJ_FUEL_TEMP_SENSOR_BROKEN  0

void initI2C(void);
void initBasicPIO(void);
void initSensors(void);

int32_t  getAdjustometerPulses(void);
uint32_t getAdjustometerSignalHz(void);
uint8_t  getAdjustometerStatus(void);
uint8_t  getSupplyVoltageRaw(void);
uint8_t  getFuelTemperatureRaw(void);
uint8_t  getBaselineFuelTemp(void);
int32_t  getAdaptiveCoeffX10(void);
int32_t  getDbgLastDtX256(void);
int32_t  getDbgLastRawDrift(void);
int32_t  getDbgLastNewCoeff(void);
uint32_t  getBaseline(void);
bool isAdjustometerReady(void);

#ifdef __cplusplus
}
#endif

#endif

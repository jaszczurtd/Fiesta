
#ifndef T_SENSORS
#define T_SENSORS

#include <libConfig.h>
#include "config.h"

#include <tools_c.h>
#include "../common/canDefinitions/canDefinitions.h"

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

/**
 * @brief Initialize the I2C slave interface and default registers.
 * @return None.
 */
void initI2C(void);

/**
 * @brief Initialize basic GPIO and ADC resources used by the module.
 * @return None.
 */
void initBasicPIO(void);

/**
 * @brief Initialize runtime sensor state and attach the Hall interrupt.
 * @return None.
 * @note This module acts as a project-local G149-like quantity-feedback source for
 *       the VP37 control path.
 */
void initSensors(void);

/**
 * @brief Return the current Adjustometer pulse magnitude.
 * @return Current pulse value, clamped to zero on signal loss.
 * @note This is a project-local G149-like raw feedback signal. It is not a literal
 *       OEM G149 output and not a calibrated mg/stroke value.
 */
int32_t  getAdjustometerPulses(void);

/**
 * @brief Return the current filtered oscillator frequency.
 * @return Signal frequency in hertz.
 * @note This is the oscillator-side raw observable behind the project's G149-like
 *       quantity-feedback path.
 */
uint32_t getAdjustometerSignalHz(void);

/**
 * @brief Return the packed module status bitmask.
 * @return Status register value.
 * @note The flags describe health of the project-local G149-like feedback module,
 *       including its G81-like fuel-temperature input.
 */
uint8_t  getAdjustometerStatus(void);

/**
 * @brief Read the filtered supply voltage in tenths of a volt.
 * @return Raw voltage register value.
 */
uint8_t  getSupplyVoltageRaw(void);

/**
 * @brief Read the filtered fuel temperature in degrees Celsius.
 * @return Raw fuel-temperature register value.
 * @note This is the module's G81-like fuel-temperature input.
 */
uint8_t  getFuelTemperatureRaw(void);

/**
 * @brief Return the locked baseline frequency used as the pulse zero point.
 * @return Baseline frequency in hertz.
 * @note This is the zero reference for the project-local G149-like quantity-feedback path.
 */
uint32_t  getBaseline(void);

/**
 * @brief Check whether baseline acquisition and verification are complete.
 * @return True when the Adjustometer is ready for use.
 * @note The ECU waits for this state before enabling its N146/G149-like inner loop.
 */
bool isAdjustometerReady(void);

#ifdef __cplusplus
}
#endif

#endif

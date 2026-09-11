
#ifndef T_SENSORS
#define T_SENSORS

#include "../common/adjustometer_feedback.h"
#include "config.h"
#include <libConfig.h>

#include "../common/canDefinitions/canDefinitions.h"
#include <JaszczurHAL.h>

#include "hardwareConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// in miliseconds, print values into serial
#define DEBUG_UPDATE (250)

#define ADJUSTOMETER_SIGNAL_LOSS_MULTIPLIER 3U
// Minimum timeout for signal-loss detection.  At the operating range (~37 kHz)
// the dynamic timeout (period × 3 ≈ 81 µs) is always clamped here.  10 ms gives
// margin for 32-period capture batches at the operating frequency.
#define ADJUSTOMETER_SIGNAL_LOSS_MIN_US 10000U
#define ADJUSTOMETER_SIGNAL_LOSS_MAX_US 200000U

// Supply voltage thresholds (tenths of a volt).
#define ADJ_VOLTAGE_MIN_TV 80  // 8.0 V
#define ADJ_VOLTAGE_MAX_TV 150 // 15.0 V

// Fuel temp raw == 0 means the sensor conversion failed or returned a
// negative value.
#define ADJ_FUEL_TEMP_SENSOR_BROKEN 0

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
 * @brief Initialize runtime sensor state and hardware period capture.
 * @return None.
 * @note This module acts as a project-local G149-like quantity-feedback source
 * for the VP37 control path.
 */
void initSensors(void);

/** @brief Drain hardware capture on Core0 before publishing feedback. */
void updateAdjustometerCapture(void);

/** @brief Read a coherent oscillator window without ADC or USB I/O.
 * @param out Non-NULL destination; errors leave it unchanged.
 * @return HAL_OK, HAL_EINVAL, or HAL_EAGAIN if the producer keeps updating.
 */
hal_status_t getAdjustometerFeedback(adjustometer_feedback_t *out);
/** @brief Read and cache voltage/temperature on their owning auxiliary core. */
void updateAuxiliarySensors(void);

/**
 * @brief Return the current Adjustometer pulse magnitude.
 * @return Current pulse value, clamped to zero on signal loss.
 * @note This is a project-local G149-like raw feedback signal. It is not a
 * literal OEM G149 output and not a calibrated mg/stroke value.
 */
int32_t getAdjustometerPulses(void);

/**
 * @brief Return the current filtered oscillator frequency.
 * @return Signal frequency in hertz.
 * @note This is the oscillator-side raw observable behind the project's
 * G149-like quantity-feedback path.
 */
uint32_t getAdjustometerSignalHz(void);

/**
 * @brief Return the signed filtered-frequency displacement from baseline.
 * @return signalHz - baselineHz in hertz, without abs() or zero-hold.
 */
int32_t getAdjustometerSignedDeltaHz(void);

/**
 * @brief Return the packed module status bitmask.
 * @return Status register value.
 * @note The flags describe health of the project-local G149-like feedback
 * module, including its G81-like fuel-temperature input.
 */
uint8_t getAdjustometerStatus(void);

/**
 * @brief Read the filtered supply voltage in tenths of a volt.
 * @return Raw voltage register value.
 */
uint8_t getSupplyVoltageRaw(void);

/**
 * @brief Read the filtered fuel temperature in degrees Celsius.
 * @return Raw fuel-temperature register value.
 * @note This is the module's G81-like fuel-temperature input.
 */
uint8_t getFuelTemperatureRaw(void);

/**
 * @brief Return the locked baseline frequency used as the pulse zero point.
 * @return Baseline frequency in hertz.
 * @note This is the zero reference for the project-local G149-like
 * quantity-feedback path.
 */
uint32_t getBaseline(void);

/**
 * @brief Check whether baseline acquisition and verification are complete.
 * @return True when the Adjustometer is ready for use.
 * @note The ECU waits for this state before enabling its N146/G149-like inner
 * loop.
 */
bool isAdjustometerReady(void);

#ifdef __cplusplus
}
#endif

#endif

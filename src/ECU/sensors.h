
#ifndef T_SENSORS
#define T_SENSORS

#include "config.h"
#include <libConfig.h>

#include "../common/canDefinitions/canDefinitions.h"
#include <JaszczurHAL.h>

#include "dtcManager.h"
#include "hardwareConfig.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int16_t pulseHz;             // deviation from baseline [Hz]
  uint8_t voltageRaw;          // supply voltage in 0.1 V units
  uint8_t fuelTempC;           // fuel temperature °C
  uint8_t status;              // bitmask (ADJ_STATUS_*)
  bool commOk;                 // true if I2C transaction succeeded
  uint32_t signalHz;           // filtered absolute oscillator frequency [Hz]
  uint32_t baselineHz;         // locked zero-reference frequency [Hz]
  int32_t signedDeltaHz;       // signalHz - baselineHz, without abs/zero-hold
  int16_t chipTempDeciC;       // RP2040 die temperature in 0.1 °C units
  uint8_t extendedFlags;       // bitmask (ADJUSTOMETER_EXT_FLAG_*)
  bool extendedTelemetryValid; // versioned extension is coherent
  bool fastFeedback, feedbackFresh;
  uint32_t rawHz, sampleNumber, measuredUs;
  uint16_t ageUs;
  hal_status_t readStatus;
  uint32_t readUs;
  uint8_t readRetries;
} adjustometer_reading_t;

// in miliseconds, print values into serial
#define DEBUG_UPDATE 3 * 1000

#define GPS_TIME_DATE_BUFFER_SIZE 16

/**
 * @brief Store one global runtime value.
 * @param idx Global value index to update.
 * @param val Value to store.
 */
void setGlobalValue(int idx, float val);

/**
 * @brief Read one global runtime value.
 * @param idx Global value index to read.
 * @return Current value stored at the requested index.
 */
float getGlobalValue(int idx);

/**
 * @brief Initialize the main I2C bus and recover it if needed.
 */
void initI2C(void);

/**
 * @brief Initialize the main SPI bus and its chip-select lines.
 */
void initSPI(void);

/**
 * @brief Initialize sensor infrastructure, globals, PWM, and GPS.
 */
void initSensors(void);

/**
 * @brief Initialize basic GPIO outputs used by the ECU.
 */
void initBasicPIO(void);
// readers

/**
 * @brief Read averaged coolant temperature.
 * @return Coolant temperature in degrees Celsius.
 * @note Closest OEM alias is the G62 coolant-temperature path.
 */
float readCoolantTemp(void);

/**
 * @brief Read averaged oil temperature.
 * @return Oil temperature in degrees Celsius.
 */
float readOilTemp(void);

/**
 * @brief Read the legacy throttle-named driver-demand input and map it into
 * PWM-scale units.
 * @return Driver-demand signal in the internal PWM-scale range.
 * @note In EDC15/VW terms this is closest to the G79/G185 accelerator-pedal
 * path, not to a gasoline throttle plate.
 */
int32_t readThrottle(void);

/**
 * @brief Read intake air temperature.
 * @return Intake air temperature in degrees Celsius.
 * @note Closest OEM alias is the G72 intake-air-temperature path.
 */
float readAirTemperature(void);

/**
 * @brief Read boost pressure above atmospheric pressure.
 * @return Pressure in bar relative to atmosphere.
 * @note Closest OEM alias is the G71 intake-manifold / boost-pressure path.
 */
float readBarPressure(void);

/**
 * @brief Convert the stored legacy throttle value into a 0..100 driver-demand
 * percentage.
 * @return Driver-demand percentage.
 * @note In EDC15/VW terms this is closest to G79/G185-derived pedal demand.
 */
int32_t getThrottlePercentage(void);

/**
 * @brief Calculate engine load percentage from current pressure and RPM.
 * @return Engine load percentage in the 0..100 range.
 * @note This is a project-local supervisory load estimate, not a literal OEM
 * air-mass or mg/stroke quantity variable.
 */
int32_t getPercentageEngineLoad(void);

/**
 * @brief Initialize the PCF8574 expander output latch.
 * @return True on success, otherwise false.
 */
bool pcf8574_init(void);

/**
 * @brief Write one output bit on the PCF8574 expander.
 * @param pin Expander pin index to write.
 * @param value True to set the bit, false to clear it.
 */
void pcf8574_write(unsigned char pin, bool value);

/**
 * @brief Read one bit from the PCF8574 expander.
 * @param pin Expander pin index to read.
 * @return Current logic state of the selected pin.
 */
bool pcf8574_read(unsigned char pin);

/**
 * @brief Write a logical output value to one PWM-controlled channel.
 * @param pin Logical PWM output identifier.
 * @param val Command value in project PWM units.
 */
void valToPWM(unsigned char pin, int32_t val);

/**
 * @brief Refresh medium-rate sensor values.
 */
void readMediumValues(void);

/**
 * @brief Refresh high-rate runtime values and selected CAN updates.
 */
void readHighValues(void);

/**
 * @brief Initialize the HC4051 analog multiplexer control pins.
 */
void init4051(void);

/** Analog settling after a channel change: the multiplexer's switch plus the
 * input's RC, before the converter may look at the new channel. Under the
 * hardware-paced scan the wait grows by two scan frames, computed from the
 * scan's own frame period, because the newest scanned sample can be up to two
 * frames old when it is read. Overridable so the analog part can be measured
 * against the path. */
#ifndef SENSORS_MUX_ANALOG_SETTLE_US
#define SENSORS_MUX_ANALOG_SETTLE_US 12U
#endif

/**
 * @brief Whether every analog input read through hal_adc_read() is carried
 * by the running ADC scan.
 *
 * While the scan owns the converter a pin outside it cannot be converted and
 * hal_adc_read() reports it unreadable, so the sensor reads would fail for
 * the whole run. Checked once after the scan starts; true without a scan,
 * when reads convert live.
 */
bool sensors_scanCoversInputs(void);

/** Bench diagnostic for the demand read: every sample near the zero threshold
 * is reported with its scan frame-mates, the margin once a second, and right
 * after each channel switch the newest-sample path is hammered while the mux
 * is fresh on the demand input, so a frame from before the switch stands out
 * as the previous channel's value. Build with SENSORS_THROTTLE_DIAG=1; the
 * bench on 2026-09-16 saw one such frame in about 220 000 reads before the
 * scan reader was fixed and none after. Off in every build unless asked. */
#ifndef SENSORS_THROTTLE_DIAG
#define SENSORS_THROTTLE_DIAG 0
#endif
/** Reads this close to the zero threshold are reported in full. */
#ifndef SENSORS_THROTTLE_DIAG_EDGE
#define SENSORS_THROTTLE_DIAG_EDGE 15
#endif
#ifndef SENSORS_THROTTLE_DIAG_EVENTS_PER_S
#define SENSORS_THROTTLE_DIAG_EVENTS_PER_S 8U
#endif
/** Newest-sample reads hammered right after the switch; a frame from before
 * it carries the previous channel and stands apart from the potentiometer by
 * far more than the band. */
#ifndef SENSORS_THROTTLE_DIAG_STRESS_READS
#define SENSORS_THROTTLE_DIAG_STRESS_READS 1500U
#endif
#ifndef SENSORS_THROTTLE_DIAG_STRESS_BAND
#define SENSORS_THROTTLE_DIAG_STRESS_BAND 64
#endif
/** The other mux inputs are surveyed this often so a stray value can be
 * matched to its channel. */
#ifndef SENSORS_THROTTLE_DIAG_SURVEY_MS
#define SENSORS_THROTTLE_DIAG_SURVEY_MS 10000U
#endif

/**
 * @brief Read one HC4051 input with the standard averaging, under the mux
 * lock.
 * @param channel Multiplexer channel to select.
 * @param outAverage Non-NULL; receives the averaged, compensated ADC value.
 * @return HAL_OK, or the averaging helper's status.
 * @note Every reader of the shared analog input goes through here or takes
 * the same lock itself: a channel switch from another context in the middle
 * of a burst hands that burst the other channel's value.
 */
hal_status_t sensors_readMuxAverage(unsigned char channel, float *outAverage);

/**
 * @brief Select the active HC4051 input channel.
 * @param pin Multiplexer channel number to select.
 */
void set4051ActivePin(unsigned char pin);

/**
 * @brief Check whether DPF regeneration is currently active.
 * @return True when regeneration is active, otherwise false.
 */
bool isDPFRegenerating(void);

/**
 * @brief Print selected runtime values when they change.
 */
void updateValsForDebug(void);

/**
 * @brief Create PWM channel handles used by ECU outputs.
 */
void pwm_init(void);

/**
 * @brief Take a thread-safe snapshot of the latest Adjustometer state.
 * @param out Caller-owned storage receiving the snapshot. Must not be NULL.
 * @note Triggers a fresh I2C read via readAdjustometer() and copies the
 *       resulting snapshot into @p out. No heap allocation; the caller
 *       provides the destination (stack or static). Adjustometer is only
 *       a project-local G149-like signal source, not a literal OEM G149
 *       implementation.
 */
void getVP37Adjustometer(adjustometer_reading_t *out);

/** @brief Select versioned fast feedback before starting control; false retains
 * legacy API behavior. Changing mode resets sample-age tracking. Call during
 * single-owner initialization. */
void setVP37AdjustometerFastFeedback(bool enabled);

/**
 * @brief Refresh the optional Adjustometer diagnostic-telemetry extension.
 * @param out Snapshot receiving legacy and cached extension fields.
 * @return True when a new coherent version-1 extension was received.
 * @note Failure does not affect legacy commOk or the VP37 control path.
 */
bool getVP37AdjustometerExtendedTelemetry(adjustometer_reading_t *out);

/**
 * @brief Wait until the Adjustometer reports that its quantity-feedback
 * baseline capture is ready.
 * @return True when baseline becomes ready before timeout, otherwise false.
 * @note Baseline readiness gates the project-local G149-like feedback path
 * before the ECU enables the inner VP37 loop.
 */
bool waitForAdjustometerBaseline(void);

/**
 * @brief Get current ECU system supply voltage.
 * @return Supply voltage in volts, or 0 when unavailable.
 * @note With VP37 enabled, sourced from Adjustometer telemetry.
 *       Without VP37, sourced from the local ADC divider path.
 */
float getSystemSupplyVoltage(void);

/**
 * @brief Read ECU supply voltage directly from its local ADC divider.
 * @return Supply voltage in volts, or 0 when the conversion fails.
 * @note This bypasses Adjustometer transport and filtering. VP37 uses its fast
 *       changes while retaining the Adjustometer path as its reference.
 */
float getLocalSystemSupplyVoltage(void);

#ifdef __cplusplus
}
#endif

#endif

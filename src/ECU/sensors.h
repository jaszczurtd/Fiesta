
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

// in miliseconds, print values into serial
#define DEBUG_UPDATE 3 * 1000

#define GPS_TIME_DATE_BUFFER_SIZE 16

/** Driver-demand input filter time constant, in seconds. */
#define SENSORS_DRIVER_FILTER_S 0.03f
/** Minimum filtered change accepted as driver demand, in percentage points. */
#define SENSORS_DRIVER_DEADBAND_PERCENT 0.1f

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
 * @brief Read the filtered fractional driver demand.
 * @return Cached demand in 0..100 percent; zero before the first sample.
 * @note readThrottleValues() updates this value under the shared sensor lock.
 * Reading it neither samples the ADC nor advances the filter.
 */
float getDriverDemandPercent(void);

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
 * @note Reports DTC_PWM_CHANNEL_NOT_INIT on every write, set or cleared.
 */
void valToPWM(unsigned char pin, int32_t val);

/**
 * @brief Write one PWM output without touching the DTC store.
 * @param pin Logical PWM output identifier.
 * @param val Command value in project PWM units; the output is active low.
 * @return False when pwm_init() has not created a channel for @p pin.
 * @note For the VP37 control step: it must not wait on the DTC mutex, which
 * core 0 holds while it writes the DTC store.
 */
bool pwmWrite(unsigned char pin, int32_t val);

/** @brief Whether pwm_init() created a channel for @p pin. */
bool pwmChannelReady(unsigned char pin);

/**
 * @brief Refresh medium-rate sensor values.
 */
void readMediumValues(void);

/**
 * @brief Sample driver input and update its raw cache and filtered demand.
 * @note Uses the mux lock and ADC averaging, then publishes both values under
 * the shared sensor lock. The first sample seeds the filter; later samples use
 * elapsed milliseconds, including counter wrap. Zero and failed reads release
 * demand immediately. Full-scale input snaps to 100 percent within the
 * deadband. Does not transmit CAN or refresh other sensor values.
 */
void readThrottleValues(void);

/**
 * @brief Refresh high-rate runtime values and selected CAN updates.
 * @note Potentiometer sampling belongs to readThrottleValues(); this reads
 * its cached value for CAN updates.
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
 * @brief Write one register address to an I2C device and read bytes back.
 * @param address 7-bit device address.
 * @param reg Register address the read starts at.
 * @param data Destination of @p len bytes.
 * @param len Number of bytes to read.
 * @return HAL_OK, or the bus error of hal_i2c_write_read_bus_ex().
 * @note Holds i2cBusMutex for the whole transaction.
 */
hal_status_t i2cReadRegisters(uint8_t address, uint8_t reg, uint8_t *data,
                              size_t len);

/**
 * @brief Get current ECU system supply voltage from the local ADC divider.
 * @return Supply voltage in volts, or 0 when the conversion fails.
 */
float getSystemSupplyVoltage(void);

#ifdef __cplusplus
}
#endif

#endif

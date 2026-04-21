
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

/**
 * @brief Store one global runtime value.
 * @param idx Global value index to update.
 * @param val Value to store.
 * @return None.
 */
void  setGlobalValue(int idx, float val);

/**
 * @brief Read one global runtime value.
 * @param idx Global value index to read.
 * @return Current value stored at the requested index.
 */
float getGlobalValue(int idx);

/**
 * @brief Initialize the main I2C bus and recover it if needed.
 * @return None.
 */
void initI2C(void);

/**
 * @brief Initialize the main SPI bus and its chip-select lines.
 * @return None.
 */
void initSPI(void);

/**
 * @brief Initialize sensor infrastructure, globals, PWM, and GPS.
 * @return None.
 */
void initSensors(void);

/**
 * @brief Initialize basic GPIO outputs used by the ECU.
 * @return None.
 */
void initBasicPIO(void);
//readers

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
 * @brief Read the legacy throttle-named driver-demand input and map it into PWM-scale units.
 * @return Driver-demand signal in the internal PWM-scale range.
 * @note In EDC15/VW terms this is closest to the G79/G185 accelerator-pedal path,
 *       not to a gasoline throttle plate.
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
 * @brief Convert the stored legacy throttle value into a 0..100 driver-demand percentage.
 * @return Driver-demand percentage.
 * @note In EDC15/VW terms this is closest to G79/G185-derived pedal demand.
 */
int32_t getThrottlePercentage(void);

/**
 * @brief Calculate engine load percentage from current pressure and RPM.
 * @return Engine load percentage in the 0..100 range.
 * @note This is a project-local supervisory load estimate, not a literal OEM air-mass
 *       or mg/stroke quantity variable.
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
 * @return None.
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
 * @return None.
 */
void valToPWM(unsigned char pin, int32_t val);

/**
 * @brief Refresh medium-rate sensor values.
 * @return None.
 */
void readMediumValues(void);

/**
 * @brief Refresh high-rate runtime values and selected CAN updates.
 * @return None.
 */
void readHighValues(void);

/**
 * @brief Initialize the HC4051 analog multiplexer control pins.
 * @return None.
 */
void init4051(void);

/**
 * @brief Select the active HC4051 input channel.
 * @param pin Multiplexer channel number to select.
 * @return None.
 */
void set4051ActivePin(unsigned char pin);

/**
 * @brief Check whether DPF regeneration is currently active.
 * @return True when regeneration is active, otherwise false.
 */
bool isDPFRegenerating(void);

/**
 * @brief Print selected runtime values when they change.
 * @return None.
 */
void updateValsForDebug(void);

/**
 * @brief Create PWM channel handles used by ECU outputs.
 * @return None.
 */
void pwm_init(void);

/**
 * @brief Read and return the latest Adjustometer state for the VP37 quantity-feedback path.
 * @return Pointer to the shared Adjustometer reading structure.
 * @note Adjustometer is only a project-local G149-like signal source; it is not a
 *       literal OEM G149 implementation.
 */
adjustometer_reading_t *getVP37Adjustometer(void);

/**
 * @brief Wait until the Adjustometer reports that its quantity-feedback baseline capture is ready.
 * @return True when baseline becomes ready before timeout, otherwise false.
 * @note Baseline readiness gates the project-local G149-like feedback path before the
 *       ECU enables the inner VP37 loop.
 */
bool waitForAdjustometerBaseline(void);

/**
 * @brief Get current system supply voltage from Adjustometer telemetry.
 * @return Supply voltage in volts, or 0 when unavailable.
 */
float getSystemSupplyVoltage(void);

#ifdef __cplusplus
}
#endif

#endif

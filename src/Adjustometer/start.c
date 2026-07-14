
#include "start.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "led.h"
#include <hal/hal_i2c_slave.h>
#include <hal/hal_soft_timer.h>
#include <hal/hal_system.h>
#include <limits.h>

#if defined(__cplusplus)
static_assert(ADJUSTOMETER_TOTAL_REG_COUNT <= HAL_I2C_SLAVE_REG_MAP_SIZE,
              "Adjustometer telemetry exceeds the I2C register map");
#else
_Static_assert(ADJUSTOMETER_TOTAL_REG_COUNT <= HAL_I2C_SLAVE_REG_MAP_SIZE,
               "Adjustometer telemetry exceeds the I2C register map");
#endif

#ifdef DEBUG_MAX_CHANGES
static int32_t lastPulse = 0;
static uint8_t lastVoltage = 0;
static uint8_t lastFuelTemp = 0;
#endif
static uint32_t lastPeriodicLogMs = 0;
static uint8_t telemetrySequence = 0U;
static uint32_t lastExtendedUpdateMs = 0U;
static uint32_t lastChipTempReadMs = 0U;
static int16_t chipTempDeciC = INT16_MIN;

#define ADJUSTOMETER_CHIP_TEMP_UPDATE_MS 250U

/**
 * @brief Write one 32-bit big-endian value into the I2C register map.
 * @param reg Register address of the most-significant byte.
 * @param value Value to publish.
 * @return None.
 */
static void writeRegisterU32BE(uint8_t reg, uint32_t value) {
  hal_i2c_slave_reg_write8(reg, (uint8_t)(value >> 24));
  hal_i2c_slave_reg_write8((uint8_t)(reg + 1U), (uint8_t)(value >> 16));
  hal_i2c_slave_reg_write8((uint8_t)(reg + 2U), (uint8_t)(value >> 8));
  hal_i2c_slave_reg_write8((uint8_t)(reg + 3U), (uint8_t)value);
}

/**
 * @brief Refresh the slowly changing RP2040 die-temperature telemetry.
 * @return True when the cached temperature is valid.
 */
static bool updateChipTemperature(void) {
  const uint32_t nowMs = hal_millis();
  if (chipTempDeciC == INT16_MIN ||
      (nowMs - lastChipTempReadMs) >= ADJUSTOMETER_CHIP_TEMP_UPDATE_MS) {
    lastChipTempReadMs = nowMs;
    const float chipTempC = hal_read_chip_temp();
    if (chipTempC == chipTempC && chipTempC >= -50.0f && chipTempC <= 150.0f) {
      const float scaled = chipTempC * 10.0f;
      chipTempDeciC = (int16_t)(scaled + ((scaled >= 0.0f) ? 0.5f : -0.5f));
    } else {
      chipTempDeciC = INT16_MIN;
    }
  }
  return chipTempDeciC != INT16_MIN;
}

/**
 * @brief Publish the optional, versioned Adjustometer telemetry extension.
 * @param status Current legacy status flags.
 * @return None.
 */
static void updateExtendedI2CRegisters(uint8_t status) {
  const uint32_t signalHz = getAdjustometerSignalHz();
  const uint32_t baselineHz = getBaseline();
  const int32_t signedDeltaHz = (int32_t)signalHz - (int32_t)baselineHz;
  uint8_t flags = 0U;

  if ((status & ADJ_STATUS_SIGNAL_LOST) == 0U) {
    flags |= ADJUSTOMETER_EXT_FLAG_SIGNAL_VALID;
  }
  if ((status & ADJ_STATUS_BASELINE_PENDING) == 0U) {
    flags |= ADJUSTOMETER_EXT_FLAG_BASELINE_VALID;
  }
  if (updateChipTemperature()) {
    flags |= ADJUSTOMETER_EXT_FLAG_CHIP_TEMP_VALID;
  }

  const uint8_t nextEvenSequence = (uint8_t)((telemetrySequence + 2U) & 0xFEU);
  const uint8_t updateInProgressSequence = (uint8_t)(nextEvenSequence | 1U);

  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_EXT_SEQ_BEGIN,
                           updateInProgressSequence);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_EXT_VERSION,
                           ADJUSTOMETER_EXT_VERSION);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_EXT_FLAGS, flags);
  writeRegisterU32BE(ADJUSTOMETER_REG_SIGNAL_HZ, signalHz);
  writeRegisterU32BE(ADJUSTOMETER_REG_BASELINE_HZ, baselineHz);
  writeRegisterU32BE(ADJUSTOMETER_REG_SIGNED_DELTA_HZ, (uint32_t)signedDeltaHz);
  hal_i2c_slave_reg_write16(ADJUSTOMETER_REG_CHIP_TEMP_DECI_C,
                            (uint16_t)chipTempDeciC);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_EXT_SEQ_END, nextEvenSequence);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_EXT_SEQ_BEGIN, nextEvenSequence);
  telemetrySequence = nextEvenSequence;
}

/**
 * @brief Initialize core-0 services, sensors and status reporting.
 * @return None.
 */
void initialization(void) {

  debugInit();
  setDebugPrefixWithColon(SC_MODULE_TOKEN_ADJUSTOMETER);

  setupWatchdog(NULL, WATCHDOG_TIME);

  initI2C();

  initSensors();
  initLed();

  setStartedCore0();

  deb("Fiesta Adjustometer started: %s\n",
      isEnvironmentStarted() ? "yes" : "no");
}

/**
 * @brief Run one iteration of the idle core-0 maintenance loop.
 * @return None.
 */
void looper(void) {
  updateWatchdogCore0();

  if (!isEnvironmentStarted()) {
    hal_idle();
    return;
  }

  hal_idle();
  hal_delay_ms(CORE_OPERATION_DELAY);
}

/**
 * @brief Mirror current measurements and status into the I2C register map.
 * @return None.
 */
static void updateI2CRegisters(void) {
  // getAdjustometerPulses() returns abs() - VP37 deflects only in one
  // direction, so pulse is always >= 0.  Only the upper int16_t bound needs
  // clamping.
  int32_t pulse = getAdjustometerPulses();
  if (pulse > 32767)
    pulse = 32767;

  uint8_t voltage = getSupplyVoltageRaw();
  uint8_t fuelTemp = getFuelTemperatureRaw();
  uint8_t status = getAdjustometerStatus();

  if (isAdjustometerReady()) {
    hal_i2c_slave_reg_write16(ADJUSTOMETER_REG_PULSE_HI,
                              (uint16_t)(int16_t)pulse);
    hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_VOLTAGE, voltage);
    hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_FUEL_TEMP, fuelTemp);
    hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_STATUS, status);
    const uint32_t telemetryNowMs = hal_millis();
    if ((telemetryNowMs - lastExtendedUpdateMs) >= ADJUSTOMETER_EXT_UPDATE_MS) {
      lastExtendedUpdateMs = telemetryNowMs;
      updateExtendedI2CRegisters(status);
    }
  }

#ifdef DEBUG_DEEP
#ifdef DEBUG_MAX_CHANGES
  if (pulse != lastPulse) {
    deb("p: %ld\n", (long)pulse);
    lastPulse = pulse;
  }
  if (abs((int)voltage - (int)lastVoltage) >= 2) {
    deb("v: %u\n", voltage);
    lastVoltage = voltage;
  }
  if (abs((int)fuelTemp - (int)lastFuelTemp) >= 2) {
    deb("ft: %u\n", fuelTemp);
    lastFuelTemp = fuelTemp;
  }
#endif
  uint32_t now = hal_millis();
  if (now - lastPeriodicLogMs >= DEBUG_UPDATE) {
    lastPeriodicLogMs = now;
    deb("p:%ld f:%lu.%lukHz d:%ld v:%u ft:%u tc:%.1f s:%u bl:%lu ready:%d\n",
        (long)pulse, (unsigned long)(getAdjustometerSignalHz() / 1000U),
        (unsigned long)((getAdjustometerSignalHz() % 1000U) / 100U),
        (long)getAdjustometerSignedDeltaHz(), voltage, fuelTemp,
        (double)chipTempDeciC * 0.1, status, (unsigned long)getBaseline(),
        isAdjustometerReady());
  }
#endif
}

/**
 * @brief Initialize the second core used for I2C register publishing.
 * @return None.
 */
void initialization1(void) {
  setStartedCore1();

  deb("Second core initialized");
}

//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------

/**
 * @brief Run one iteration of the core-1 Adjustometer loop.
 * @return None.
 */
void looper1(void) {

  updateWatchdogCore1();

  if (!isEnvironmentStarted()) {
    hal_idle();
    return;
  }

  updateI2CRegisters();
  updateLed();

  hal_idle();
  hal_delay_ms(CORE_OPERATION_DELAY);
}


#include "start.h"
#include <hal/hal_soft_timer.h>
#include <hal/hal_i2c_slave.h>
#include "led.h"

#ifdef DEBUG_MAX_CHANGES
static int32_t lastPulse = 0;
static uint8_t lastVoltage = 0;
static uint8_t lastFuelTemp = 0;
#endif
static uint32_t lastPeriodicLogMs = 0;

/**
 * @brief Initialize core-0 services, sensors and status reporting.
 * @return None.
 */
void initialization(void) {

  debugInit();
  setDebugPrefix("Adj:");

  setupWatchdog(NULL, WATCHDOG_TIME);
  
  initI2C();

  initSensors();
  initLed();

  setStartedCore0();

  deb("Fiesta Adjustometer started: %s\n", isEnvironmentStarted() ? "yes" : "no");
}

/**
 * @brief Run one iteration of the idle core-0 maintenance loop.
 * @return None.
 */
void looper(void) {
  updateWatchdogCore0();

  if(!isEnvironmentStarted()) {
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
  // getAdjustometerPulses() returns abs() — VP37 deflects only in one direction,
  // so pulse is always >= 0.  Only the upper int16_t bound needs clamping.
  int32_t pulse = getAdjustometerPulses();
  if (pulse > 32767) pulse = 32767;

  uint8_t voltage  = getSupplyVoltageRaw();
  uint8_t fuelTemp = getFuelTemperatureRaw();
  uint8_t status   = getAdjustometerStatus();

  if(isAdjustometerReady()) {
    hal_i2c_slave_reg_write16(ADJUSTOMETER_REG_PULSE_HI, (uint16_t)(int16_t)pulse);
    hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_VOLTAGE, voltage);
    hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_FUEL_TEMP, fuelTemp);
    hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_STATUS, status);
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
    deb("p:%ld f:%lu.%lukHz v:%u ft:%u s:%u bl:%lu ready:%d\n",
      (long)pulse,
      (unsigned long)(getAdjustometerSignalHz() / 1000U),
      (unsigned long)((getAdjustometerSignalHz() % 1000U) / 100U),
      voltage,
      fuelTemp,
      status,
      (unsigned long)getBaseline(),
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

  if(!isEnvironmentStarted()) {
    hal_idle();
    return;
  }

  updateI2CRegisters();
  updateLed();

  hal_idle();
  hal_delay_ms(CORE_OPERATION_DELAY);
}

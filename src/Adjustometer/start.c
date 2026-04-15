
#include "start.h"
#include <hal/hal_soft_timer.h>
#include <hal/hal_i2c_slave.h>
#include "led.h"

void initialization(void) {

  debugInit();
  setDebugPrefix("Adj:");
 
  initSensors();
  initLed();

  setStartedCore0();

  deb("Fiesta Adjustometer started: %s\n", isEnvironmentStarted() ? "yes" : "no");
}

void looper(void) {
  updateWatchdogCore0();

  if(!isEnvironmentStarted()) {
    hal_idle();
    return;
  }

  hal_idle();
  hal_delay_ms(CORE_OPERATION_DELAY);
}

static int32_t lastPulse = 0;
static uint8_t lastVoltage = 0;
static uint8_t lastFuelTemp = 0;
static uint32_t lastPeriodicLogMs = 0;

static void updateI2CRegisters(void) {
  int32_t pulse = getAdjustometerPulses();
  // Clamp to int16_t range for register map
  if (pulse > 32767) pulse = 32767;
  if (pulse < -32768) pulse = -32768;

  uint8_t voltage  = getSupplyVoltageRaw();
  uint8_t fuelTemp = getFuelTemperatureRaw();
  uint8_t status   = getAdjustometerStatus();

  hal_i2c_slave_reg_write16(ADJUSTOMETER_REG_PULSE_HI, (uint16_t)(int16_t)pulse);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_VOLTAGE, voltage);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_FUEL_TEMP, fuelTemp);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_STATUS, status);

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

  uint32_t now = hal_millis();
  if (now - lastPeriodicLogMs >= DEBUG_UPDATE) {
    lastPeriodicLogMs = now;
    deb("p:%ld v:%u ft:%u s:%u bft:%u ac:%ld dt:%ld rd:%ld nc:%ld\n", (long)pulse, voltage, fuelTemp, status, getBaselineFuelTemp(), (long)getAdaptiveCoeffX10(), (long)getDbgLastDtX256(), (long)getDbgLastRawDrift(), (long)getDbgLastNewCoeff());
  }
}

void initialization1(void) {
  setStartedCore1();

  deb("Second core initialized");
}

//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------

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

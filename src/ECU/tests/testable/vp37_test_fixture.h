#pragma once
// Pump fixture shared by the VP37 test binaries: a calibrated pump ready for
// VP37_process() and Adjustometer register data fed through the I2C mock.
#include "../../common/vp37/tests/vp37_pump_setup.h"
#include "dtcManager.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
#include "vp37_adapter.h"
#include "vp37_internal.h"
#include <string.h>

static inline void injectLocalSupplyVoltage(float volts) {
  const float ratio =
      ((float)V_DIVIDER_R1 + (float)V_DIVIDER_R2) / (float)V_DIVIDER_R2;
  int adc = (int)((volts / ratio) * (4095.0f / 3.3f) + 0.5f);
  adc = hal_constrain(adc, 0, 4095);
  hal_mock_adc_inject(ADC_VOLT_PIN, adc);
}

static inline void injectAdjRegisterData(int16_t pulseHz, uint8_t voltage,
                                         uint8_t fuelTemp, uint8_t status) {
  uint8_t buf[5];
  buf[0] = (uint8_t)((uint16_t)pulseHz >> 8);
  buf[1] = (uint8_t)((uint16_t)pulseHz & 0xFF);
  buf[2] = voltage;
  buf[3] = fuelTemp;
  buf[4] = status;
  hal_mock_i2c_inject_rx(buf, 5);
  injectLocalSupplyVoltage((float)voltage * 0.1f);
}

static inline void setupPumpForProcessTests(VP37Pump *pump,
                                            bool fastFrame = false) {
  vp37SetupCalibratedPump(pump, vp37AdapterCallbacks(), fastFrame);
  setGlobalValue(F_RPM, 1000.0f);
  setGlobalValue(F_VOLTS, 14.0f);
  injectLocalSupplyVoltage(14.0f);
  setGlobalValue(F_THROTTLE_POS, 0.0f);
  hal_mock_i2c_set_busy(false);
}

// Common part of setUp(): clock, I2C, sensors and an empty DTC store.
static inline void setUpVp37Fixture(void) {
  hal_mock_set_millis(0);
  hal_i2c_init(4, 5, 400000);
  initSensors();
  initI2C();
  dtcManagerInit();
  dtcManagerClearAll();
}

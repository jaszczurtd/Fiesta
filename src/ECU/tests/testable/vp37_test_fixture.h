#pragma once
// Pump fixture shared by the VP37 test binaries: a calibrated pump ready for
// VP37_process() and Adjustometer register data fed through the I2C mock.
#include "dtcManager.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
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

static inline void setupPumpForProcessTests(VP37Pump *pump) {
  memset(pump, 0, sizeof(*pump));
  pump->pid.controller = hal_pid_controller_create();
  pump->vp37Initialized = true;
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 9100;
  pump->feedback.adjustMiddle =
      (pump->feedback.adjustMax + pump->feedback.adjustMin) / 2;
  pump->demand.target = -1;
  pump->demand.desired = -1;
  pump->demand.requestedPercent = -1.0f;
  pump->demand.topArrivalDecel = VP37_TOP_ARRIVAL_DECEL_PERCENT_PER_S2;
  pump->feedforward.motionRateCap = VP37_PWM_FF_MOTION_RATE_CAP_PERCENT_PER_S;
  pump->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  pump->pid.topKd = VP37_PID_TOP_KD;
  pump->pid.integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
  pump->feedforward.motionBoostUp = VP37_PWM_FF_MOTION_BOOST;
  pump->feedforward.motionBoostDown = VP37_PWM_FF_DESCENT_BOOST;
  pump->thermal.temperatureCorrection = 1.0f;
  pump->thermal.temperatureCompensationWeight = 1.0f;
  pump->thermal.driveResistance = VP37_DRIVE_REFERENCE_OHMS;
  pump->thermal.driveCorrection = 1.0f;
  pump->supply.heldVolts = NOMINAL_VOLTAGE;
  pump->supply.ready = false;
  pump->feedback.lastStatus = ADJ_STATUS_OK;
  VP37_setVP37PID(pump, VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, false);
  hal_pid_controller_set_tf(pump->pid.controller, VP37_PID_TF);
  hal_pid_controller_set_max_integral(pump->pid.controller,
                                      VP37_PID_MAX_INTEGRAL);
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

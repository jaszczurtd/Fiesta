#pragma once
// Pump fixture of the module's own tests: fake board services with a scripted
// Adjustometer transfer, no ECU sources. The transfer keeps the semantics the
// suite was written against on the I2C mock: a busy bus refuses the transfer,
// a script serves its bytes in order, and an exhausted script serves zeros.
#include "../vp37_internal.h"
#include "hal/impl/.mock/hal_mock.h"
#include "vp37_pump_setup.h"
#include <string.h>

/** @brief A scripted sequence of Adjustometer frames, served byte by byte. */
typedef struct {
  uint8_t bytes[255];
  int length;
} AdjustometerScript;

static struct {
  uint8_t script[512];
  int scriptLength;
  int scriptPosition;
  bool busy;
  int32_t lastQuantityPwm;
  int32_t lastTimingPwm;
  bool driveEnabled;
} s_vp37Board;

/** @brief Replace the transfer script, like one hal_mock_i2c_inject_rx(). */
static inline void injectAdjustometerBytes(const uint8_t *data, int length) {
  if (length > (int)sizeof(s_vp37Board.script)) {
    length = (int)sizeof(s_vp37Board.script);
  }
  memcpy(s_vp37Board.script, data, (size_t)length);
  s_vp37Board.scriptLength = length;
  s_vp37Board.scriptPosition = 0;
}

/** @brief Refuse every transfer, like a busy I2C bus. */
static inline void vp37AdjustometerTransferBusy(bool busy) {
  s_vp37Board.busy = busy;
}

static inline hal_status_t vp37FixtureTransfer(uint8_t reg, uint8_t *data,
                                               size_t length) {
  (void)reg; /* The scripts already lie in read order. */
  if (s_vp37Board.busy) {
    return HAL_EBUS;
  }
  for (size_t i = 0U; i < length; i++) {
    data[i] = (s_vp37Board.scriptPosition < s_vp37Board.scriptLength)
                  ? s_vp37Board.script[s_vp37Board.scriptPosition++]
                  : 0U;
  }
  return HAL_OK;
}

static inline void vp37FixtureWriteQuantity(int32_t command) {
  s_vp37Board.lastQuantityPwm = command;
}
static inline void vp37FixtureWriteTiming(int32_t command) {
  s_vp37Board.lastTimingPwm = command;
}
static inline void vp37FixtureSetEnabled(bool enabled) {
  s_vp37Board.driveEnabled = enabled;
}
static inline bool vp37FixtureEnabled(void) { return s_vp37Board.driveEnabled; }

/** @brief The fake board services every module test drives the pump with. */
static inline const VP37Callbacks *vp37ModuleCallbacks(void) {
  static const VP37Callbacks callbacks = {
      .writeQuantityPwm = vp37FixtureWriteQuantity,
      .writeTimingPwm = vp37FixtureWriteTiming,
      .setDriveEnabled = vp37FixtureSetEnabled,
      .driveEnabled = vp37FixtureEnabled,
      .adjustometerTransfer = vp37FixtureTransfer,
  };
  return &callbacks;
}

/** @brief The pump under test; storage owned by the fixture. */
static inline VP37Pump *vp37TestPump(void) {
  static VP37Pump pump;
  return &pump;
}

/** @brief Feed the supply divider the module reads through the HAL ADC. */
static inline void injectLocalSupplyVoltage(float volts) {
  const float ratio =
      ((float)VP37_SUPPLY_DIVIDER_R1 + (float)VP37_SUPPLY_DIVIDER_R2) /
      (float)VP37_SUPPLY_DIVIDER_R2;
  int adc = (int)((volts / ratio) * (4095.0f / 3.3f) + 0.5f);
  adc = hal_constrain(adc, 0, 4095);
  hal_mock_adc_inject(VP37_SUPPLY_ADC_PIN, adc);
}

static inline void appendAdjRegisterData(AdjustometerScript *script,
                                         int16_t pulseHz, uint8_t voltage,
                                         uint8_t fuelTemp, uint8_t status) {
  if (script->length + 5 > (int)sizeof(script->bytes)) {
    return;
  }
  script->bytes[script->length++] = (uint8_t)((uint16_t)pulseHz >> 8);
  script->bytes[script->length++] = (uint8_t)((uint16_t)pulseHz & 0xFF);
  script->bytes[script->length++] = voltage;
  script->bytes[script->length++] = fuelTemp;
  script->bytes[script->length++] = status;
}

static inline void appendAdjRegisterDataRepeated(AdjustometerScript *script,
                                                 int count, int16_t pulseHz,
                                                 uint8_t voltage,
                                                 uint8_t fuelTemp,
                                                 uint8_t status) {
  for (int i = 0; i < count; i++) {
    appendAdjRegisterData(script, pulseHz, voltage, fuelTemp, status);
  }
}

static inline void injectAdjustometerScript(const AdjustometerScript *script) {
  injectAdjustometerBytes(script->bytes, script->length);
}

/** @brief One legacy frame plus the matching local supply reading. */
static inline void injectAdjRegisterData(int16_t pulseHz, uint8_t voltage,
                                         uint8_t fuelTemp, uint8_t status) {
  AdjustometerScript script = {.bytes = {0}, .length = 0};
  appendAdjRegisterData(&script, pulseHz, voltage, fuelTemp, status);
  injectAdjustometerScript(&script);
  injectLocalSupplyVoltage((float)voltage * 0.1f);
}

#ifdef __cplusplus
/** @brief One versioned fast frame in the transfer script. */
static inline void injectFastAdjustometer(const adjustometer_feedback_t &sample,
                                          uint8_t sequence = 2U) {
  uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES];
  adjustometer_feedback_encode(frame, &sample, sequence);
  injectAdjustometerBytes(frame, (int)COUNTOF(frame));
}
#endif

/** @brief Common part of setUp(): clock, board state, fresh script. */
static inline void setUpVp37Fixture(void) {
  hal_mock_set_millis(0);
  memset(&s_vp37Board, 0, sizeof(s_vp37Board));
  s_vp37Board.lastQuantityPwm = -1;
  s_vp37Board.lastTimingPwm = -1;
}

/** @brief A calibrated pump ready for VP37_process(). */
static inline void setupPumpForProcessTests(VP37Pump *pump,
                                            bool fastFrame = false) {
  vp37SetupCalibratedPump(pump, vp37ModuleCallbacks(), fastFrame);
  injectLocalSupplyVoltage(14.0f);
}

#include "chargeLamp.h"
#include "can.h"
#include "timingDiag.h"

#include <math.h>

void ChargeLampConfirmation::clear() { holding = false; }

bool ChargeLampConfirmation::update(bool condition, uint32_t nowMs,
                                    uint32_t holdMs) {
  if (!condition) {
    holding = false;
    return false;
  }
  if (!holding) {
    holding = true;
    sinceMs = nowMs;
  }
  return hal_elapsed_u32(nowMs, sinceMs, holdMs);
}

ChargeLamp::ChargeLamp() { reset(); }

void ChargeLamp::reset() {
  engineRunning = false;
  startedMs = 0u;
  charging = false;
  overvoltage = false;
  overvoltageSinceMs = 0u;
  low.clear();
  ok.clear();
  high.clear();
}

bool ChargeLamp::process(const ChargeLampInput &input) {
  // Without fresh data or with the engine stopped the lamp is lit, and the
  // next start has to confirm charging again.
  if (!input.rpmFresh || !input.voltsFresh || input.rpm < RPM_MIN) {
    reset();
    return true;
  }
  if (!engineRunning) {
    engineRunning = true;
    startedMs = input.nowMs;
  }
  if (!hal_elapsed_u32(input.nowMs, startedMs, CHARGE_LAMP_START_SETTLE_MS)) {
    return true;
  }

  // Between both limits neither confirmation runs and charging stays as it
  // was.
  const int32_t mv = input.millivolts;
  if (low.update(mv < CHARGE_LAMP_LOW_MV, input.nowMs,
                 CHARGE_LAMP_CONFIRM_MS)) {
    charging = false;
  }
  if (ok.update(mv > CHARGE_LAMP_CHARGING_MV, input.nowMs,
                CHARGE_LAMP_CONFIRM_MS)) {
    charging = true;
  }

  // Too high a voltage is tracked apart from charging, so the lamp shows the
  // charging state again once it ends.
  const bool overvoltageNow =
      high.update(mv > CHARGE_LAMP_OVERVOLTAGE_MV, input.nowMs,
                  CHARGE_LAMP_OVERVOLTAGE_CONFIRM_MS);
  if (overvoltageNow && !overvoltage) {
    overvoltageSinceMs = input.nowMs;
  }
  overvoltage = overvoltageNow;
  if (overvoltage) {
    const uint32_t halfPeriods = (uint32_t)(input.nowMs - overvoltageSinceMs) /
                                 (uint32_t)CHARGE_LAMP_BLINK_HALF_MS;
    return (halfPeriods % 2u) == 0u;
  }
  return !charging;
}

static ChargeLamp chargeLamp;

void initChargeLamp(void) {
  chargeLamp.reset();
  hal_gpio_set_mode(JH_HW_NODE_CHARGE_LAMP_PIN_OUT, HAL_GPIO_OUTPUT);
  hal_gpio_write(JH_HW_NODE_CHARGE_LAMP_PIN_OUT, true);
}

void updateChargeLamp(void) {
  ChargeLampInput input;
  input.nowMs = hal_millis();
  input.rpmFresh =
      canValueFresh(F_RPM, input.nowMs, CHARGE_LAMP_DATA_TIMEOUT_MS);
  input.voltsFresh =
      canValueFresh(F_VOLTS, input.nowMs, CHARGE_LAMP_DATA_TIMEOUT_MS);
  input.rpm = (int)canGetValue(F_RPM);
  input.millivolts = (int32_t)lroundf(canGetValue(F_VOLTS) * 1000.0f);
  const bool lit = chargeLamp.process(input);
  hal_gpio_write(JH_HW_NODE_CHARGE_LAMP_PIN_OUT, lit);
  timingDiagChargeLamp(lit, input.millivolts);
}

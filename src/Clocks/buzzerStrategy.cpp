#include "buzzerStrategy.h"

namespace {
bool intervalElapsed(unsigned long nowMs, unsigned long lastMs, unsigned long intervalMs) {
  return (unsigned long)(nowMs - lastMs) >= intervalMs;
}
}

BuzzerStrategy::BuzzerStrategy() {
  reset();
}

void BuzzerStrategy::reset() {
  coolantOilOverheat = false;
  egtOverheat = false;
  coolantOilHasAlarmTime = false;
  egtHasAlarmTime = false;
  lastCoolantOilAlarmMs = 0;
  lastEgtAlarmMs = 0;
}

int BuzzerStrategy::process(const BuzzerStrategyInput &input) {
  if(!input.engineRunning) {
    reset();
    return BUZZER_STRATEGY_NONE;
  }

  const bool coolantOverheatNow = input.coolantTemp > BUZZER_COOLANT_OVERHEAT_TEMP;
  const bool oilOverheatNow = input.oilTemp > BUZZER_OIL_OVERHEAT_TEMP;
  const bool coolantOilOverheatNow = coolantOverheatNow || oilOverheatNow;
  const bool egtOverheatNow = input.egtTemp > BUZZER_EGT_OVERHEAT_TEMP;

  int result = BUZZER_STRATEGY_NONE;

  if(coolantOilOverheatNow) {
    bool shouldFire = !coolantOilOverheat || !coolantOilHasAlarmTime;
    if(!shouldFire) {
      shouldFire = intervalElapsed(input.nowMs, lastCoolantOilAlarmMs, BUZZER_COOLANT_OIL_REPEAT_INTERVAL_MS);
    }
    if(shouldFire) {
      result = BUZZER_STRATEGY_MIDDLE;
      lastCoolantOilAlarmMs = input.nowMs;
      coolantOilHasAlarmTime = true;
    }
  } else {
    coolantOilHasAlarmTime = false;
  }

  if(egtOverheatNow) {
    bool shouldFire = !egtOverheat || !egtHasAlarmTime;
    if(!shouldFire) {
      shouldFire = intervalElapsed(input.nowMs, lastEgtAlarmMs, BUZZER_EGT_REPEAT_INTERVAL_MS);
    }
    if(shouldFire) {
      // EGT has higher priority than coolant/oil.
      result = BUZZER_STRATEGY_LONG;
      lastEgtAlarmMs = input.nowMs;
      egtHasAlarmTime = true;
    }
  } else {
    egtHasAlarmTime = false;
  }

  coolantOilOverheat = coolantOilOverheatNow;
  egtOverheat = egtOverheatNow;
  return result;
}

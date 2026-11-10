#include "alerts.h"
#include "buzzer.h"
#include "buzzerStrategy.h"
#include "can.h"
#include "testOutputs.h"

static BuzzerStrategy s_strategy;
static bool s_startBeepDone = false;

void resetBuzzerAlerts(void) {
  s_strategy.reset();
  s_startBeepDone = false;
}

void processStartBeep(bool environmentStarted) {
  if (s_startBeepDone || !environmentStarted ||
      testOutputsOwned(TEST_OUTPUT_BUZZER)) {
    return;
  }
  s_startBeepDone = true;
  startBuzzer(BUZZER_SHORT);
}

void processTemperatureBuzzerAlerts(const clocks_local_snapshot_t *local) {
  if (testOutputsOwned(TEST_OUTPUT_BUZZER)) {
    return;
  }
  BuzzerStrategyInput input;
  input.engineRunning = (int(canGetValue(F_RPM)) != 0);
  input.nowMs = hal_millis();
  input.coolantTemp = int(canGetValue(F_COOLANT_TEMP));
  input.oilTemp = int(canGetValue(F_OIL_TEMP));
  input.egtTemp =
      local->egtValid[EGT_PRE_DPF] ? int(local->egtC[EGT_PRE_DPF]) : 0;

  switch (s_strategy.process(input)) {
  case BUZZER_STRATEGY_MIDDLE:
    startBuzzer(BUZZER_MIDDLE);
    break;
  case BUZZER_STRATEGY_LONG:
    startBuzzer(BUZZER_LONG);
    break;
  default:
    break;
  }
}

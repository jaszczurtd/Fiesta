#ifndef C_BUZZER_STRATEGY
#define C_BUZZER_STRATEGY

#include "config.h"

enum {
  BUZZER_STRATEGY_NONE = -1,
  BUZZER_STRATEGY_MIDDLE = 1,
  BUZZER_STRATEGY_LONG = 2
};

struct BuzzerStrategyInput {
  bool engineRunning;
  unsigned long nowMs;
  int coolantTemp;
  int oilTemp;
  int egtTemp;
};

class BuzzerStrategy {
public:
  BuzzerStrategy();
  void reset();
  int process(const BuzzerStrategyInput &input);

private:
  bool coolantOilOverheat;
  bool egtOverheat;
  bool coolantOilHasAlarmTime;
  bool egtHasAlarmTime;
  unsigned long lastCoolantOilAlarmMs;
  unsigned long lastEgtAlarmMs;
};

#endif

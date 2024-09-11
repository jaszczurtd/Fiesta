#ifndef T_FAN
#define T_FAN

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"
#include "canDefinitions.h"

#include "EngineController.h"

enum {
  FAN_REASON_NONE, FAN_REASON_COOLANT, FAN_REASON_AIR
};

class engineFan : public EngineController {
public:
  engineFan();
  void init() override;  
  void process() override;
  void showDebug() override;
  bool isFanEnabled(void);
  void fan(bool enable);

private:
  int fanEnabled;
  int lastFanStatus;

  int fanEnabledReason(void);
};

engineFan *getFanInstance(void);
void createFan(void);

#endif
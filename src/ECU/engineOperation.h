#ifndef T_ENGINE_OPERATION
#define T_ENGINE_OPERATION

#include <tools.h>
#include <arduino-timer.h>
#include <pidController.h>
#include <pico.h>
#include <hardware/timer.h>

#include "config.h"
#include "start.h"
#include "hardwareConfig.h"
#include "tests.h"

#include "engineMaps.h"
#include "EngineController.h"

class EngineOperation : public EngineController {
private:

  bool engineInitialized;
  int lastThrottle;
  int desiredAdjustometer;

public:
  EngineOperation();
  void init() override;  
  void process() override;
  void tickPumpTimer(void);

  void showDebug(void);
};

class VP37Pump;
class Turbo;

VP37Pump& getInjectionPump();
Turbo& getTurbo();

#endif

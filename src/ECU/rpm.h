
#ifndef T_RPM
#define T_RPM

#include <arduino-timer.h>
#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

#include "EngineController.h"

//is it really needed? To evaluate later
#define RPM_CORRECTION_VAL 50

//tweakable value: 
//by how much percentage should the solenoid position change from RPM?
#define RPM_PERCENTAGE_CORRECTION_VAL 5

//since solenoid has some time lag we have to compensate it
//values defined as ms
#define RPM_TIME_TO_POSITIVE_CORRECTION_RPM_PERCENTAGE 500
#define RPM_TIME_TO_NEGATIVE_CORRECTION_RPM_PERCENTAGE 600

class RPM : public EngineController {
public:
  RPM();
  void init() override;  
  void process() override;
  void showDebug() override;
  void setAccelMaxRPM(void);
  void resetRPMEngine(void);
#ifndef VP37
  void stabilizeRPM(void);
#endif
  bool isEngineRunning(void);
  void setAccelRPMPercentage(int percentage);
  int getCurrentRPMSolenoid(void);
  void interrupt(void);
  void resetRPMCycle(void);

private:
  volatile unsigned long previousMillis;
  volatile unsigned long shortPulse;
  volatile unsigned long lastPulse;
  volatile long rpmAliveTime;
  volatile int RPMpulses;

  int currentRPMSolenoid;
  bool rpmCycle;
#ifndef VP37
  int rpmPercentValue;
#endif

  bool isEngineThrottlePressed(void);
  int getCurrentRPM(void);
};

RPM *getRPMInstance(void);
void createRPM(void);


#endif

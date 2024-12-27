#include "engineOperation.h"

static VP37Pump injectionPump;
static Turbo turbo;
static Timer engineMainTimer;

VP37Pump& getInjectionPump() {
    return injectionPump;
}

Turbo& getTurbo() {
    return turbo;
}


//static repeating_timer_t timer;
//bool vp37Interrupt(repeating_timer_t *rt) {
//  injectionPump.interrupt();
//  return true;
//}

EngineOperation::EngineOperation() { }

void EngineOperation::tickPumpTimer(void) {
  injectionPump.VP37TickMainTimer();
}

void EngineOperation::init(void) {

  injectionPump.init();
  turbo.init();

  engineMainTimer = timer_create_default();

  //noInterrupts();
  //add_repeating_timer_ms(injectionPump.getVP37PIDTimeUpdate() / 2, vp37Interrupt, NULL, &timer);    
  //interrupts();

  engineInitialized = true;
}

void EngineOperation::process(void) {
  if(!engineInitialized) {
    return;
  }

  engineMainTimer.tick();  
  turbo.process();

#ifndef TEST_CYCLIC

  int thr = getThrottlePercentage();

  if(lastThrottle != thr || desiredAdjustometer < 0) {
    lastThrottle = thr;
    desiredAdjustometer = map(thr, 0, 100, injectionPump.getMinVP37ThrottleValue(), 
                                           injectionPump.getMaxVP37ThrottleValue());

    injectionPump.setVP37Throttle(desiredAdjustometer);
  }

#endif

  injectionPump.process();

}


void EngineOperation::showDebug(void) {
  if(!engineInitialized) {
    return;
  }  
  
  turbo.showDebug();
  injectionPump.showDebug();

}

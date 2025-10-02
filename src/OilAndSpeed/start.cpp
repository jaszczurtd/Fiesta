
#include "start.h"

bool updateValsForDebug(void *arg);

static Timer generalTimer;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

static int *wValues = NULL;
static int wSize = 0;
void executeByWatchdog(int *values, int size) {
  wValues = values;
  wSize = size;
}

void setupTimerWith(unsigned long time, bool(*function)(void *argument)) {
  //watchdog_feed();
  generalTimer.every(time, function);
  m_delay(CORE_OPERATION_DELAY);
}

void setupTimers(void) {

  generalTimer = timer_create_default();

  setupTimerWith(CAN_UPDATE_RECIPIENTS, updateCANrecipients);
  setupTimerWith(CAN_MAIN_LOOP_READ_INTERVAL, canMainLoop);
  setupTimerWith(CAN_CHECK_CONNECTION, canCheckConnection);  
  setupTimerWith(DEBUG_UPDATE, updateValsForDebug);
}

void initialization(void) {
  bool result = false;

  debugInit();
  setDebugPrefix("OIL&SPD:");
  setupOnboardLed();
  initBasicPIO();

  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  if(!rebooted) {
    statusVariable0 = statusVariable1 = 0;
  }

  initSPI();
  
  result = canInit();
  setLEDColor(result ? RED: GREEN);
  if(result) {
    deb("cannot setup CAN, exiting");
    return;
  }

  updateCANrecipients(NULL);
  canMainLoop(NULL);
  updateValsForDebug(NULL);

  watchdog_feed();
  setupTimers();
  result = setupSpeedometer();
  setLEDColor(result ? GREEN: YELLOW);
  if(!result) {
    deb("cannot setup speedometer, exiting");
    return;
  }

  setStartedCore0();
}

void looper() {

  statusVariable0 = 0;
  updateWatchdogCore0();

  if(!isEnvironmentStarted()) {
    statusVariable0 = -1;
    m_delay(CORE_OPERATION_DELAY);  
    tight_loop_contents();
    return;
  }

  statusVariable0 = 1;

  generalTimer.tick();
  onImpulseTranslating();
  canSendLoop();

  m_delay(CORE_OPERATION_DELAY);  
  tight_loop_contents();
}


void initialization1() {
  setStartedCore1();
}

void looper1() {
  updateWatchdogCore1();

  if(!isEnvironmentStarted()) {
    statusVariable1 = -1;
    m_delay(CORE_OPERATION_DELAY);  
    tight_loop_contents();
    return;
  }
  statusVariable1 = 1;

  m_delay(CORE_OPERATION_DELAY);  
  tight_loop_contents();
}

bool updateValsForDebug(void *arg) {

  deb("ECU:%s, cluster:%s, circumference: %f ", isEcuConnected() ? "on" : "off", 
                                                isClusterConnected() ? "on" : "off",
                                                getCircumference());
  

  return true;
}
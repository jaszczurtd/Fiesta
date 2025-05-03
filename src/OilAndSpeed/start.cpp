
#include "start.h"

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
}

void initialization(void) {
  debugInit();
  setupOnboardLed();
  initBasicPIO();


  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  if(!rebooted) {
    statusVariable0 = statusVariable1 = 0;
  }

  initSPI();

  setLEDColor(canInit() ? RED: GREEN);

  updateCANrecipients(NULL);
  canMainLoop(NULL);

  watchdog_feed();
  setupTimers();
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
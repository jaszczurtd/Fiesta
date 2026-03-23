
#include "start.h"

void updateValsForDebug(void);

static SmartTimers timerCANUpdate;
static SmartTimers timerCANLoop;
static SmartTimers timerCANCheck;
static SmartTimers timerOilPressure;
static SmartTimers timerDebug;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

static int *wValues = NULL;
static int wSize = 0;
void executeByWatchdog(int *values, int size) {
  wValues = values;
  wSize = size;
}

void setupTimers(void) {
  timerCANUpdate.begin(updateCANrecipients, CAN_UPDATE_RECIPIENTS);    m_delay(CORE_OPERATION_DELAY);
  timerCANLoop.begin(canMainLoop, CAN_MAIN_LOOP_READ_INTERVAL);        m_delay(CORE_OPERATION_DELAY);
  timerCANCheck.begin(canCheckConnection, CAN_CHECK_CONNECTION);       m_delay(CORE_OPERATION_DELAY);
  timerOilPressure.begin(readOilPressure, OIL_PRESSURE_READ_INTERVAL); m_delay(CORE_OPERATION_DELAY);
  timerDebug.begin(updateValsForDebug, DEBUG_UPDATE);
}

void initialization(void) {
  bool result = false;

  debugInit();
  setDebugPrefix("OIL&SPD:");
  setupOnboardLed();
  initBasicPIO();

  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  if (!rebooted) {
    statusVariable0 = statusVariable1 = 0;
  }

  initSPI();

  result = canInit();
  setLEDColor(result ? RED: GREEN);
  if (result) {
    derr("cannot setup CAN, exiting");
    return;
  }

  updateCANrecipients();
  canMainLoop();
  updateValsForDebug();

  watchdog_feed();

  result = setupOilPressure();
  if (!result) {
    derr("cannot setup oil pressure readout, exiting");
    return;
  }

  setupTimers();

  result = setupSpeedometer();
  setLEDColor(result ? GREEN: YELLOW);
  if (!result) {
    derr("cannot setup speedometer, exiting");
    return;
  }

  setStartedCore0();
}

void looper() {

  statusVariable0 = 0;
  updateWatchdogCore0();

  if (!isEnvironmentStarted()) {
    statusVariable0 = -1;
    m_delay(CORE_OPERATION_DELAY);
    hal_idle();
    return;
  }

  statusVariable0 = 1;

  timerCANUpdate.tick();
  timerCANLoop.tick();
  timerCANCheck.tick();
  timerOilPressure.tick();
  timerDebug.tick();
  onImpulseTranslating();
  canSendLoop();

  m_delay(CORE_OPERATION_DELAY);
  hal_idle();
}


void initialization1() {
  setStartedCore1();
}

void looper1() {
  updateWatchdogCore1();

  if (!isEnvironmentStarted()) {
    statusVariable1 = -1;
    m_delay(CORE_OPERATION_DELAY);
    hal_idle();
    return;
  }
  statusVariable1 = 1;

  m_delay(CORE_OPERATION_DELAY);
  hal_idle();
}

void updateValsForDebug(void) {
  deb("ECU:%s, cluster:%s, circumference:%f, oil:%fBAR", isEcuConnected() ? "on" : "off",
                                                          isClusterConnected() ? "on" : "off",
                                                          getCircumference(),
                                                          getGlobalValue(F_OIL_PRESSURE));
}

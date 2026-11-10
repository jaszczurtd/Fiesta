// The core-0 pass of logic.cpp with the TIMING statistics: the loop time
// (l0_max) covers the functional-test tick that opens every pass, while the
// receive section starts after it. Everything else the pass and the rest of
// logic.cpp call is a stub without work.
#include "alerts.h"
#include "chargeLamp.h"
#include "clocksTests.h"
#include "clusterOutput.h"
#include "egt.h"
#include "gps.h"
#include "hal/impl/.mock/hal_mock.h"
#include "logic.h"
#include "oilLamp.h"
#include "oilPressure.h"
#include "speed.h"
#include "timingDiag.h"
#include "unity.h"
#include <utils/multicoreWatchdog.h>

#include <cstdlib>
#include <cstring>

extern "C" void app_task0(void);

// A test step whose outputs take this long to write.
static uint32_t s_tickUs = 0u;
bool tickClocksTests(void) {
  hal_mock_advance_micros(s_tickUs);
  return false;
}

float valueFields[F_LAST];
void canCheckConnection(void) {}
hal_status_t canInit(void) { return HAL_OK; }
void canMainLoop(void) {}
bool canRetryInit(void) { return false; }
bool canRxPending(void) { return true; } // the loop's wait returns at once
void canSetAbsReady(bool) {}
bool canSetupBroadcastTimers(void) { return true; }
void canTickBroadcastTimers(void) {}
void canTickRpmTimeout(void) {}
void changeEGT(void) {}
bool clocksGpsReadChanged(clocks_gps_snapshot_t *) { return false; }
void clocksStateCopyLocal(clocks_local_snapshot_t *) {}
void clocksStateInit(void) {}
void clocksStatePublishLocal(const clocks_local_snapshot_t *) {}
bool clocksStateRefreshView(void) { return false; }
void clocksTestsEvents(void) {}
void configSessionInit(void) {}
void configSessionTick(void) {}
void drawFuelEmpty(void) {}
void egtInit(void) {}
void egtRead(clocks_local_snapshot_t *, uint32_t) {}
int getCurrentCarSpeed(void) { return 0; }
int getEngineRPM(void) { return 0; }
float getOilPressure(void) { return 0.0f; }
void initBasicPIO(void) {}
void initChargeLamp(void) {}
bool initClocksTests(void) { return true; }
void initClusterOutput(void) {}
void initFuelMeasurement(void) {}
void initGPS(void) {}
void initOilLamp(void) {}
void initSPI(void) {}
void initTFT(void) {}
bool isDPFConnected(void) { return false; }
bool isEcuConnected(void) { return false; }
bool isEgtValid(int) { return false; }
bool isEngineRpmValid(void) { return false; }
bool isEnvironmentStarted(void) { return true; }
bool isOilPressureValid(void) { return false; }
void loopBuzzers(void) {}
void onImpulseTranslating(void) {}
void processStartBeep(bool) {}
void processTemperatureBuzzerAlerts(const clocks_local_snapshot_t *) {}
oil_sender_state_t readOilPressure(float *) { return (oil_sender_state_t)0; }
void redrawAllGauges(void) {}
void resetBuzzerAlerts(void) {}
void setStartedCore0(void) {}
void setStartedCore1(void) {}
bool setupOilPressure(void) { return true; }
void setupOnboardLed(void) {}
bool setupSpeedometer(void) { return true; }
bool setupWatchdog(void (*)(int *, int), unsigned int) { return false; }
void showECUConnectionGauge(void) {}
void showEGTGauge(void) {}
void showEngineLoadGauge(void) {}
void showFuelAmount(void) {}
void showGPSGauge(void) {}
void showPressureGauges(void) {}
void showRpmGauge(void) {}
void showSimpleGauges(void) {}
void showTempGauges(void) {}
void updateCANrecipients(void) {}
void updateChargeLamp(void) {}
void updateCluster(void) {}
void updateOilLamp(void) {}
void updateWatchdogCore0(void) {}
void updateWatchdogCore1(void) {}

static long field(const char *line, const char *name) {
  const char *at = strstr(line, name);
  TEST_ASSERT_NOT_NULL_MESSAGE(at, name);
  return strtol(at + strlen(name), nullptr, 10);
}

// Close the statistics windows of both cores; the display core prints the
// T0 report of the receive core.
static const char *closeWindows(void) {
  hal_mock_advance_millis(TIMING_DIAG_REPORT_MS);
  timingDiagLoopEnd(0u, timingDiagLoopStart());
  timingDiagLoopEnd(1u, timingDiagLoopStart());
  return hal_mock_serial_last_line();
}

void setUp(void) {}
void tearDown(void) {}

void test_loop_time_covers_the_test_tick_and_receive_does_not(void) {
  static const uint32_t kTickUs = 3700u;
  (void)closeWindows(); // start from an empty window
  s_tickUs = kTickUs;
  app_task0();
  s_tickUs = 0u;

  const char *line = closeWindows();
  TEST_ASSERT_NOT_NULL(strstr(line, "T0 "));
  TEST_ASSERT_TRUE(field(line, "l0_max:") >= (long)kTickUs);
  TEST_ASSERT_TRUE(field(line, "phases:") < (long)kTickUs); // receive section
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_loop_time_covers_the_test_tick_and_receive_does_not);
  return UNITY_END();
}

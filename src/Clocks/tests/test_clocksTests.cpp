// The five cluster tests behind the configurator operations: every step
// reaches the real gauge, lamp and buzzer outputs, the measurements keep
// flowing to CAN, and every exit hands the outputs back to the measurements.
#include "../../common/scDefinitions/sc_protocol.h"
#include "alerts.h"
#include "buzzer.h"
#include "can.h"
#include "chargeLamp.h"
#include "clocksState.h"
#include "clocksTests.h"
#include "clusterOutput.h"
#include "config.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hardwareConfig.h"
#include "oilLamp.h"
#include "testOutputs.h"
#include "unity.h"

#include <climits>
#include <cstring>

hal_can_t clocksTestGetCanHandle(void);
float *clocksTestCanValues(void);
int getLcdBrightness(void) { return 0; }

#define canValues (clocksTestCanValues())

static const uint8_t kTacho = JH_HW_NODE_TACHO_OUTPUT_PIN_OUT;
static const uint8_t kSpeedo = JH_HW_NODE_SPEED_OUTPUT_PIN_OUT;
static const uint8_t kCharge = JH_HW_NODE_CHARGE_LAMP_PIN_OUT;
static const uint8_t kOil = JH_HW_NODE_OIL_LAMP_PIN_OUT;
static const uint8_t kBuzzer = JH_HW_NODE_BUZZER_PIN_OUT;
static const uint32_t kStepMs = CLOCKS_TEST_STEP_MS_MIN; // faster cases

static const sc_command_test_ops_t *ops(void) { return clocksTestsScOps(); }

static sc_command_test_status_t status(void) {
  sc_command_test_status_t out = {};
  ops()->status(nullptr, &out);
  return out;
}

static int32_t field(const sc_command_test_status_t &s, const char *key) {
  for (size_t i = 0u; i < s.field_count; i++) {
    if (strcmp(s.fields[i].key, key) == 0) {
      return s.fields[i].value;
    }
  }
  return INT32_MIN;
}

static const char *textField(const sc_command_test_status_t &s,
                             const char *key) {
  for (size_t i = 0u; i < s.field_count; i++) {
    if (strcmp(s.fields[i].key, key) == 0) {
      return s.fields[i].text;
    }
  }
  return nullptr;
}

// Both clocks of the mock: millis drive the steps and the lamp phases, the
// alarm clock fires the gauge timers and the buzzer runs on millis.
static void advance(uint32_t ms) {
  hal_mock_advance_millis(ms);
  hal_mock_timer_advance_us((uint64_t)ms * 1000u);
}

// One pass of core 0 in the order of runCore0(): the tests first, then the
// outputs the loop and its timers write. The runner refreshes the polled
// status every 20 ms, so a pass that is read back afterwards lasts at least
// that long.
static const uint32_t kPassMs = 20u;
static void pass(uint32_t ms) {
  advance(ms);
  (void)tickClocksTests();
  (void)status(); // the configurator keeps polling
  updateCluster();
  updateChargeLamp();
  updateOilLamp();
  loopBuzzers();
}

static void passes(uint32_t totalMs, uint32_t stepMs) {
  for (uint32_t t = 0u; t < totalMs; t += stepMs) {
    pass(stepMs);
  }
}

// Rising edges of a gauge output over one second of passes. The mock fires
// an alarm at most once per advance and reschedules it from the time of that
// advance, so the alarm clock moves in 20 us quanta under the passes.
static const uint32_t kAlarmQuantumUs = 20u;
static uint32_t edgesPerSecond(uint8_t pin) {
  const uint32_t before = hal_mock_gpio_rise_count(pin);
  for (uint32_t ms = 0u; ms < 1000u; ms += kPassMs) {
    for (uint32_t us = 0u; us < kPassMs * 1000u; us += kAlarmQuantumUs) {
      hal_mock_timer_advance_us(kAlarmQuantumUs);
    }
    hal_mock_advance_millis(kPassMs);
    (void)tickClocksTests();
    (void)status();
    updateCluster();
    updateChargeLamp();
    updateOilLamp();
    loopBuzzers();
  }
  return hal_mock_gpio_rise_count(pin) - before;
}

// The same second, with the other gauge required to stay quiet.
static uint32_t edgesPerSecondWhileQuiet(uint8_t pin, uint8_t quietPin) {
  const uint32_t quietBefore = hal_mock_gpio_rise_count(quietPin);
  const uint32_t edges = edgesPerSecond(pin);
  TEST_ASSERT_EQUAL_UINT32(quietBefore, hal_mock_gpio_rise_count(quietPin));
  return edges;
}

// Nominal edges per second of the tachometer and the speedometer, from the
// Cluster calibration (0.0332143 * rpm + 2.14286; the speed table).
static const uint32_t kEdges1000Rpm = 35u;
static const uint32_t kEdges2000Rpm = 68u;
static const uint32_t kEdges3000Rpm = 101u;
static const uint32_t kEdges20Kmh = 24u;
static const uint32_t kEdges60Kmh = 80u;
static const uint32_t kEdgeTolerance = 2u;

// Passes until the running test shows @p step; a step lasts kStepMs.
static void passUntilStep(int32_t step) {
  for (uint32_t t = 0u;
       (t < 2u * kStepMs) && (field(status(), SC_TEST_FIELD_STEP) != step);
       t += kPassMs) {
    pass(kPassMs);
  }
  TEST_ASSERT_EQUAL_INT32(step, field(status(), SC_TEST_FIELD_STEP));
}

// Length of a signal on 1 ms loop passes, from its start to the buzzer
// falling idle.
static uint32_t signalLengthMs(int signal) {
  stopAllBuzzers();
  startBuzzer(signal);
  uint32_t ms = 0u;
  while (Buzzer::isActive() && (ms < 5000u)) {
    hal_mock_advance_millis(1u);
    loopBuzzers();
    ms++;
  }
  return ms;
}

static bool lastDebugLineHas(const char *text) {
  const char *line = hal_mock_deb_last_line();
  return (line != nullptr) && (strstr(line, text) != nullptr);
}

static void runTest(const char *name) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops()->run(nullptr, name));
  (void)tickClocksTests(); // the controller takes the request
  TEST_ASSERT_EQUAL_STRING(name, clocksTestsActiveName());
}

static void ensureHardware(void) {
  static bool once = false;
  if (!once) {
    once = true;
    TEST_ASSERT_EQUAL_INT(HAL_OK, canInit());
    Buzzer::initHardware(kBuzzer);
    initBuzzers();
    initClusterOutput();
  }
  TEST_ASSERT_NOT_NULL(clocksTestGetCanHandle());
  hal_mock_can_reset(clocksTestGetCanHandle());
}

void setUp(void) {
  hal_mock_set_millis(0u);
  ensureHardware();
  clocksStateInit();
  for (int i = 0; i < F_LAST; i++) {
    canValues[i] = 0.0f;
  }
  const clocks_local_snapshot_t noMeasurements = {};
  clocksStatePublishLocal(&noMeasurements);
  canCheckConnection();
  initChargeLamp();
  initOilLamp();
  stopAllBuzzers();
  resetBuzzerAlerts();
  TEST_ASSERT_TRUE(initClocksTests());
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops()->set_param(nullptr, SC_TEST_PARAM_STEP_MS,
                                                 (int32_t)kStepMs));
  pass(1u); // outputs at their measured state
}

void tearDown(void) {
  (void)ops()->stop(nullptr);
  (void)tickClocksTests();
}

void test_registry_lists_the_five_cluster_tests_with_the_shared_step(void) {
  TEST_ASSERT_EQUAL_UINT(5u, ops()->count(nullptr));
  static const char *const names[] = {SC_TEST_NAME_CHARGE, SC_TEST_NAME_OIL,
                                      SC_TEST_NAME_RPM, SC_TEST_NAME_SPEED,
                                      SC_TEST_NAME_BUZZER};
  for (size_t i = 0u; i < 5u; i++) {
    sc_command_test_info_t info = {};
    TEST_ASSERT_TRUE(ops()->info(nullptr, i, &info));
    TEST_ASSERT_EQUAL_STRING(names[i], info.name);
    TEST_ASSERT_TRUE(info.in_sequence);
    TEST_ASSERT_EQUAL_UINT(1u, info.param_count);
    sc_command_test_param_t param = {};
    TEST_ASSERT_TRUE(ops()->param_at(nullptr, i, 0u, &param));
    TEST_ASSERT_EQUAL_STRING(SC_TEST_PARAM_STEP_MS, param.id);
    TEST_ASSERT_EQUAL_STRING(SC_TEST_UNIT_MS, param.unit);
  }
  // The step time is bounded; the shortest step outlasts every buzzer signal.
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        ops()->set_param(nullptr, SC_TEST_PARAM_STEP_MS,
                                         (int32_t)CLOCKS_TEST_STEP_MS_MIN - 1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        ops()->set_param(nullptr, SC_TEST_PARAM_STEP_MS,
                                         (int32_t)CLOCKS_TEST_STEP_MS_MAX + 1));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ops()->set_param(nullptr, SC_TEST_PARAM_STEP_MS,
                                         (int32_t)CLOCKS_TEST_STEP_MS_MAX));
  // Measured on the loop, not summed from the patterns: the opening delay
  // of a signal counts twice (Buzzer::start() and Buzzer::loop()).
  const uint32_t lengths[] = {signalLengthMs(BUZZER_SHORT),
                              signalLengthMs(BUZZER_MIDDLE),
                              signalLengthMs(BUZZER_LONG)};
  uint32_t longest = 0u;
  for (uint32_t length : lengths) {
    TEST_ASSERT_TRUE(length > 0u);
    longest = (length > longest) ? length : longest;
  }
  TEST_ASSERT_EQUAL_UINT32(BUZZER_LONGEST_SIGNAL_MS, longest);
  TEST_ASSERT_TRUE(CLOCKS_TEST_STEP_MS_MIN > longest);
}

void test_rpm_steps_drive_the_tachometer_and_release_to_the_measurement(void) {
  canSetValue(F_RPM, 2000.0f);
  TEST_ASSERT_UINT32_WITHIN(kEdgeTolerance, kEdges2000Rpm,
                            edgesPerSecondWhileQuiet(kTacho, kSpeedo));
  // Steps of 3 s: a measurement, the RPM timeout and another measurement fit.
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ops()->set_param(nullptr, SC_TEST_PARAM_STEP_MS, 3000));

  runTest(SC_TEST_NAME_RPM);
  sc_command_test_status_t s = status();
  TEST_ASSERT_EQUAL_INT32(1, field(s, SC_TEST_FIELD_STEP));
  TEST_ASSERT_EQUAL_INT32(5, field(s, SC_TEST_FIELD_STEPS));
  TEST_ASSERT_EQUAL_INT32(1000, field(s, SC_TEST_FIELD_VALUE));
  clocksTestsEvents(); // the debug log is audible outside a session
  TEST_ASSERT_TRUE(lastDebugLineHas("TEST: rpm step 1/5 1000"));
  // The tachometer shows 1000 rpm, the speedometer keeps its measurement (0).
  TEST_ASSERT_UINT32_WITHIN(kEdgeTolerance, kEdges1000Rpm,
                            edgesPerSecondWhileQuiet(kTacho, kSpeedo));
  TEST_ASSERT_EQUAL_FLOAT(2000.0f, canValues[F_RPM]);

  // Step 2: an RPM frame and the RPM timeout call updateCluster() too, and
  // neither bypasses the test value.
  passes(2000u, kPassMs);
  s = status();
  TEST_ASSERT_EQUAL_INT32(2, field(s, SC_TEST_FIELD_STEP));
  TEST_ASSERT_EQUAL_INT32(3000, field(s, SC_TEST_FIELD_VALUE));
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_RPM_UPDATE_HI] = 0x09;
  frame[CAN_FRAME_RPM_UPDATE_LO] = 0xC4; // 2500 rpm
  hal_mock_can_inject(clocksTestGetCanHandle(), CAN_ID_RPM,
                      CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();
  TEST_ASSERT_EQUAL_FLOAT(2500.0f, canValues[F_RPM]);
  TEST_ASSERT_EQUAL_INT32(3000, field(status(), SC_TEST_FIELD_VALUE));
  TEST_ASSERT_UINT32_WITHIN(kEdgeTolerance, kEdges3000Rpm,
                            edgesPerSecond(kTacho));
  advance(RPM_CAN_TIMEOUT_MS + 1u);
  canTickRpmTimeout(); // silence on the bus expires the measurement
  TEST_ASSERT_EQUAL_FLOAT(0.0f, canValues[F_RPM]);
  TEST_ASSERT_UINT32_WITHIN(kEdgeTolerance, kEdges3000Rpm,
                            edgesPerSecond(kTacho));

  // Steps 3 and 4 pass; the closing zero of step 5 lasts a whole step.
  passes(700u, kPassMs); // 2301 ms of step 2 are over; the boundary is 3000
  TEST_ASSERT_EQUAL_INT32(5000, field(status(), SC_TEST_FIELD_VALUE));
  passes(3000u, 100u);
  TEST_ASSERT_EQUAL_INT32(7000, field(status(), SC_TEST_FIELD_VALUE));
  passes(3000u, 100u);
  TEST_ASSERT_EQUAL_INT32(0, field(status(), SC_TEST_FIELD_VALUE));
  TEST_ASSERT_EQUAL_UINT32(0u, edgesPerSecond(kTacho));
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_RPM, clocksTestsActiveName());
  passes(2000u, 100u);
  TEST_ASSERT_NULL(clocksTestsActiveName());
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_DONE, status().result);
  TEST_ASSERT_FALSE(testOutputsOwned(TEST_OUTPUT_RPM));
  // Released: the expired measurement (0), then a new one, reach the gauge.
  TEST_ASSERT_EQUAL_UINT32(0u, edgesPerSecond(kTacho));
  canSetValue(F_RPM, 2000.0f);
  TEST_ASSERT_UINT32_WITHIN(kEdgeTolerance, kEdges2000Rpm,
                            edgesPerSecond(kTacho));
}

void test_speed_steps_drive_the_speedometer_while_can_carries_the_measurement(
    void) {
  canSetValue(F_ABS_CAR_SPEED, 42.0f);

  runTest(SC_TEST_NAME_SPEED);
  TEST_ASSERT_EQUAL_INT32(20, field(status(), SC_TEST_FIELD_VALUE));
  TEST_ASSERT_EQUAL_INT32(7, field(status(), SC_TEST_FIELD_STEPS));
  TEST_ASSERT_UINT32_WITHIN(kEdgeTolerance, kEdges20Kmh,
                            edgesPerSecondWhileQuiet(kSpeedo, kTacho));
  passUntilStep(2);
  TEST_ASSERT_EQUAL_INT32(60, field(status(), SC_TEST_FIELD_VALUE));
  TEST_ASSERT_UINT32_WITHIN(kEdgeTolerance, kEdges60Kmh,
                            edgesPerSecondWhileQuiet(kSpeedo, kTacho));

  // 0x12A carries the measured 42 km/h and a fresh 2.5 bar reading (they
  // expire after OIL_PRESSURE_MAX_AGE_MS), never the 100 km/h on the gauge.
  passUntilStep(3);
  TEST_ASSERT_EQUAL_INT32(100, field(status(), SC_TEST_FIELD_VALUE));
  clocks_local_snapshot_t local = {};
  local.oilValid = true;
  local.oilBar = 2.5f;
  local.oilUpdatedMs = hal_millis();
  clocksStatePublishLocal(&local);
  hal_mock_can_reset(clocksTestGetCanHandle());
  updateCANrecipients();
  bool seen = false;
  uint32_t id = 0u;
  uint8_t len = 0u;
  uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};
  while (hal_mock_can_get_sent(clocksTestGetCanHandle(), &id, &len, data) ==
         HAL_OK) {
    if (id == CAN_ID_OIL_AND_SPEED_MODULE_UPDATE) {
      seen = true;
      TEST_ASSERT_EQUAL_UINT8(42u, data[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED]);
      TEST_ASSERT_EQUAL_UINT8(2u, data[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI]);
      TEST_ASSERT_EQUAL_UINT8(5u, data[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO]);
    }
  }
  TEST_ASSERT_TRUE(seen);
  TEST_ASSERT_EQUAL_FLOAT(42.0f, canValues[F_ABS_CAR_SPEED]);
}

void test_a_late_pass_shows_one_step_and_the_steps_cross_the_millis_wrap(void) {
  hal_mock_set_millis(UINT32_MAX - kStepMs);
  runTest(SC_TEST_NAME_RPM);
  TEST_ASSERT_EQUAL_INT32(1, field(status(), SC_TEST_FIELD_STEP));
  // Over two steps late (inside the keepalive): only the next state appears.
  const uint32_t lateMs = 2u * kStepMs + kStepMs / 4u;
  TEST_ASSERT_TRUE(lateMs < CLOCKS_SC_TESTS_KEEPALIVE_MS);
  pass(lateMs);
  TEST_ASSERT_EQUAL_INT32(2, field(status(), SC_TEST_FIELD_STEP));
  TEST_ASSERT_EQUAL_INT32(3000, field(status(), SC_TEST_FIELD_VALUE));
  TEST_ASSERT_TRUE(hal_millis() < lateMs); // the clock has wrapped meanwhile
  // Each following state lasts a full step, counted from its own start.
  pass(kStepMs - kPassMs);
  TEST_ASSERT_EQUAL_INT32(2, field(status(), SC_TEST_FIELD_STEP));
  pass(kPassMs);
  TEST_ASSERT_EQUAL_INT32(3, field(status(), SC_TEST_FIELD_STEP));
  pass(kStepMs);
  TEST_ASSERT_EQUAL_INT32(4, field(status(), SC_TEST_FIELD_STEP));
  pass(kStepMs);
  TEST_ASSERT_EQUAL_INT32(5, field(status(), SC_TEST_FIELD_STEP));
  TEST_ASSERT_EQUAL_INT32(0, field(status(), SC_TEST_FIELD_VALUE));
  pass(kStepMs);
  TEST_ASSERT_NULL(clocksTestsActiveName());
}

void test_charge_lamp_steps_reach_the_output_at_once_and_release(void) {
  // No fresh data: the lamp logic keeps it lit.
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kCharge));
  runTest(SC_TEST_NAME_CHARGE);
  TEST_ASSERT_EQUAL_STRING("lit", textField(status(), SC_TEST_FIELD_PHASE));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kCharge));
  // The step change writes the GPIO in the tick itself, before the loop.
  advance(kStepMs);
  (void)tickClocksTests();
  TEST_ASSERT_EQUAL_STRING("off", textField(status(), SC_TEST_FIELD_PHASE));
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(kCharge));
  // The lamp logic still says lit, and loses to the test on every pass.
  updateChargeLamp();
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(kCharge));
  pass(kStepMs);
  TEST_ASSERT_EQUAL_STRING("blink", textField(status(), SC_TEST_FIELD_PHASE));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kCharge));
  pass(CHARGE_LAMP_BLINK_HALF_MS);
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(kCharge));
  pass(CHARGE_LAMP_BLINK_HALF_MS);
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kCharge));
  // The run ends after the third step; the logic (lit) is back.
  passes(kStepMs, 50u);
  TEST_ASSERT_NULL(clocksTestsActiveName());
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kCharge));
  TEST_ASSERT_FALSE(testOutputsOwned(TEST_OUTPUT_CHARGE));
}

void test_oil_lamp_steps_release_to_the_blinking_of_a_faulty_sender(void) {
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kOil)); // no reading yet
  runTest(SC_TEST_NAME_OIL);
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kOil));
  advance(kStepMs);
  (void)tickClocksTests();
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(kOil)); // off, without the timer
  // The sender fails during the test; the lamp shows the test until the end.
  clocks_local_snapshot_t local = {};
  local.oilSenderFault = true;
  clocksStatePublishLocal(&local);
  updateOilLamp();
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(kOil));
  pass(kStepMs);
  TEST_ASSERT_EQUAL_STRING("blink", textField(status(), SC_TEST_FIELD_PHASE));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kOil));
  pass(OIL_LAMP_BLINK_HALF_MS);
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(kOil));
  pass(kStepMs - OIL_LAMP_BLINK_HALF_MS);
  TEST_ASSERT_NULL(clocksTestsActiveName());
  // Released: the fault blinking of the lamp logic takes over.
  bool lit = hal_mock_gpio_get_state(kOil);
  bool toggled = false;
  for (int i = 0; i < 4 && !toggled; i++) {
    pass(OIL_LAMP_BLINK_HALF_MS);
    toggled = hal_mock_gpio_get_state(kOil) != lit;
  }
  TEST_ASSERT_TRUE(toggled);
}

// The signal of a step has ended by itself before the step ends: the buzzer
// is idle and silent (the pin is active low) at the last pass of the step.
static void assertSignalOverBeforeStepEnds(const char *phase) {
  TEST_ASSERT_EQUAL_STRING(phase, textField(status(), SC_TEST_FIELD_PHASE));
  TEST_ASSERT_FALSE(Buzzer::isActive());
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kBuzzer));
}

// Edges of the buzzer pin: every pulse ends with a rising edge; SHORT,
// MIDDLE and LONG have three pulses each.
void test_buzzer_steps_take_over_signals_and_hold_off_the_alarms(void) {
  // An alarm is sounding when the test starts: LONG waits 600 ms before its
  // first pulse, SHORT only 20 ms.
  startBuzzer(BUZZER_LONG);
  TEST_ASSERT_TRUE(Buzzer::isActive());
  const uint32_t before = hal_mock_gpio_rise_count(kBuzzer);
  runTest(SC_TEST_NAME_BUZZER);
  TEST_ASSERT_EQUAL_STRING("short", textField(status(), SC_TEST_FIELD_PHASE));
  passes(4u * kPassMs, kPassMs); // the first SHORT pulse is over at 60 ms
  TEST_ASSERT_EQUAL_UINT32(before + 1u, hal_mock_gpio_rise_count(kBuzzer));
  passes(kStepMs - 5u * kPassMs, kPassMs);
  assertSignalOverBeforeStepEnds("short");
  TEST_ASSERT_EQUAL_UINT32(before + 3u, hal_mock_gpio_rise_count(kBuzzer));

  // Step 2, MIDDLE: an overheat alarm due meanwhile neither sounds nor is
  // consumed while the test owns the buzzer.
  pass(kPassMs);
  TEST_ASSERT_EQUAL_STRING("middle", textField(status(), SC_TEST_FIELD_PHASE));
  passes(800u, kPassMs); // the signal is over, the buzzer idle
  TEST_ASSERT_FALSE(Buzzer::isActive());
  canSetValue(F_RPM, 2000.0f);
  canSetValue(F_COOLANT_TEMP, (float)(BUZZER_COOLANT_OVERHEAT_TEMP + 10));
  clocks_local_snapshot_t local = {};
  clocksStateCopyLocal(&local);
  processTemperatureBuzzerAlerts(&local);
  TEST_ASSERT_FALSE(Buzzer::isActive());
  passes(kStepMs - 800u - kPassMs, kPassMs);
  assertSignalOverBeforeStepEnds("middle");
  TEST_ASSERT_EQUAL_UINT32(before + 6u, hal_mock_gpio_rise_count(kBuzzer));

  // Step 3, LONG, the longest signal, also ends inside the shortest step.
  pass(kPassMs);
  TEST_ASSERT_EQUAL_STRING("long", textField(status(), SC_TEST_FIELD_PHASE));
  passes(kStepMs - kPassMs, kPassMs);
  assertSignalOverBeforeStepEnds("long");
  TEST_ASSERT_EQUAL_UINT32(before + 9u, hal_mock_gpio_rise_count(kBuzzer));

  // The end of the run gives the buzzer back; the alarm fires on the next
  // evaluation.
  pass(kPassMs);
  TEST_ASSERT_NULL(clocksTestsActiveName());
  TEST_ASSERT_FALSE(Buzzer::isActive());
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kBuzzer));
  TEST_ASSERT_FALSE(testOutputsOwned(TEST_OUTPUT_BUZZER));
  TEST_ASSERT_EQUAL_UINT32(before + 9u, hal_mock_gpio_rise_count(kBuzzer));
  processTemperatureBuzzerAlerts(&local);
  TEST_ASSERT_TRUE(Buzzer::isActive());
  passes(kStepMs, kPassMs);
  TEST_ASSERT_EQUAL_UINT32(before + 12u, hal_mock_gpio_rise_count(kBuzzer));
}

// Passes while the running test shows @p phase; at most two steps.
static void passWhilePhase(const char *phase) {
  for (uint32_t t = 0u; t < 2u * kStepMs; t += kPassMs) {
    const char *now = textField(status(), SC_TEST_FIELD_PHASE);
    if ((clocksTestsActiveName() == nullptr) || (now == nullptr) ||
        (strcmp(now, phase) != 0)) {
      return;
    }
    pass(kPassMs);
  }
}

void test_a_late_pass_lets_every_buzzer_signal_play_to_the_end(void) {
  const uint32_t stallMs = 2u * kStepMs; // the loop stops for two steps
  TEST_ASSERT_TRUE(stallMs + kPassMs < CLOCKS_SC_TESTS_KEEPALIVE_MS);
  const uint32_t before = hal_mock_gpio_rise_count(kBuzzer);
  runTest(SC_TEST_NAME_BUZZER);
  // SHORT has not sounded once when the loop comes back: the step waits for
  // its signal, and MIDDLE follows only after all three pulses.
  advance(stallMs);
  pass(kPassMs);
  TEST_ASSERT_EQUAL_STRING("short", textField(status(), SC_TEST_FIELD_PHASE));
  passWhilePhase("short");
  TEST_ASSERT_EQUAL_STRING("middle", textField(status(), SC_TEST_FIELD_PHASE));
  TEST_ASSERT_EQUAL_UINT32(before + 3u, hal_mock_gpio_rise_count(kBuzzer));
  passWhilePhase("middle");
  TEST_ASSERT_EQUAL_STRING("long", textField(status(), SC_TEST_FIELD_PHASE));
  TEST_ASSERT_EQUAL_UINT32(before + 6u, hal_mock_gpio_rise_count(kBuzzer));
  // The same stall in the last step: the run ends after LONG, not over it.
  advance(stallMs);
  pass(kPassMs);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_BUZZER, clocksTestsActiveName());
  passWhilePhase("long");
  TEST_ASSERT_NULL(clocksTestsActiveName());
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_DONE, status().result);
  TEST_ASSERT_EQUAL_UINT32(before + 9u, hal_mock_gpio_rise_count(kBuzzer));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kBuzzer));
}

void test_buzzer_signals_keep_their_length_across_the_millis_wrap(void) {
  // The opening delay of LONG ends after the wrap.
  hal_mock_set_millis(UINT32_MAX - 400u);
  TEST_ASSERT_EQUAL_UINT32(BUZZER_LONGEST_SIGNAL_MS,
                           signalLengthMs(BUZZER_LONG));
  // On 20 ms passes a phase deadline just before the wrap is passed over;
  // the step still waits for a signal that plays to the end.
  hal_mock_set_millis(UINT32_MAX - 15u);
  const uint32_t before = hal_mock_gpio_rise_count(kBuzzer);
  runTest(SC_TEST_NAME_BUZZER);
  passWhilePhase("short");
  TEST_ASSERT_EQUAL_STRING("middle", textField(status(), SC_TEST_FIELD_PHASE));
  TEST_ASSERT_EQUAL_UINT32(before + 3u, hal_mock_gpio_rise_count(kBuzzer));
}

void test_start_beep_waits_for_the_buzzer_test(void) {
  // setUp re-arms the start beep through resetBuzzerAlerts().
  processStartBeep(false);
  TEST_ASSERT_FALSE(Buzzer::isActive()); // the environment is not up yet
  runTest(SC_TEST_NAME_BUZZER);
  passes(kStepMs - kPassMs, kPassMs); // SHORT is over, the test owns the buzzer
  TEST_ASSERT_FALSE(Buzzer::isActive());
  processStartBeep(true);
  TEST_ASSERT_FALSE(Buzzer::isActive());
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops()->stop(nullptr));
  (void)tickClocksTests();
  TEST_ASSERT_FALSE(testOutputsOwned(TEST_OUTPUT_BUZZER));
  const uint32_t before = hal_mock_gpio_rise_count(kBuzzer);
  processStartBeep(true); // released: the beep sounds now
  TEST_ASSERT_TRUE(Buzzer::isActive());
  passes(kStepMs, kPassMs);
  TEST_ASSERT_FALSE(Buzzer::isActive());
  TEST_ASSERT_EQUAL_UINT32(before + 3u, hal_mock_gpio_rise_count(kBuzzer));
  processStartBeep(true); // once only
  TEST_ASSERT_FALSE(Buzzer::isActive());
}

void test_steps_are_printed_only_while_the_debug_log_is_audible(void) {
  runTest(SC_TEST_NAME_CHARGE);
  clocksTestsEvents();
  TEST_ASSERT_TRUE(lastDebugLineHas("TEST: charge step 1/3 lit"));
  // A configurator session mutes the log: the step is dropped, not printed
  // after the session.
  hal_debug_set_muted(true);
  passUntilStep(2);
  clocksTestsEvents();
  hal_debug_set_muted(false);
  clocksTestsEvents();
  TEST_ASSERT_FALSE(lastDebugLineHas("step 2/3"));
  passUntilStep(3);
  clocksTestsEvents();
  TEST_ASSERT_TRUE(lastDebugLineHas("TEST: charge step 3/3 blink"));
}

void test_stop_skip_replace_and_the_session_release_the_outputs(void) {
  // stop
  runTest(SC_TEST_NAME_RPM);
  TEST_ASSERT_TRUE(testOutputsOwned(TEST_OUTPUT_RPM));
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops()->stop(nullptr));
  (void)tickClocksTests();
  TEST_ASSERT_NULL(clocksTestsActiveName());
  TEST_ASSERT_FALSE(testOutputsOwned(TEST_OUTPUT_RPM));
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_STOPPED, status().result);
  TEST_ASSERT_EQUAL_UINT32(0u, edgesPerSecond(kTacho));

  // stop in the middle of a pulse silences the buzzer at once
  runTest(SC_TEST_NAME_BUZZER);
  passes(2u * kPassMs, kPassMs); // the first SHORT pulse is on (pin low)
  TEST_ASSERT_FALSE(hal_mock_gpio_get_state(kBuzzer));
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops()->stop(nullptr));
  (void)tickClocksTests();
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(kBuzzer));
  TEST_ASSERT_FALSE(Buzzer::isActive());

  // replace: a new run releases the previous test's output
  runTest(SC_TEST_NAME_SPEED);
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops()->run(nullptr, SC_TEST_NAME_CHARGE));
  (void)tickClocksTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CHARGE, clocksTestsActiveName());
  TEST_ASSERT_FALSE(testOutputsOwned(TEST_OUTPUT_SPEED));
  TEST_ASSERT_TRUE(testOutputsOwned(TEST_OUTPUT_CHARGE));

  // skip inside the sequence: charge released, oil owned
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops()->run(nullptr, SC_TEST_SEQUENCE));
  (void)tickClocksTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CHARGE, clocksTestsActiveName());
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops()->skip(nullptr));
  (void)tickClocksTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_OIL, clocksTestsActiveName());
  TEST_ASSERT_FALSE(testOutputsOwned(TEST_OUTPUT_CHARGE));
  TEST_ASSERT_TRUE(testOutputsOwned(TEST_OUTPUT_OIL));

  // the session ends: the sequence stops and the lamp is back to its logic
  clocksTestsScSessionEnded();
  (void)tickClocksTests();
  TEST_ASSERT_NULL(clocksTestsActiveName());
  TEST_ASSERT_FALSE(testOutputsOwned(TEST_OUTPUT_OIL));
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_SESSION_END, status().result);

  // a quiet host: no status poll for the keepalive time
  runTest(SC_TEST_NAME_BUZZER);
  TEST_ASSERT_TRUE(testOutputsOwned(TEST_OUTPUT_BUZZER));
  advance(CLOCKS_SC_TESTS_KEEPALIVE_MS - 1u);
  (void)tickClocksTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_BUZZER, clocksTestsActiveName());
  advance(1u);
  (void)tickClocksTests();
  TEST_ASSERT_NULL(clocksTestsActiveName());
  TEST_ASSERT_FALSE(testOutputsOwned(TEST_OUTPUT_BUZZER));
  TEST_ASSERT_FALSE(Buzzer::isActive());
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_HOST_LOST, status().result);
}

void test_run_all_walks_the_five_tests_in_table_order(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops()->run(nullptr, SC_TEST_SEQUENCE));
  (void)tickClocksTests();
  static const char *const order[] = {SC_TEST_NAME_CHARGE, SC_TEST_NAME_OIL,
                                      SC_TEST_NAME_RPM, SC_TEST_NAME_SPEED,
                                      SC_TEST_NAME_BUZZER};
  static const uint32_t steps[] = {3u, 3u, 5u, 7u, 3u};
  for (size_t i = 0u; i < 5u; i++) {
    TEST_ASSERT_EQUAL_STRING(order[i], clocksTestsActiveName());
    const sc_command_test_status_t s = status();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(i + 1u), s.seq_index);
    TEST_ASSERT_EQUAL_UINT8(5u, s.seq_count);
    TEST_ASSERT_EQUAL_INT32((int32_t)steps[i], field(s, SC_TEST_FIELD_STEPS));
    passes(steps[i] * kStepMs, 100u);
  }
  TEST_ASSERT_NULL(clocksTestsActiveName());
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_DONE, status().result);
  for (unsigned int o = 0u; o < (unsigned int)TEST_OUTPUT_COUNT; o++) {
    TEST_ASSERT_FALSE(testOutputsOwned((clocks_test_output_t)o));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_registry_lists_the_five_cluster_tests_with_the_shared_step);
  RUN_TEST(test_rpm_steps_drive_the_tachometer_and_release_to_the_measurement);
  RUN_TEST(
      test_speed_steps_drive_the_speedometer_while_can_carries_the_measurement);
  RUN_TEST(test_a_late_pass_shows_one_step_and_the_steps_cross_the_millis_wrap);
  RUN_TEST(test_charge_lamp_steps_reach_the_output_at_once_and_release);
  RUN_TEST(test_oil_lamp_steps_release_to_the_blinking_of_a_faulty_sender);
  RUN_TEST(test_buzzer_steps_take_over_signals_and_hold_off_the_alarms);
  RUN_TEST(test_a_late_pass_lets_every_buzzer_signal_play_to_the_end);
  RUN_TEST(test_buzzer_signals_keep_their_length_across_the_millis_wrap);
  RUN_TEST(test_start_beep_waits_for_the_buzzer_test);
  RUN_TEST(test_steps_are_printed_only_while_the_debug_log_is_audible);
  RUN_TEST(test_stop_skip_replace_and_the_session_release_the_outputs);
  RUN_TEST(test_run_all_walks_the_five_tests_in_table_order);
  return UNITY_END();
}

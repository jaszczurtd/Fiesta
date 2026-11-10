#include "clocksTests.h"
#include "../common/scDefinitions/sc_protocol.h"
#include "../common/seqlock.h"
#include "../common/test_runner/test_runner.h"
#include "buzzer.h"
#include "chargeLamp.h"
#include "clusterOutput.h"
#include "config.h"
#include "oilLamp.h"
#include "testOutputs.h"

#include <hal/core/hal_compiler.h>

namespace {
// Runs required by the user; the lamps and the buzzer are the plan's
// proposals (functional-tests K1).
const unsigned int kRpmSteps[] = {1000u, 3000u, 5000u, 7000u, 0u};
const unsigned int kSpeedSteps[] = {20u, 60u, 100u, 140u, 180u, 220u, 0u};
const clocks_test_lamp_t kLampSteps[] = {TEST_LAMP_LIT, TEST_LAMP_OFF,
                                         TEST_LAMP_BLINK};
const int kBuzzerSteps[] = {BUZZER_SHORT, BUZZER_MIDDLE, BUZZER_LONG};
const char *const kLampNames[] = {"off", "lit", "blink"}; // clocks_test_lamp_t
JB_STATIC_ASSERT(COUNTOF(kLampNames) == (size_t)TEST_LAMP_BLINK + 1u,
                 "one name per clocks_test_lamp_t state");
const char *const kBuzzerNames[] = {"short", "middle", "long"}; // BUZZER_*
JB_STATIC_ASSERT(COUNTOF(kBuzzerNames) == (size_t)BUZZER_COUNT,
                 "one name per buzzer signal");

enum { PARAM_STEP_MS = 0, PARAM_COUNT };
const fiesta_test_param_t kParams[PARAM_COUNT] = {
    {SC_TEST_PARAM_STEP_MS, FIESTA_TEST_ALL, SC_TEST_UNIT_MS,
     (int32_t)CLOCKS_TEST_STEP_MS_MIN, (int32_t)CLOCKS_TEST_STEP_MS_MAX,
     (int32_t)CLOCKS_TEST_STEP_MS_DEFAULT}};
int32_t s_values[PARAM_COUNT];

// Progress of the running test; controller role.
struct {
  uint32_t stepStartedMs;
  size_t step;
} s_run;

// Step announcement for the session role: one slot, a newer step replaces an
// unprinted one. The sequence tells the printer that a new step arrived.
struct Announcement {
  uint32_t id;
  uint32_t step;
  uint32_t steps;
  int32_t value;
  const char *text;
  uint32_t sequence;
};
fiesta_seqlock_t s_announceLock;
Announcement s_announce;
uint32_t s_announced; // controller: announcements written
uint32_t s_printed;   // session: last sequence printed

uint32_t stepMs() { return (uint32_t)fiesta_test_param(PARAM_STEP_MS); }

size_t stepsOf(uint32_t id) {
  switch (id) {
  case CLOCKS_TEST_RPM:
    return COUNTOF(kRpmSteps);
  case CLOCKS_TEST_SPEED:
    return COUNTOF(kSpeedSteps);
  case CLOCKS_TEST_CHARGE:
  case CLOCKS_TEST_OIL:
    return COUNTOF(kLampSteps);
  case CLOCKS_TEST_BUZZER:
    return COUNTOF(kBuzzerSteps);
  default:
    return 0u;
  }
}

int32_t valueOf(uint32_t id, size_t step) {
  switch (id) {
  case CLOCKS_TEST_RPM:
    return (int32_t)kRpmSteps[step];
  case CLOCKS_TEST_SPEED:
    return (int32_t)kSpeedSteps[step];
  case CLOCKS_TEST_CHARGE:
  case CLOCKS_TEST_OIL:
    return (int32_t)kLampSteps[step];
  case CLOCKS_TEST_BUZZER:
    return (int32_t)kBuzzerSteps[step];
  default:
    return 0;
  }
}

// Step name of a lamp or buzzer step; NULL for the numeric gauges.
const char *textOf(uint32_t id, size_t step) {
  switch (id) {
  case CLOCKS_TEST_CHARGE:
  case CLOCKS_TEST_OIL:
    return kLampNames[kLampSteps[step]];
  case CLOCKS_TEST_BUZZER:
    return kBuzzerNames[kBuzzerSteps[step]];
  default:
    return nullptr;
  }
}

const char *nameOf(uint32_t id);

void announce(uint32_t id, size_t step) {
  const uint32_t begun = fiestaSeqlockWriteBegin(&s_announceLock);
  s_announced++;
  s_announce = Announcement{id,
                            (uint32_t)step + 1u,
                            (uint32_t)stepsOf(id),
                            valueOf(id, step),
                            textOf(id, step),
                            s_announced};
  fiestaSeqlockWriteEnd(&s_announceLock, begun);
}

// Show one step on its output in this very pass.
void applyStep(uint32_t id, size_t step) {
  const uint32_t nowMs = hal_millis();
  switch (id) {
  case CLOCKS_TEST_RPM:
    testOutputsSetGauge(TEST_OUTPUT_RPM, kRpmSteps[step]);
    updateCluster();
    break;
  case CLOCKS_TEST_SPEED:
    testOutputsSetGauge(TEST_OUTPUT_SPEED, kSpeedSteps[step]);
    updateCluster();
    break;
  case CLOCKS_TEST_CHARGE:
    testOutputsSetLamp(TEST_OUTPUT_CHARGE, kLampSteps[step], nowMs);
    updateChargeLamp();
    break;
  case CLOCKS_TEST_OIL:
    testOutputsSetLamp(TEST_OUTPUT_OIL, kLampSteps[step], nowMs);
    updateOilLamp();
    break;
  case CLOCKS_TEST_BUZZER:
    // Takes the buzzer over from the start beep or an alarm in progress.
    stopAllBuzzers();
    testOutputsSetBuzzer(true);
    startBuzzer(kBuzzerSteps[step]);
    break;
  default:
    break;
  }
  announce(id, step);
}

// Give the output back to the measurements in this very pass.
void releaseOutputs(uint32_t id) {
  switch (id) {
  case CLOCKS_TEST_RPM:
    testOutputsRelease(TEST_OUTPUT_RPM);
    updateCluster();
    break;
  case CLOCKS_TEST_SPEED:
    testOutputsRelease(TEST_OUTPUT_SPEED);
    updateCluster();
    break;
  case CLOCKS_TEST_CHARGE:
    testOutputsRelease(TEST_OUTPUT_CHARGE);
    updateChargeLamp();
    break;
  case CLOCKS_TEST_OIL:
    testOutputsRelease(TEST_OUTPUT_OIL);
    updateOilLamp();
    break;
  case CLOCKS_TEST_BUZZER:
    stopAllBuzzers();
    testOutputsSetBuzzer(false);
    break;
  default:
    break;
  }
}

template <uint32_t Id> void startTest() {
  s_run.step = 0u;
  s_run.stepStartedMs = hal_millis();
  applyStep(Id, 0u);
}

// A step is over once its time has passed and, for the buzzer, its signal
// has played to the end: the signal runs on the loop, so a pass that comes
// late must not cut it short, nor replace it before its first pulse.
bool stepOver(uint32_t id, uint32_t nowMs) {
  if (!hal_elapsed_u32(nowMs, s_run.stepStartedMs, stepMs())) {
    return false;
  }
  return (id != CLOCKS_TEST_BUZZER) || !Buzzer::isActive();
}

// One step per tick: a late pass shows the next state, never several at
// once, and every state lasts at least the step time.
template <uint32_t Id> float stepTest(bool *outFinished) {
  *outFinished = false;
  const uint32_t nowMs = hal_millis();
  if (!stepOver(Id, nowMs)) {
    return (float)valueOf(Id, s_run.step);
  }
  s_run.step++;
  s_run.stepStartedMs = nowMs;
  if (s_run.step >= stepsOf(Id)) {
    *outFinished = true;
    return 0.0f;
  }
  applyStep(Id, s_run.step);
  return (float)valueOf(Id, s_run.step);
}

template <uint32_t Id>
size_t progressTest(sc_command_test_field_t *fields, size_t capacity) {
  size_t count = 0u;
  const auto add = [&](const char *key, const char *text, int32_t value) {
    if (count < capacity) {
      fields[count] = sc_command_test_field_t{key, text, value};
      count++;
    }
  };
  add(SC_TEST_FIELD_STEP, nullptr, (int32_t)s_run.step + 1);
  add(SC_TEST_FIELD_STEPS, nullptr, (int32_t)stepsOf(Id));
  const char *text = textOf(Id, s_run.step);
  if (text != nullptr) {
    add(SC_TEST_FIELD_PHASE, text, 0);
  } else {
    add(SC_TEST_FIELD_VALUE, nullptr, valueOf(Id, s_run.step));
  }
  return count;
}

// id, name, summary, drivesDemand, sequenced, scSupported, start, step,
// result, progress. run all follows this order.
const fiesta_test_t kSuite[] = {
    {CLOCKS_TEST_CHARGE, SC_TEST_NAME_CHARGE, "charge lamp: lit, off, blinking",
     false, true, true, startTest<CLOCKS_TEST_CHARGE>,
     stepTest<CLOCKS_TEST_CHARGE>, nullptr, progressTest<CLOCKS_TEST_CHARGE>},
    {CLOCKS_TEST_OIL, SC_TEST_NAME_OIL,
     "oil lamp: lit, off, blinking as with a faulty sender", false, true, true,
     startTest<CLOCKS_TEST_OIL>, stepTest<CLOCKS_TEST_OIL>, nullptr,
     progressTest<CLOCKS_TEST_OIL>},
    {CLOCKS_TEST_RPM, SC_TEST_NAME_RPM,
     "tachometer 1000-3000-5000-7000, then 0", false, true, true,
     startTest<CLOCKS_TEST_RPM>, stepTest<CLOCKS_TEST_RPM>, nullptr,
     progressTest<CLOCKS_TEST_RPM>},
    {CLOCKS_TEST_SPEED, SC_TEST_NAME_SPEED,
     "speedometer 20-60-100-140-180-220 km/h, then 0", false, true, true,
     startTest<CLOCKS_TEST_SPEED>, stepTest<CLOCKS_TEST_SPEED>, nullptr,
     progressTest<CLOCKS_TEST_SPEED>},
    {CLOCKS_TEST_BUZZER, SC_TEST_NAME_BUZZER, "buzzer: short, middle, long",
     false, true, true, startTest<CLOCKS_TEST_BUZZER>,
     stepTest<CLOCKS_TEST_BUZZER>, nullptr, progressTest<CLOCKS_TEST_BUZZER>},
};

const char *nameOf(uint32_t id) {
  for (size_t i = 0u; i < COUNTOF(kSuite); i++) {
    if (kSuite[i].id == id) {
      return kSuite[i].name;
    }
  }
  return "?";
}

const fiesta_test_t *suite(size_t *count) {
  *count = COUNTOF(kSuite);
  return kSuite;
}

void moduleReset() {
  testOutputsReset();
  s_run = {};
}

void moduleCleanup(const fiesta_test_t *entry, const char *result,
                   bool restart) {
  (void)result;
  (void)restart;
  releaseOutputs(entry->id);
}

// Session role: print the latest step once the debug log is audible.
void moduleEvents() {
  Announcement copy{};
  bool consistent = false;
  for (unsigned int attempt = 0u;
       !consistent && (attempt < FIESTA_SEQLOCK_READ_ATTEMPTS); attempt++) {
    const uint32_t begun = fiestaSeqlockReadBegin(&s_announceLock);
    copy = s_announce;
    consistent = fiestaSeqlockReadConsistent(&s_announceLock, begun);
  }
  if (!consistent || (copy.sequence == s_printed)) {
    return;
  }
  s_printed = copy.sequence;
  // A configurator session mutes the log: the step is dropped like the
  // runner's lifecycle lines, never printed late. The SC status carries it.
  if (hal_debug_is_muted()) {
    return;
  }
  if (copy.text != nullptr) {
    deb("TEST: %s step %lu/%lu %s", nameOf(copy.id), (unsigned long)copy.step,
        (unsigned long)copy.steps, copy.text);
  } else {
    deb("TEST: %s step %lu/%lu %ld", nameOf(copy.id), (unsigned long)copy.step,
        (unsigned long)copy.steps, (long)copy.value);
  }
}

// suite, params, values, param_count, keepalive_ms, reset, prepare, cleanup,
// apply_output, read_drive, sc_blocked, command, help, events.
const fiesta_test_config_t kConfig = {
    suite,
    kParams,
    s_values,
    PARAM_COUNT,
    CLOCKS_SC_TESTS_KEEPALIVE_MS,
    moduleReset,
    nullptr,
    moduleCleanup,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    moduleEvents,
};
} // namespace

bool initClocksTests(void) { return fiesta_test_init(&kConfig) == HAL_OK; }

bool tickClocksTests(void) { return fiesta_test_tick(); }

void clocksTestsEvents(void) { fiesta_test_events(); }

const sc_command_test_ops_t *clocksTestsScOps(void) {
  return fiesta_test_sc_ops();
}

void clocksTestsScSessionEnded(void) { fiesta_test_session_ended(); }

const char *clocksTestsActiveName(void) { return fiesta_test_active_name(); }

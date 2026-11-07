#include "../../scDefinitions/sc_protocol.h"
#include "../../seqlock.h"
#include "../test_runner.h"
#include <hal/impl/.mock/hal_mock.h>
#include <string.h>
#include <unity.h>

static unsigned s_controller, s_session, s_caller;
static unsigned s_steps, s_starts, s_fixtures, s_cleanups, s_restarts;
static uint32_t s_cleanedId;
static const char *s_reason;
static float s_output;
static bool s_finish, s_blocked;

static void start() {
  TEST_ASSERT_EQUAL_UINT(s_controller, s_caller);
  ++s_starts;
}
static float step(bool *finished) {
  TEST_ASSERT_EQUAL_UINT(s_controller, s_caller);
  ++s_steps;
  *finished = s_finish;
  return 25.0f;
}
static void fixture() {
  TEST_ASSERT_EQUAL_UINT(s_session, s_caller);
  ++s_fixtures;
}
static void cleanup(const fiesta_test_t *entry, const char *reason,
                    bool restart) {
  TEST_ASSERT_EQUAL_UINT(s_controller, s_caller);
  ++s_cleanups;
  s_cleanedId = entry->id;
  s_reason = reason;
  if (restart) {
    ++s_restarts;
  } else {
    s_output = 0.0f;
  }
}
static void apply(float value) {
  TEST_ASSERT_EQUAL_UINT(s_controller, s_caller);
  s_output = value;
}
static const char *blocked() { return s_blocked ? "module_blocked" : nullptr; }

static const fiesta_test_t s_suite[] = {
    {1U, "fixture", "Session fixture", false, true, false, fixture, nullptr,
     nullptr, nullptr},
    {2U, "steps", "Stepped output", true, true, true, start, step, nullptr,
     nullptr},
    {3U, "manual", "Manual output", true, false, true, start, step, nullptr,
     nullptr},
};
static const fiesta_test_param_t s_params[] = {
    {"steps_n", 2U, "count", 1, 10, 3},
};
static int32_t s_values[COUNTOF(s_params)];
static const fiesta_test_t *suite(size_t *count) {
  *count = COUNTOF(s_suite);
  return s_suite;
}
static const fiesta_test_config_t s_config = {
    suite,   s_params, s_values, COUNTOF(s_params), 1000U,   nullptr, nullptr,
    cleanup, apply,    nullptr,  blocked,           nullptr, nullptr, nullptr,
};

static bool tick() {
  s_caller = s_controller;
  return fiesta_test_tick();
}
static void events() {
  s_caller = s_session;
  fiesta_test_events();
}
static void initialize(unsigned controller) {
  s_controller = controller;
  s_session = 1U - controller;
  s_caller = s_controller;
  s_steps = s_starts = s_fixtures = s_cleanups = s_restarts = 0U;
  s_cleanedId = 0U;
  s_reason = nullptr;
  s_output = 0.0f;
  s_finish = s_blocked = false;
  hal_mock_set_millis(0U);
  hal_debug_set_muted(false);
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_init(&s_config));
}
void setUp() { initialize(1U); }
void tearDown() {
  s_caller = s_controller;
  (void)fiesta_test_stop();
  events();
}

static void sequence_roles() {
  for (unsigned controller = 0U; controller < 2U; ++controller) {
    initialize(controller);
    TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_start(FIESTA_TEST_ALL));
    TEST_ASSERT_EQUAL_UINT32(1U, fiesta_test_active_id());
    TEST_ASSERT_FALSE(tick());
    TEST_ASSERT_EQUAL_UINT(0U, s_fixtures);
    events();
    TEST_ASSERT_EQUAL_UINT(1U, s_fixtures);
    events();
    TEST_ASSERT_EQUAL_UINT(1U, s_fixtures);
    TEST_ASSERT_FALSE(tick());
    TEST_ASSERT_EQUAL_UINT32(2U, fiesta_test_active_id());
    TEST_ASSERT_EQUAL_UINT(1U, s_cleanups);
    TEST_ASSERT_EQUAL_UINT(1U, s_starts);
    TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_DONE, s_reason);
    TEST_ASSERT_TRUE(tick());
    TEST_ASSERT_EQUAL_UINT(1U, s_steps);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, s_output);
    s_finish = true;
    TEST_ASSERT_TRUE(tick());
    TEST_ASSERT_EQUAL_UINT(2U, s_steps);
    TEST_ASSERT_EQUAL_UINT(2U, s_cleanups);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s_output);
    TEST_ASSERT_NULL(fiesta_test_active_name());
    TEST_ASSERT_FALSE(tick());
    events();
    TEST_ASSERT_NOT_NULL(
        strstr(hal_mock_deb_last_line(), "TEST: sequence finished"));
  }
}

static void stop_skip_replace_restart() {
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_start(FIESTA_TEST_ALL));
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_skip());
  TEST_ASSERT_EQUAL_UINT32(1U, s_cleanedId);
  events(); // The skipped one-shot must never run.
  TEST_ASSERT_EQUAL_UINT(0U, s_fixtures);
  TEST_ASSERT_TRUE(tick());
  TEST_ASSERT_EQUAL_UINT32(2U, fiesta_test_active_id());
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_start(2U));
  TEST_ASSERT_EQUAL_UINT(1U, s_restarts);
  TEST_ASSERT_EQUAL_FLOAT(25.0f, s_output);
  TEST_ASSERT_EQUAL_UINT(2U, s_starts);
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_start(3U));
  TEST_ASSERT_EQUAL_UINT(3U, s_cleanups);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s_output);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_STOPPED, s_reason);
  TEST_ASSERT_TRUE(tick());
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_stop());
  TEST_ASSERT_EQUAL_UINT(4U, s_cleanups);
  TEST_ASSERT_EQUAL_UINT32(3U, s_cleanedId);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s_output);
  TEST_ASSERT_FALSE(tick());
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_stop());
  TEST_ASSERT_EQUAL_UINT(4U, s_cleanups);
}

static void sc_supervision() {
  const auto *ops = fiesta_test_sc_ops();
  for (unsigned controller = 0U; controller < 2U; ++controller) {
    initialize(controller);
    s_caller = s_session;
    hal_mock_set_millis(UINT32_MAX - 10U);
    TEST_ASSERT_EQUAL(HAL_OK, ops->run(nullptr, "steps"));
    TEST_ASSERT_TRUE(tick());
    hal_mock_advance_millis(999U);
    TEST_ASSERT_TRUE(tick());
    s_caller = s_session;
    sc_command_test_status_t status = {};
    ops->status(nullptr, &status);
    TEST_ASSERT_EQUAL_STRING("steps", status.active);
    hal_mock_advance_millis(999U);
    TEST_ASSERT_TRUE(tick());
    hal_mock_advance_millis(1U);
    TEST_ASSERT_FALSE(tick());
    TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_HOST_LOST, s_reason);
    TEST_ASSERT_EQUAL_UINT(1U, s_cleanups);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s_output);
    s_caller = s_session;
    TEST_ASSERT_EQUAL(HAL_OK, ops->run(nullptr, "all"));
    TEST_ASSERT_TRUE(tick());
    TEST_ASSERT_EQUAL_UINT(0U, s_fixtures); // SC filters unsupported fixtures.
    s_caller = s_session;
    fiesta_test_session_ended();
    TEST_ASSERT_FALSE(tick());
    TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_SESSION_END, s_reason);
    TEST_ASSERT_EQUAL_UINT(2U, s_cleanups);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, s_output);
    // A session end never terminates an operator-owned console run.
    TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_start(3U));
    s_caller = s_session;
    fiesta_test_session_ended();
    hal_mock_advance_millis(2000U);
    TEST_ASSERT_TRUE(tick());
    TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_stop());
    events();
  }
}

static void sc_queue_and_interlock() {
  const auto *ops = fiesta_test_sc_ops();
  s_blocked = true;
  TEST_ASSERT_EQUAL(HAL_EPERM, ops->run(nullptr, "steps"));
  TEST_ASSERT_EQUAL(HAL_ENOENT, ops->run(nullptr, "fixture"));
  s_blocked = false;
  TEST_ASSERT_EQUAL(HAL_OK, ops->run(nullptr, "all"));
  TEST_ASSERT_EQUAL(HAL_EBUSY, ops->stop(nullptr));
  TEST_ASSERT_TRUE(tick());
  TEST_ASSERT_EQUAL(HAL_OK, ops->skip(nullptr));
  TEST_ASSERT_FALSE(tick());
  TEST_ASSERT_EQUAL_UINT(1U, s_cleanups);
  TEST_ASSERT_EQUAL(HAL_OK, ops->run(nullptr, "steps"));
  TEST_ASSERT_TRUE(tick());
  s_blocked = true;
  TEST_ASSERT_FALSE(tick());
  TEST_ASSERT_EQUAL_STRING("module_blocked", s_reason);
  TEST_ASSERT_EQUAL_UINT(2U, s_cleanups);
}

static void parameters_and_console() {
  const auto *ops = fiesta_test_sc_ops();
  TEST_ASSERT_EQUAL_UINT(2U, ops->count(nullptr));
  sc_command_test_param_t param = {};
  TEST_ASSERT_TRUE(ops->param_at(nullptr, 0U, 0U, &param));
  TEST_ASSERT_EQUAL_STRING("steps_n", param.id);
  TEST_ASSERT_EQUAL_INT32(3, param.value);
  TEST_ASSERT_EQUAL(HAL_ENOENT, ops->set_param(nullptr, "absent", 1));
  TEST_ASSERT_EQUAL(HAL_EINVAL, ops->set_param(nullptr, "steps_n", 11));
  TEST_ASSERT_EQUAL_INT32(3, fiesta_test_param(0U));
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_param_set(0U, 1));
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_param_set(0U, 10));
  TEST_ASSERT_EQUAL(HAL_ENOENT, fiesta_test_param_set(SIZE_MAX, 1));
  TEST_ASSERT_NULL(fiesta_test_param_desc(SIZE_MAX));
  fiesta_test_params_reset();
  TEST_ASSERT_EQUAL_INT32(3, fiesta_test_param(0U));
  s_caller = s_session;
  fiesta_test_console("set steps_n 7");
  fiesta_test_console("run steps"); // Busy: set is retained.
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_serial_last_line(), "pending; retry"));
  TEST_ASSERT_FALSE(tick());
  TEST_ASSERT_EQUAL_INT32(7, fiesta_test_param(0U));
  s_caller = s_session;
  fiesta_test_console("  RuN steps");
  TEST_ASSERT_TRUE(tick());
  events();
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_deb_last_line(), "TEST: steps started"));
  s_caller = s_session;
  fiesta_test_console("stop");
  TEST_ASSERT_FALSE(tick());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s_output);
  events();
  TEST_ASSERT_NOT_NULL(
      strstr(hal_mock_deb_last_line(), "TEST: steps result: stopped"));
}

static void seqlock_wrap_and_busy_writer() {
  fiesta_seqlock_t lock = {UINT32_MAX - 1U};
  const uint32_t before = fiestaSeqlockReadBegin(&lock);
  TEST_ASSERT_TRUE(fiestaSeqlockReadConsistent(&lock, before));
  const uint32_t begun = fiestaSeqlockWriteBegin(&lock);
  TEST_ASSERT_FALSE(fiestaSeqlockReadConsistent(&lock, before));
  TEST_ASSERT_FALSE(
      fiestaSeqlockReadConsistent(&lock, fiestaSeqlockReadBegin(&lock)));
  fiestaSeqlockWriteEnd(&lock, begun);
  TEST_ASSERT_EQUAL_UINT32(0U, lock.sequence);
  TEST_ASSERT_FALSE(fiestaSeqlockReadConsistent(&lock, before));
  TEST_ASSERT_TRUE(
      fiestaSeqlockReadConsistent(&lock, fiestaSeqlockReadBegin(&lock)));
}

static fiesta_test_t s_limitSuite[UINT8_MAX];
static size_t s_limitCount;
static const fiesta_test_t *limitSuite(size_t *count) {
  *count = s_limitCount;
  return s_limitSuite;
}

static void sequence_at_registry_limit() {
  static const fiesta_test_config_t config = {
      limitSuite, nullptr, nullptr, 0U,      1000U,   nullptr, nullptr,
      cleanup,    apply,   nullptr, nullptr, nullptr, nullptr, nullptr,
  };
  for (size_t i = 0U; i < COUNTOF(s_limitSuite); ++i) {
    s_limitSuite[i] = s_suite[1];
    s_limitSuite[i].id = (uint32_t)i + 1U;
  }
  s_limitCount = COUNTOF(s_limitSuite) + 1U;
  TEST_ASSERT_EQUAL(HAL_EINVAL, fiesta_test_init(&config));
  s_limitCount = COUNTOF(s_limitSuite);
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_init(&config));
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_start(FIESTA_TEST_ALL));
  s_finish = true;
  for (size_t i = 0U; i < COUNTOF(s_limitSuite); ++i) {
    TEST_ASSERT_EQUAL_UINT32(i + 1U, fiesta_test_active_id());
    sc_command_test_status_t status = {};
    s_caller = s_session;
    fiesta_test_sc_ops()->status(nullptr, &status);
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, status.seq_count);
    TEST_ASSERT_EQUAL_UINT8(i + 1U, status.seq_index);
    events();
    TEST_ASSERT_TRUE(tick());
  }
  TEST_ASSERT_EQUAL_UINT32(FIESTA_TEST_NONE, fiesta_test_active_id());
  TEST_ASSERT_EQUAL_UINT(UINT8_MAX, s_steps);
  TEST_ASSERT_EQUAL_UINT(UINT8_MAX, s_cleanups);
  TEST_ASSERT_FALSE(tick());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(sequence_roles);
  RUN_TEST(stop_skip_replace_restart);
  RUN_TEST(sc_supervision);
  RUN_TEST(sc_queue_and_interlock);
  RUN_TEST(parameters_and_console);
  RUN_TEST(seqlock_wrap_and_busy_writer);
  RUN_TEST(sequence_at_registry_limit);
  return UNITY_END();
}

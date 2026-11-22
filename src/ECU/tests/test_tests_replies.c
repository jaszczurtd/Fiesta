/* Replies must leave the control core before formatting or USB writes. */
#include "../../common/test_runner/test_runner.h"
#include "../../common/tests/hal_mock_c_bridge.h"
#include "ecuContext.h"
#include "tests.h"
#include "tests_helpers.h"
#include "tests_workers.h"
#include "unity.h"
#include <string.h>

static uint32_t s_values[4];
static uint32_t s_details[4];
static unsigned s_replies;
static bool s_draining;

static const fiesta_test_t s_suite[] = {
    {.id = 1U, .name = "idle", .summary = "No output"}};

static const fiesta_test_t *suite(size_t *count) {
  *count = COUNTOF(s_suite);
  return s_suite;
}

static void reply(uint8_t code, uint32_t value, uint32_t detail) {
  TEST_ASSERT_EQUAL_UINT8(42U, code);
  TEST_ASSERT_TRUE(s_draining);
  TEST_ASSERT_LESS_THAN_UINT(COUNTOF(s_values), s_replies);
  s_values[s_replies] = value;
  s_details[s_replies] = detail;
  ++s_replies;
}

static const fiesta_test_config_t s_config = {
    .suite = suite, .keepalive_ms = 1000U, .reply = reply};

void setUp(void) {
  hal_debug_set_muted(false);
  fiesta_test_set_millis(0U);
  s_replies = 0U;
  s_draining = false;
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_init(&s_config));
}

void tearDown(void) {
  hal_debug_set_muted(false);
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_init(&s_config));
}

static void drain(void) {
  s_draining = true;
  fiesta_test_events();
  s_draining = false;
}

static void
test_replies_capture_each_payload_and_only_run_on_session_role(void) {
  fiesta_test_reply(42U, 0x12345678U, 60000U);
  fiesta_test_reply(42U, 0x87654321U, 0U);
  TEST_ASSERT_EQUAL_UINT(0U, s_replies);
  drain();
  TEST_ASSERT_EQUAL_UINT(2U, s_replies);
  TEST_ASSERT_EQUAL_HEX32(0x12345678U, s_values[0]);
  TEST_ASSERT_EQUAL_HEX32(0x87654321U, s_values[1]);
  TEST_ASSERT_EQUAL_UINT32(60000U, s_details[0]);
  TEST_ASSERT_EQUAL_UINT32(0U, s_details[1]);
  drain();
  TEST_ASSERT_EQUAL_UINT(2U, s_replies);
}

static void test_reply_overflow_is_bounded_and_reset_discards_pending(void) {
  for (uint32_t i = 0U; i < 6U; ++i) {
    fiesta_test_reply(42U, i, i + 10U);
  }
  drain();
  TEST_ASSERT_EQUAL_UINT(4U, s_replies);
  for (uint32_t i = 0U; i < 4U; ++i) {
    TEST_ASSERT_EQUAL_UINT32(i, s_values[i]);
    TEST_ASSERT_EQUAL_UINT32(i + 10U, s_details[i]);
  }
  TEST_ASSERT_NOT_NULL(strstr(fiesta_test_serial_last_line(), "overflow: 2"));
  fiesta_test_reply(42U, 9U, 99U);
  TEST_ASSERT_EQUAL(HAL_OK, fiesta_test_init(&s_config));
  s_replies = 0U;
  drain();
  TEST_ASSERT_EQUAL_UINT(0U, s_replies);
  hal_debug_set_muted(true);
  fiesta_test_reply(42U, 8U, 88U);
  hal_debug_set_muted(false);
  drain();
  TEST_ASSERT_EQUAL_UINT(0U, s_replies);
}

static void test_watch_reply_waits_for_session_and_captures_the_limit(void) {
  deb("reply-test sentinel");
  fiesta_test_console("watch 15000");
  (void)fiesta_test_tick();
  TEST_ASSERT_NOT_NULL(strstr(fiesta_test_debug_last_line(), "sentinel"));
  drain();
  TEST_ASSERT_NOT_NULL(strstr(fiesta_test_debug_last_line(), "watch 15000 ms"));
  deb("reply-test sentinel");
  fiesta_test_console("watch 0");
  (void)fiesta_test_tick();
  TEST_ASSERT_NOT_NULL(strstr(fiesta_test_debug_last_line(), "sentinel"));
  drain();
  TEST_ASSERT_NOT_NULL(strstr(fiesta_test_debug_last_line(), "watch off"));
}

static void test_manual_command_applies_without_printing_on_control_role(void) {
  TEST_ASSERT_TRUE(initTests());
  VP37Pump *pump = &getECUContext()->injectionPump;
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 9100;
  pump->demand.physicalLimitPercent = 100.0f;
  testsWorkersManualHoldSet(4321U);
  deb("reply-test sentinel");
  tickTestsHandleSerialLine("S23.5");
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_EQUAL_STRING("manual", testsActiveName());
  TEST_ASSERT_EQUAL_FLOAT(23.5f, pump->demand.requestedPercent);
  TEST_ASSERT_NOT_NULL(strstr(fiesta_test_debug_last_line(), "sentinel"));
  testsPrintEvents();
  TEST_ASSERT_NOT_NULL(
      strstr(fiesta_test_debug_last_line(), "TEST: manual started"));
  deb("reply-test sentinel");
  testsPrintEvents();
  TEST_ASSERT_NOT_NULL(strstr(fiesta_test_debug_last_line(), "sentinel"));
}

static void
test_parameter_and_rejected_commands_never_print_on_control_role(void) {
  TEST_ASSERT_TRUE(initTests());
  static const char *const commands[] = {
      "P0.025", "R", "watch 999", "set missing 7", "run missing", "unknown"};
  for (unsigned i = 0U; i < COUNTOF(commands); ++i) {
    deb("command-test sentinel");
    tickTestsHandleSerialLine(commands[i]);
    (void)tickTests();
    TEST_ASSERT_EQUAL_STRING("command-test sentinel",
                             fiesta_test_debug_last_line());
    TEST_ASSERT_NOT_NULL(strstr(fiesta_test_serial_last_line(), "sentinel"));
    testsPrintEvents();
    TEST_ASSERT_NULL(strstr(fiesta_test_serial_last_line(), "sentinel"));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_replies_capture_each_payload_and_only_run_on_session_role);
  RUN_TEST(test_reply_overflow_is_bounded_and_reset_discards_pending);
  RUN_TEST(test_watch_reply_waits_for_session_and_captures_the_limit);
  RUN_TEST(test_manual_command_applies_without_printing_on_control_role);
  RUN_TEST(test_parameter_and_rejected_commands_never_print_on_control_role);
  return UNITY_END();
}

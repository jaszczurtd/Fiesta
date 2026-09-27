#include "sc_transport_timeout.h"

#include <stdio.h>

static int s_failures;

#define TEST_ASSERT(condition, message)                                        \
  do {                                                                         \
    if (!(condition)) {                                                        \
      (void)fprintf(stderr, "FAIL: %s\n", (message));                          \
      ++s_failures;                                                            \
    }                                                                          \
  } while (0)

static void test_commit_uses_extended_deadlines(void) {
  TEST_ASSERT(sc_transport_command_timeout_ms(SC_CMD_COMMIT_PARAMS, 0) ==
                  SC_TRANSPORT_COMMIT_PRIMARY_TIMEOUT_MS,
              "commit primary timeout");
  TEST_ASSERT(sc_transport_command_timeout_ms(SC_CMD_COMMIT_PARAMS, 1) ==
                  SC_TRANSPORT_COMMIT_RETRY_TIMEOUT_MS,
              "commit retry timeout");
}

static void test_other_payloads_keep_generic_deadlines(void) {
  TEST_ASSERT(sc_transport_command_timeout_ms(SC_CMD_GET_VALUES, 0) ==
                  SC_TRANSPORT_PRIMARY_TIMEOUT_MS,
              "read primary timeout");
  TEST_ASSERT(sc_transport_command_timeout_ms(SC_CMD_GET_VALUES, 1) ==
                  SC_TRANSPORT_RETRY_TIMEOUT_MS,
              "read retry timeout");
  TEST_ASSERT(sc_transport_command_timeout_ms("SC_COMMIT_PARAMS ", 0) ==
                  SC_TRANSPORT_PRIMARY_TIMEOUT_MS,
              "malformed commit timeout");
  TEST_ASSERT(sc_transport_command_timeout_ms(NULL, 0) ==
                  SC_TRANSPORT_PRIMARY_TIMEOUT_MS,
              "null command timeout");
}

static void test_only_a_lost_session_triggers_a_new_hello(void) {
  TEST_ASSERT(sc_transport_reply_requires_hello("SC_NOT_READY HELLO_REQUIRED"),
              "hello required");
  TEST_ASSERT(
      !sc_transport_reply_requires_hello("SC_NOT_READY STORAGE_RECOVERY"),
      "storage recovery keeps the session");
  TEST_ASSERT(!sc_transport_reply_requires_hello("SC_NOT_READY BUSY"),
              "busy keeps the session");
  TEST_ASSERT(!sc_transport_reply_requires_hello("SC_NOT_READY ENGINE_RUNNING"),
              "engine running keeps the session");
  TEST_ASSERT(
      !sc_transport_reply_requires_hello("SC_NOT_READY HELLO_REQUIREDX"),
      "longer tag");
  TEST_ASSERT(!sc_transport_reply_requires_hello("SC_OK HELLO"), "ok reply");
  TEST_ASSERT(!sc_transport_reply_requires_hello(NULL), "null reply");
}

int main(void) {
  test_commit_uses_extended_deadlines();
  test_only_a_lost_session_triggers_a_new_hello();
  test_other_payloads_keep_generic_deadlines();
  if (s_failures != 0) {
    return 1;
  }
  (void)puts("Serial transport timeout tests passed.");
  return 0;
}

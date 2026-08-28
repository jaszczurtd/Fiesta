#include "ecuPersistence.h"
#include "unity.h"

static unsigned s_callCount;

void setUp(void) { s_callCount = 0u; }

void tearDown(void) {}

static hal_status_t succeed(const void *user) {
  const unsigned *value = static_cast<const unsigned *>(user);
  ++s_callCount;
  return value != nullptr && *value == 1u ? HAL_OK : HAL_EINVAL;
}

static hal_status_t fail(const void *user) {
  (void)user;
  ++s_callCount;
  return HAL_EIO;
}

void test_execute_runs_operation_and_reports_resume(void) {
  const unsigned value = 1u;
  hal_status_t resumeStatus = HAL_NONE;

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ecuPersistenceExecute(succeed, &value, &resumeStatus));
  TEST_ASSERT_EQUAL_UINT(1u, value);
  TEST_ASSERT_EQUAL_UINT(1u, s_callCount);
  TEST_ASSERT_EQUAL_INT(HAL_OK, resumeStatus);
}

void test_execute_propagates_storage_failure_and_releases_mutex(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EIO, ecuPersistenceExecute(fail, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, ecuPersistenceExecute(fail, NULL, NULL));
  TEST_ASSERT_EQUAL_UINT(2u, s_callCount);
}

void test_execute_rejects_missing_operation(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, ecuPersistenceExecute(NULL, NULL, NULL));
  TEST_ASSERT_EQUAL_UINT(0u, s_callCount);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_execute_runs_operation_and_reports_resume);
  RUN_TEST(test_execute_propagates_storage_failure_and_releases_mutex);
  RUN_TEST(test_execute_rejects_missing_operation);
  return UNITY_END();
}

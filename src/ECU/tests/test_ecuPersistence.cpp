#include "ecuPersistence.h"
#include "unity.h"
#include <hal/gps/hal_gps.h>
#include <hal/impl/.mock/hal_mock.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>

static unsigned s_callCount;
static unsigned s_pauseCount;
static unsigned s_resumeCount;
static bool s_paused;
static hal_status_t s_pauseStatus;
static hal_status_t s_resumeStatus;

/* Transport doubles let the test observe the exact storage boundary. */
extern "C" hal_status_t hal_gps_pause(void) {
  ++s_pauseCount;
  if (s_pauseStatus == HAL_OK) {
    s_paused = true;
  }
  return s_pauseStatus;
}

extern "C" hal_status_t hal_gps_resume(void) {
  ++s_resumeCount;
  if (s_resumeStatus == HAL_OK) {
    s_paused = false;
  }
  return s_resumeStatus;
}

void setUp(void) {
  s_callCount = s_pauseCount = s_resumeCount = 0u;
  s_paused = false;
  s_pauseStatus = s_resumeStatus = HAL_OK;
  hal_mock_set_millis(0u);
  hal_mock_eeprom_reset();
  hal_mock_kv_full_reset();
}

void tearDown(void) {
  hal_mock_eeprom_reset();
  hal_mock_kv_full_reset();
}

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

static hal_status_t initializeKv(const void *user) {
  (void)user;
  return hal_kv_init_ex(ECU_KV_BASE, ECU_KV_SIZE);
}

static hal_status_t storeValue(const void *user) {
  return hal_kv_set_u32_ex(42u, *static_cast<const uint32_t *>(user));
}

static hal_status_t commitKv(const void *user) {
  (void)user;
  return hal_kv_commit_ex();
}

static void prepareKv(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, HAL_RP_FLASH_EEPROM_SIZE, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ecuPersistenceExecute(initializeKv, nullptr, nullptr));
  s_pauseCount = s_resumeCount = 0u;
  hal_mock_eeprom_clear_write_count();
}

static void checkGpsPausedDuringWrite(void *user) {
  ++*static_cast<unsigned *>(user);
  TEST_ASSERT_TRUE(s_paused);
}

void test_execute_without_flash_keeps_gps_running(void) {
  const unsigned value = 1u;
  hal_status_t resumeStatus = HAL_NONE;

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ecuPersistenceExecute(succeed, &value, &resumeStatus));
  TEST_ASSERT_EQUAL_UINT(1u, value);
  TEST_ASSERT_EQUAL_UINT(1u, s_callCount);
  TEST_ASSERT_EQUAL_INT(HAL_NONE, resumeStatus);
  TEST_ASSERT_EQUAL_UINT(0u, s_pauseCount);
  TEST_ASSERT_EQUAL_UINT(0u, s_resumeCount);
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

void test_gps_pause_covers_only_physical_publication(void) {
  prepareKv();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ecuPersistenceExecute(initializeKv, nullptr, nullptr));
  TEST_ASSERT_EQUAL_UINT(0u, s_pauseCount);

  const uint32_t value = 17u;
  unsigned progress = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_set_progress_callback(
                                    checkGpsPausedDuringWrite, &progress));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(false));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ecuPersistenceExecute(storeValue, &value, nullptr));
  TEST_ASSERT_EQUAL_UINT(0u, s_pauseCount);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ecuPersistenceExecute(commitKv, nullptr, nullptr));
  TEST_ASSERT_EQUAL_UINT(1u, s_pauseCount);
  TEST_ASSERT_EQUAL_UINT(1u, s_resumeCount);
  TEST_ASSERT_GREATER_THAN_UINT(0u, progress);
  TEST_ASSERT_FALSE(s_paused);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(true));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ecuPersistenceExecute(storeValue, &value, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ecuPersistenceExecute(commitKv, nullptr, nullptr));
  TEST_ASSERT_EQUAL_UINT(1u, s_pauseCount);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_set_progress_callback(nullptr, nullptr));
}

void test_failed_pause_prevents_flash_write_and_allows_retry(void) {
  prepareKv();
  const uint32_t value = 19u;
  s_pauseStatus = HAL_EBUSY;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        ecuPersistenceExecute(storeValue, &value, nullptr));
  TEST_ASSERT_EQUAL_UINT(0u, s_resumeCount);
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
  s_pauseStatus = HAL_OK;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ecuPersistenceExecute(commitKv, nullptr, nullptr));
  TEST_ASSERT_EQUAL_UINT(1u, s_resumeCount);
}

void test_interrupted_write_still_resumes_gps(void) {
  prepareKv();
  const uint32_t value = 21u;
  hal_mock_eeprom_set_replace_fail_phase(
      HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_BODY);
  hal_status_t resumed = HAL_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        ecuPersistenceExecute(storeValue, &value, &resumed));
  TEST_ASSERT_EQUAL_INT(HAL_OK, resumed);
  TEST_ASSERT_EQUAL_UINT(1u, s_pauseCount);
  TEST_ASSERT_EQUAL_UINT(1u, s_resumeCount);
  TEST_ASSERT_FALSE(s_paused);
}

void test_failed_resume_does_not_undo_storage_and_is_retried(void) {
  prepareKv();
  const uint32_t value = 23u;
  s_resumeStatus = HAL_ENOMEM;
  hal_status_t resumed = HAL_NONE;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ecuPersistenceExecute(storeValue, &value, &resumed));
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, resumed);
  uint32_t loaded = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(42u, &loaded));
  TEST_ASSERT_EQUAL_UINT32(value, loaded);
  ecuPersistencePoll();
  TEST_ASSERT_EQUAL_UINT(1u, s_resumeCount);
  hal_mock_advance_millis(999u);
  ecuPersistencePoll();
  TEST_ASSERT_EQUAL_UINT(1u, s_resumeCount);
  s_resumeStatus = HAL_OK;
  hal_mock_advance_millis(1u);
  ecuPersistencePoll();
  TEST_ASSERT_EQUAL_UINT(2u, s_resumeCount);
  TEST_ASSERT_FALSE(s_paused);
  hal_mock_advance_millis(1000u);
  ecuPersistencePoll();
  TEST_ASSERT_EQUAL_UINT(2u, s_resumeCount);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_execute_without_flash_keeps_gps_running);
  RUN_TEST(test_execute_propagates_storage_failure_and_releases_mutex);
  RUN_TEST(test_execute_rejects_missing_operation);
  RUN_TEST(test_gps_pause_covers_only_physical_publication);
  RUN_TEST(test_failed_pause_prevents_flash_write_and_allows_retry);
  RUN_TEST(test_interrupted_write_still_resumes_gps);
  RUN_TEST(test_failed_resume_does_not_undo_storage_and_is_retried);
  return UNITY_END();
}

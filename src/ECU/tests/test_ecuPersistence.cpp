#include "ecuPersistence.h"
#include "unity.h"
#include <hal/impl/.mock/hal_mock.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>

static unsigned s_callCount;

void setUp(void) {
  s_callCount = 0u;
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
  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistenceExecute(initializeKv, nullptr));
  hal_mock_eeprom_clear_write_count();
}

static void countProgress(void *user) { ++*static_cast<unsigned *>(user); }

void test_execute_runs_the_operation_once(void) {
  const unsigned value = 1u;

  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistenceExecute(succeed, &value));
  TEST_ASSERT_EQUAL_UINT(1u, value);
  TEST_ASSERT_EQUAL_UINT(1u, s_callCount);
}

void test_execute_propagates_storage_failure_and_releases_mutex(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EIO, ecuPersistenceExecute(fail, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, ecuPersistenceExecute(fail, NULL));
  TEST_ASSERT_EQUAL_UINT(2u, s_callCount);
}

void test_execute_rejects_missing_operation(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, ecuPersistenceExecute(NULL, NULL));
  TEST_ASSERT_EQUAL_UINT(0u, s_callCount);
}

void test_only_a_commit_writes_flash(void) {
  prepareKv();

  const uint32_t value = 17u;
  unsigned progress = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_set_progress_callback(countProgress, &progress));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistenceExecute(storeValue, &value));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
  TEST_ASSERT_EQUAL_UINT(0u, progress);
  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistenceExecute(commitKv, nullptr));
  TEST_ASSERT_GREATER_THAN_UINT32(0u, hal_mock_eeprom_get_write_count());
  TEST_ASSERT_GREATER_THAN_UINT(0u, progress);
  uint32_t loaded = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(42u, &loaded));
  TEST_ASSERT_EQUAL_UINT32(value, loaded);

  /* An unchanged value needs no write. */
  hal_mock_eeprom_clear_write_count();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistenceExecute(storeValue, &value));
  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistenceExecute(commitKv, nullptr));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_write_count());
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_set_progress_callback(nullptr, nullptr));
}

void test_interrupted_write_is_reported_and_releases_mutex(void) {
  prepareKv();
  const uint32_t value = 21u;
  hal_mock_eeprom_tear_next_append(5u);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, ecuPersistenceExecute(storeValue, &value));

  const unsigned one = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistenceExecute(succeed, &one));
  TEST_ASSERT_EQUAL_UINT(1u, s_callCount);
}

void test_prepare_erases_the_spare_bank_once(void) {
  prepareKv();
  const uint32_t value = 25u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistenceExecute(storeValue, &value));
  hal_kv_stats_t stats = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_stats_ex(&stats));
  TEST_ASSERT_FALSE(stats.spare_erased);
  hal_mock_eeprom_clear_erase_count();

  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistencePrepareStorage());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_eeprom_get_erase_count());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_stats_ex(&stats));
  TEST_ASSERT_TRUE(stats.spare_erased);

  /* Already erased: no flash write. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistencePrepareStorage());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_eeprom_get_erase_count());
}

void test_prepare_before_kv_init_is_refused(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, ecuPersistencePrepareStorage());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_eeprom_get_erase_count());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_execute_runs_the_operation_once);
  RUN_TEST(test_execute_propagates_storage_failure_and_releases_mutex);
  RUN_TEST(test_execute_rejects_missing_operation);
  RUN_TEST(test_only_a_commit_writes_flash);
  RUN_TEST(test_interrupted_write_is_reported_and_releases_mutex);
  RUN_TEST(test_prepare_erases_the_spare_bank_once);
  RUN_TEST(test_prepare_before_kv_init_is_refused);
  return UNITY_END();
}

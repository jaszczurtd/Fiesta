#include "dtcManager.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/hal_kv.h"
#include "hardwareConfig.h"
#include "testable/dtcManager_testable.h"
#include "unity.h"

static constexpr uint16_t kLegacyBase = HAL_TOOLS_EEPROM_FIRST_ADDR + 96u;
static constexpr uint16_t kPreviousKvBase = kLegacyBase;
static constexpr uint16_t kDtcKvBase = HAL_TOOLS_EEPROM_FIRST_ADDR + 128u;
static constexpr uint16_t kDtcKvSize = ECU_EEPROM_SIZE_BYTES / 2u;
static constexpr uint16_t kSchemaKey = 0xD700u;
static constexpr uint16_t kLegacyMigratedKey = 0xD701u;
static constexpr uint16_t kFlagsBase = 0xD800u;
static constexpr uint16_t kTimestampBase = 0xD900u;
static constexpr uint16_t kForeignKey = 0xDA10u;
static constexpr uint32_t kSchemaVersion = 1u;
static constexpr uint32_t kLegacyMigratedVersion = 1u;
static constexpr uint32_t kStoredAndPermanent = 0x03u;
static constexpr uint32_t kLegacyMagic = 0x4454434Du;

static void countCommit(void *ctx) {
  uint32_t *count = static_cast<uint32_t *>(ctx);
  (*count)++;
}

void setUp(void) {
  hal_mock_set_millis(0u);
  hal_mock_eeprom_reset();
  hal_mock_gps_reset();
  dtcManagerResetRuntimeStateForTest();
}

void tearDown(void) {}

void test_recovery_merges_persisted_and_pending_dtc_entries(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, ECU_EEPROM_SIZE_BYTES, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(kDtcKvBase, kDtcKvSize));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(kSchemaKey, kSchemaVersion));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_set_u32_ex(kFlagsBase, kStoredAndPermanent));

  hal_mock_eeprom_set_io_status(HAL_EIO);
  dtcManagerInit();
  hal_mock_eeprom_set_io_status(HAL_OK);

  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);
  TEST_ASSERT_EQUAL_UINT8(1u, dtcManagerCount(DTC_KIND_STORED));

  hal_mock_advance_millis(1000u);
  dtcManagerPoll();

  TEST_ASSERT_EQUAL_UINT8(2u, dtcManagerCount(DTC_KIND_STORED));
  uint32_t flags = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(kFlagsBase, &flags));
  TEST_ASSERT_EQUAL_UINT32(kStoredAndPermanent, flags);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_get_u32_ex((uint16_t)(kFlagsBase + 1u), &flags));
  TEST_ASSERT_EQUAL_UINT32(kStoredAndPermanent, flags);
}

void test_recovery_reinitializes_eeprom_before_kv(void) {
  hal_mock_eeprom_set_io_status(HAL_EIO);
  dtcManagerInit();

  hal_mock_eeprom_set_io_status(HAL_OK);
  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);
  hal_mock_advance_millis(1000u);
  dtcManagerPoll();

  TEST_ASSERT_EQUAL_UINT8(1u, dtcManagerCount(DTC_KIND_STORED));
  uint32_t flags = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_get_u32_ex((uint16_t)(kFlagsBase + 1u), &flags));
  TEST_ASSERT_EQUAL_UINT32(kStoredAndPermanent, flags);
}

void test_legacy_state_is_read_before_initializing_separate_kv(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, ECU_EEPROM_SIZE_BYTES, 0u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_write_int(kLegacyBase, (int32_t)kLegacyMagic));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_write_byte((uint16_t)(kLegacyBase + 4u), 2u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_write_byte((uint16_t)(kLegacyBase + 5u),
                                              (uint8_t)kStoredAndPermanent));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_commit());

  dtcManagerInit();

  TEST_ASSERT_EQUAL_UINT8(1u, dtcManagerCount(DTC_KIND_STORED));
  uint32_t flags = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(kFlagsBase, &flags));
  TEST_ASSERT_EQUAL_UINT32(kStoredAndPermanent, flags);
  uint32_t schema = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(kSchemaKey, &schema));
  TEST_ASSERT_EQUAL_UINT32(kSchemaVersion, schema);
  uint32_t migrated = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_get_u32_ex(kLegacyMigratedKey, &migrated));
  TEST_ASSERT_EQUAL_UINT32(kLegacyMigratedVersion, migrated);
  TEST_ASSERT_EQUAL_INT32((int32_t)kLegacyMagic,
                          hal_eeprom_read_int(kLegacyBase));

  TEST_ASSERT_TRUE(dtcManagerClearAll());
  dtcManagerResetRuntimeStateForTest();
  dtcManagerInit();
  TEST_ASSERT_EQUAL_UINT8(0u, dtcManagerCount(DTC_KIND_STORED));
}

void test_legacy_migration_uses_one_snapshot_commit(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, ECU_EEPROM_SIZE_BYTES, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(kDtcKvBase, kDtcKvSize));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_write_int(kLegacyBase, (int32_t)kLegacyMagic));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_write_byte((uint16_t)(kLegacyBase + 4u), 2u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_write_byte((uint16_t)(kLegacyBase + 5u),
                                              (uint8_t)kStoredAndPermanent));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_commit());

  uint32_t commitCount = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_set_progress_callback(countCommit, &commitCount));
  dtcManagerInit();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_set_progress_callback(nullptr, nullptr));

  /* One coalesced KV publish (legacy-migrated key + schema key + DTC flags
   * batched under a single hal_kv_commit(), not one write per key) now
   * flashes in 3 crash-safe phases -- invalidate the destination header,
   * write+verify the body, then publish the new header -- instead of one
   * flat write. 3 notifications therefore still means exactly one snapshot
   * commit; see hal_kv.cpp's publish_locked()/jh_eeprom_replace_region(). */
  TEST_ASSERT_EQUAL_UINT32(3u, commitCount);
  TEST_ASSERT_EQUAL_UINT8(1u, dtcManagerCount(DTC_KIND_STORED));
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(kLegacyMigratedKey, &value));
  TEST_ASSERT_EQUAL_UINT32(kLegacyMigratedVersion, value);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(kSchemaKey, &value));
  TEST_ASSERT_EQUAL_UINT32(kSchemaVersion, value);
  TEST_ASSERT_EQUAL_INT32((int32_t)kLegacyMagic,
                          hal_eeprom_read_int(kLegacyBase));
}

void test_migration_marker_prevents_legacy_resurrection_on_schema_change(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, ECU_EEPROM_SIZE_BYTES, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(kDtcKvBase, kDtcKvSize));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_kv_set_u32_ex(kLegacyMigratedKey, kLegacyMigratedVersion));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(kSchemaKey, 99u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_write_int(kLegacyBase, (int32_t)kLegacyMagic));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_write_byte((uint16_t)(kLegacyBase + 4u), 2u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_write_byte((uint16_t)(kLegacyBase + 5u),
                                              (uint8_t)kStoredAndPermanent));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_eeprom_commit());

  dtcManagerInit();

  TEST_ASSERT_EQUAL_UINT8(0u, dtcManagerCount(DTC_KIND_STORED));
  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(kSchemaKey, &value));
  TEST_ASSERT_EQUAL_UINT32(kSchemaVersion, value);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(kLegacyMigratedKey, &value));
  TEST_ASSERT_EQUAL_UINT32(kLegacyMigratedVersion, value);
  TEST_ASSERT_EQUAL_INT32((int32_t)kLegacyMagic,
                          hal_eeprom_read_int(kLegacyBase));
}

void test_existing_previous_kv_layout_remains_selected(void) {
  static const uint8_t foreignValue[] = {0x45u, 0x43u, 0x55u};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, ECU_EEPROM_SIZE_BYTES, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(kPreviousKvBase, kDtcKvSize));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(kSchemaKey, kSchemaVersion));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_set_u32_ex(kFlagsBase, kStoredAndPermanent));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_set_blob_ex(kForeignKey, foreignValue,
                                           (uint16_t)sizeof(foreignValue)));

  dtcManagerInit();

  TEST_ASSERT_EQUAL_UINT8(1u, dtcManagerCount(DTC_KIND_STORED));
  uint8_t actual[sizeof(foreignValue)] = {0u};
  uint16_t actualSize = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_kv_get_blob_ex(kForeignKey, actual, sizeof(actual), &actualSize));
  TEST_ASSERT_EQUAL_UINT16(sizeof(foreignValue), actualSize);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(foreignValue, actual, sizeof(foreignValue));
}

void test_schema_retry_persists_full_snapshot_and_removes_zero_timestamp(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_eeprom_init(HAL_EEPROM_FLASH, ECU_EEPROM_SIZE_BYTES, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(kDtcKvBase, kDtcKvSize));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_u32_ex(kTimestampBase, 123456u));

  hal_mock_eeprom_set_commit_status(HAL_EIO);
  dtcManagerInit();
  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);

  hal_mock_eeprom_set_commit_status(HAL_OK);
  hal_mock_advance_millis(1000u);
  dtcManagerPoll();

  uint32_t value = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_get_u32_ex(kSchemaKey, &value));
  TEST_ASSERT_EQUAL_UINT32(kSchemaVersion, value);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_kv_get_u32_ex((uint16_t)(kFlagsBase + 1u), &value));
  TEST_ASSERT_EQUAL_UINT32(kStoredAndPermanent, value);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_kv_get_u32_ex(kTimestampBase, &value));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_recovery_merges_persisted_and_pending_dtc_entries);
  RUN_TEST(test_recovery_reinitializes_eeprom_before_kv);
  RUN_TEST(test_legacy_state_is_read_before_initializing_separate_kv);
  RUN_TEST(test_legacy_migration_uses_one_snapshot_commit);
  RUN_TEST(test_migration_marker_prevents_legacy_resurrection_on_schema_change);
  RUN_TEST(test_existing_previous_kv_layout_remains_selected);
  RUN_TEST(test_schema_retry_persists_full_snapshot_and_removes_zero_timestamp);
  return UNITY_END();
}

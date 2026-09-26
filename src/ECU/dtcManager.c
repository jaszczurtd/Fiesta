#include "dtcManager.h"

#include "ecuPersistence.h"
#include "ecu_unit_testing.h"
#include "gps.h"
#include <utils/tools_common_defs.h>

#include <hal/system/hal_system.h>

#define DTC_EEPROM_MAGIC 0x4454434Du // "DTCM"
#define DTC_EEPROM_VERSION 2u
// The first 96 bytes stay reserved for legacy tooling; the DTC area has
// started here since the store was introduced, so the value is fixed.
#define DTC_EEPROM_BASE 96u
#define DTC_EEPROM_HEADER_SIZE 5u
#define DTC_EEPROM_SLOT_SIZE 2u
#define DTC_LEGACY_ENTRY_COUNT 9u
#define DTC_FLAG_STORED 0x01u
#define DTC_FLAG_PERMANENT 0x02u

#define DTC_KV_SCHEMA_KEY 0xD700u
#define DTC_KV_SCHEMA_VERSION 1u
#define DTC_KV_LEGACY_MIGRATED_KEY 0xD701u
#define DTC_KV_LEGACY_MIGRATED_VERSION 1u
#define DTC_KV_KEY_FLAGS_BASE 0xD800u
#define DTC_KV_KEY_TIMESTAMP_BASE 0xD900u
/* The flags record carries the ISO failure type byte in bits 8..15: one KV
 * key per entry, within the store's 32-key index (schema, migration marker,
 * flags and timestamp per entry, ECU parameter blob). Older records leave
 * those bits zero. */
#define DTC_KV_DETAIL_SHIFT 8u
#define DTC_KV_FLAGS_MASK 0xFFu
#define DTC_PERSIST_RETRY_MS 1000u

#if ECU_EEPROM_SIZE_BYTES < 1024u
#error "DTC KV requires at least 1024B EEPROM"
#endif

#if ECU_KV_BASE < (DTC_EEPROM_BASE + DTC_EEPROM_HEADER_SIZE +                  \
                   (DTC_LEGACY_ENTRY_COUNT * DTC_EEPROM_SLOT_SIZE))
#error "DTC KV must not overlap legacy DTC storage"
#endif

typedef struct {
  uint16_t code;
  bool active;
  bool stored;
  bool permanent;
  uint32_t firstOccurrence; // unix epoch from GPS, 0 = unknown
  uint8_t detail;           // ISO 14229-1 failure type byte, 0 = none
} dtc_entry_t;

typedef struct {
  dtc_entry_t dtcs[11];
  bool persistPending[11];
  bool clearPending;
  bool storageReady;
  bool schemaPending;
  bool legacyChecked;
  bool legacyValid;
  uint8_t legacyFlags[DTC_LEGACY_ENTRY_COUNT];
  uint32_t lastRetryMs;
  bool initialized;
} dtc_manager_state_t;

static dtc_manager_state_t s_dtcState = {
    .dtcs = {{DTC_OBD_CAN_INIT_FAIL, false, false, false, 0, 0u},
             {DTC_PCF8574_COMM_FAIL, false, false, false, 0, 0u},
             {DTC_PWM_CHANNEL_NOT_INIT, false, false, false, 0, 0u},
             {DTC_DPF_COMM_LOST, false, false, false, 0, 0u},
             {DTC_EGT_COMM_LOST, false, false, false, 0, 0u},
             {DTC_ADJ_COMM_LOST, false, false, false, 0, 0u},
             {DTC_ADJ_SIGNAL_LOST, false, false, false, 0, 0u},
             {DTC_ADJ_FUEL_TEMP_BROKEN, false, false, false, 0, 0u},
             {DTC_ADJ_VOLTAGE_BAD, false, false, false, 0, 0u},
             {DTC_RPM_IRQ_INIT_FAIL, false, false, false, 0, 0u},
             {DTC_CAN_BUS_FAULT, false, false, false, 0, 0u}},
    .persistPending = {false},
    .clearPending = false,
    .storageReady = false,
    .schemaPending = false,
    .legacyChecked = false,
    .legacyValid = false,
    .legacyFlags = {0u},
    .lastRetryMs = 0u,
    .initialized = false};

m_mutex_def(dtcManagerMutex);

#define DTC_COUNT ((uint8_t)COUNTOF(s_dtcState.dtcs))

/* Keys this module holds at once: schema, migration marker, one flags word
 * and one timestamp per entry; the ECU parameter blob takes one more. The
 * store indexes HAL_KV_MAX_KEYS distinct keys (32 in HAL builds without the
 * define). Growing the registry past the index fails every persist. */
#define DTC_KV_ENTRY_CAPACITY 11u
#ifdef HAL_KV_MAX_KEYS
#define DTC_KV_INDEX_CAPACITY HAL_KV_MAX_KEYS
#else
#define DTC_KV_INDEX_CAPACITY 32u
#endif
#if (2u + (2u * DTC_KV_ENTRY_CAPACITY) + 1u) > DTC_KV_INDEX_CAPACITY
#error "DTC key-value records exceed the store's key index"
#endif
_Static_assert(COUNTOF(s_dtcState.dtcs) == DTC_KV_ENTRY_CAPACITY,
               "DTC_KV_ENTRY_CAPACITY must follow the registry size");

/**
 * @brief Initialise dtcManagerMutex once, lazily.
 * @note Called from dtcManagerInit() and the read-side getters so a caller
 *       that locks before init never touches an uninitialised mutex.
 */
static void ensureDtcMutexInited(void) {
  static bool dtcMutexInited = false;
  if (!dtcMutexInited) {
    m_mutex_init(dtcManagerMutex);
    dtcMutexInited = true;
  }
}

/**
 * @brief Get the legacy EEPROM slot address for a DTC index.
 * @param idx Index of the DTC entry.
 * @return EEPROM address of the legacy storage slot.
 */
static uint16_t dtcSlotAddr(uint8_t idx) {
  return (uint16_t)(DTC_EEPROM_BASE + DTC_EEPROM_HEADER_SIZE +
                    (idx * DTC_EEPROM_SLOT_SIZE));
}

/**
 * @brief Get the KV key used to store DTC flags for an index.
 * @param idx Index of the DTC entry.
 * @return Key-value storage key for DTC flags.
 */
static uint16_t dtcKvKey(uint8_t idx) {
  return (uint16_t)(DTC_KV_KEY_FLAGS_BASE + idx);
}

/**
 * @brief Get the KV key used to store DTC timestamp for an index.
 * @param idx Index of the DTC entry.
 * @return Key-value storage key for DTC timestamp.
 */
static uint16_t dtcKvTimestampKey(uint8_t idx) {
  return (uint16_t)(DTC_KV_KEY_TIMESTAMP_BASE + idx);
}

/**
 * @brief Pack the persisted flags and detail of one entry into its KV word.
 * @param flags Packed flag byte.
 * @param detail ISO failure type byte.
 * @return Word stored under the entry's flags key.
 */
static uint32_t dtcKvWord(uint8_t flags, uint8_t detail) {
  return (uint32_t)flags | ((uint32_t)detail << DTC_KV_DETAIL_SHIFT);
}

/**
 * @brief Write the flags word and keep or drop the timestamp of one DTC.
 * @param idx Index of the DTC entry.
 * @param flags Packed flag byte.
 * @param detail ISO failure type byte.
 * @param firstOccurrence Timestamp to keep, 0 removes the key.
 * @return HAL_OK or the first failing KV status.
 */
static hal_status_t saveDtcRecords(uint8_t idx, uint8_t flags, uint8_t detail,
                                   uint32_t firstOccurrence) {
  hal_status_t status =
      hal_kv_set_u32_ex(dtcKvKey(idx), dtcKvWord(flags, detail));
  if (status == HAL_OK) {
    status = (firstOccurrence != 0u)
                 ? hal_kv_set_u32_ex(dtcKvTimestampKey(idx), firstOccurrence)
                 : hal_kv_delete_ex(dtcKvTimestampKey(idx));
  }
  return status;
}

/**
 * @brief Find the internal slot index for a DTC code.
 * @param code DTC code to search for.
 * @return Matching index, or -1 when the code is unknown.
 */
TESTABLE_STATIC int findDtcIndex(uint16_t code) {
  for (uint8_t i = 0; i < DTC_COUNT; i++) {
    if (s_dtcState.dtcs[i].code == code) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Build the persisted flag byte for a DTC entry.
 * @param idx Index of the DTC entry.
 * @return Packed flag byte for the selected entry.
 */
static uint8_t makeFlagsForIndex(uint8_t idx) {
  uint8_t flags = 0u;
  if (s_dtcState.dtcs[idx].stored) {
    flags |= DTC_FLAG_STORED;
  }
  if (s_dtcState.dtcs[idx].permanent) {
    flags |= DTC_FLAG_PERMANENT;
  }
  return flags;
}

/**
 * @brief Apply a persisted flag byte to one DTC entry.
 * @param idx Index of the DTC entry.
 * @param flags Packed flag byte to apply.
 */
static void applyFlagsToIndex(uint8_t idx, uint8_t flags) {
  s_dtcState.dtcs[idx].stored = (flags & DTC_FLAG_STORED) != 0u;
  s_dtcState.dtcs[idx].permanent = (flags & DTC_FLAG_PERMANENT) != 0u;
}

/**
 * Complete a best-effort deferred KV batch and restore automatic commits.
 * Committing successful writes also clears the dirty mirror after a later
 * write fails; callers retain pending state and retry the complete snapshot.
 */
static hal_status_t completeKvBatch(hal_status_t writeStatus) {
  const hal_status_t commitStatus = hal_kv_commit_ex();
  const hal_status_t restoreStatus = hal_kv_set_auto_commit(true);
  if (writeStatus != HAL_OK) {
    return writeStatus;
  }
  return commitStatus != HAL_OK ? commitStatus : restoreStatus;
}

typedef struct {
  bool includeSchema;
} dtc_full_snapshot_context_t;

static hal_status_t saveAllToKvOperation(const void *user) {
  const dtc_full_snapshot_context_t *context =
      (const dtc_full_snapshot_context_t *)user;
  const bool includeSchema = context != NULL && context->includeSchema;
  hal_status_t status = hal_kv_set_auto_commit(false);
  if (status != HAL_OK) {
    return status;
  }

  for (uint8_t i = 0; i < DTC_COUNT && status == HAL_OK; i++) {
    status = saveDtcRecords(i, makeFlagsForIndex(i), s_dtcState.dtcs[i].detail,
                            s_dtcState.dtcs[i].firstOccurrence);
  }
  if (status == HAL_OK && includeSchema) {
    /* Keep the migration marker and schema in the same EEPROM commit as the
     * full snapshot. The raw legacy bytes remain as a last-resort fallback,
     * while this marker prevents them from being applied again after a clear
     * or a later schema change. */
    status = hal_kv_set_u32_ex(DTC_KV_LEGACY_MIGRATED_KEY,
                               DTC_KV_LEGACY_MIGRATED_VERSION);
  }
  if (status == HAL_OK && includeSchema) {
    /* The schema marker is the final KV record in this snapshot. A failed or
     * interrupted write therefore remains distinguishable from complete data
     * on the next boot. */
    status = hal_kv_set_u32_ex(DTC_KV_SCHEMA_KEY, DTC_KV_SCHEMA_VERSION);
  }
  return completeKvBatch(status);
}

typedef struct {
  uint8_t idx;
  uint8_t flags;
  uint8_t detail;
  uint32_t firstOccurrence;
} dtc_persist_snapshot_t;

static hal_status_t saveDtcSnapshotOperation(const void *user) {
  const dtc_persist_snapshot_t *snapshot = (const dtc_persist_snapshot_t *)user;
  if (snapshot == NULL) {
    return HAL_EINVAL;
  }

  hal_status_t status = hal_kv_set_auto_commit(false);
  if (status != HAL_OK) {
    return status;
  }
  status = saveDtcRecords(snapshot->idx, snapshot->flags, snapshot->detail,
                          snapshot->firstOccurrence);
  return completeKvBatch(status);
}

static hal_status_t saveDtcSnapshotToKv(uint8_t idx, uint8_t flags,
                                        uint8_t detail,
                                        uint32_t firstOccurrence) {
  dtc_persist_snapshot_t snapshot = {
      .idx = idx,
      .flags = flags,
      .detail = detail,
      .firstOccurrence = firstOccurrence,
  };
  return ecuPersistenceExecute(saveDtcSnapshotOperation, &snapshot, NULL);
}

/**
 * @brief Save all DTC entries to key-value storage.
 * @return True when all writes succeed, otherwise false.
 */
static hal_status_t saveAllToKv(void) {
  dtc_full_snapshot_context_t context = {
      .includeSchema = false,
  };
  return ecuPersistenceExecute(saveAllToKvOperation, &context, NULL);
}

static bool saveSchemaSnapshotToKv(void) {
  dtc_full_snapshot_context_t context = {
      .includeSchema = true,
  };
  return hal_status_to_bool(
      ecuPersistenceExecute(saveAllToKvOperation, &context, NULL));
}

/**
 * @brief Clear all runtime and persisted flags in memory.
 */
static void resetAllState(void) {
  for (uint8_t i = 0; i < DTC_COUNT; i++) {
    s_dtcState.dtcs[i].active = false;
    s_dtcState.dtcs[i].stored = false;
    s_dtcState.dtcs[i].permanent = false;
    s_dtcState.dtcs[i].firstOccurrence = 0;
    s_dtcState.dtcs[i].detail = DTC_DETAIL_NONE;
    s_dtcState.persistPending[i] = false;
  }
}

static bool hasPendingSnapshots(void) {
  for (uint8_t i = 0u; i < DTC_COUNT; i++) {
    if (s_dtcState.persistPending[i]) {
      return true;
    }
  }
  return false;
}

static void clearPendingSnapshots(void) {
  for (uint8_t i = 0u; i < DTC_COUNT; i++) {
    s_dtcState.persistPending[i] = false;
  }
}

static void markAllSnapshotsPending(void) {
  for (uint8_t i = 0u; i < DTC_COUNT; i++) {
    s_dtcState.persistPending[i] = true;
  }
}

/**
 * @brief Cache legacy DTC bytes before the KV subsystem writes any headers.
 * @return HAL_OK after a complete check, or the EEPROM read failure.
 */
static hal_status_t captureLegacyState(void) {
  if (s_dtcState.legacyChecked) {
    return HAL_OK;
  }

  int32_t magic = 0;
  uint8_t version = 0u;
  hal_status_t status = hal_eeprom_read_int_ex(DTC_EEPROM_BASE, &magic);
  if (status == HAL_OK) {
    status =
        hal_eeprom_read_byte_ex((uint16_t)(DTC_EEPROM_BASE + 4u), &version);
  }
  if (status != HAL_OK) {
    return status;
  }

  const bool valid =
      magic == (int32_t)DTC_EEPROM_MAGIC && version == DTC_EEPROM_VERSION;
  if (valid) {
    for (uint8_t i = 0u; i < DTC_LEGACY_ENTRY_COUNT; i++) {
      status =
          hal_eeprom_read_byte_ex(dtcSlotAddr(i), &s_dtcState.legacyFlags[i]);
      if (status != HAL_OK) {
        return status;
      }
    }
  }

  s_dtcState.legacyValid = valid;
  s_dtcState.legacyChecked = true;
  return HAL_OK;
}

static void applyLegacyState(void) {
  if (!s_dtcState.legacyValid) {
    return;
  }
  for (uint8_t i = 0u; i < DTC_LEGACY_ENTRY_COUNT; i++) {
    applyFlagsToIndex(i, s_dtcState.legacyFlags[i]);
    s_dtcState.dtcs[i].active = false;
  }
}

/**
 * @brief Load all DTC state from key-value storage.
 * @return True when the load completed.
 */
static hal_status_t loadDtcFromKv(uint8_t idx) {
  uint32_t word = 0u;
  hal_status_t status = hal_kv_get_u32_ex(dtcKvKey(idx), &word);
  if (status == HAL_ENOENT) {
    word = 0u;
  } else if (status != HAL_OK) {
    return status;
  }
  const uint32_t flags = word & DTC_KV_FLAGS_MASK;
  applyFlagsToIndex(idx, (uint8_t)flags);
  s_dtcState.dtcs[idx].detail =
      (uint8_t)((word >> DTC_KV_DETAIL_SHIFT) & DTC_KV_FLAGS_MASK);
  s_dtcState.dtcs[idx].active = false;

  /* A timestamp without persisted DTC flags is an incomplete or stale
   * record. Keep the runtime value empty so the next full snapshot removes
   * the orphaned key. */
  if ((flags & (DTC_FLAG_STORED | DTC_FLAG_PERMANENT)) == 0u) {
    s_dtcState.dtcs[idx].firstOccurrence = 0u;
    s_dtcState.dtcs[idx].detail = DTC_DETAIL_NONE;
    return HAL_OK;
  }

  uint32_t timestamp = 0u;
  status = hal_kv_get_u32_ex(dtcKvTimestampKey(idx), &timestamp);
  if (status == HAL_ENOENT) {
    timestamp = 0u;
  } else if (status != HAL_OK) {
    return status;
  }
  s_dtcState.dtcs[idx].firstOccurrence = timestamp;
  return HAL_OK;
}

static hal_status_t loadAllFromKv(void) {
  for (uint8_t i = 0; i < DTC_COUNT; i++) {
    const hal_status_t status = loadDtcFromKv(i);
    if (status != HAL_OK) {
      return status;
    }
  }
  return HAL_OK;
}

static hal_status_t mergeNonPendingFromKv(void) {
  for (uint8_t i = 0u; i < DTC_COUNT; i++) {
    if (s_dtcState.persistPending[i]) {
      continue;
    }
    const hal_status_t status = loadDtcFromKv(i);
    if (status != HAL_OK) {
      return status;
    }
  }
  return HAL_OK;
}

static void mergeNonPendingLegacyState(void) {
  if (!s_dtcState.legacyValid) {
    return;
  }
  for (uint8_t i = 0u; i < DTC_LEGACY_ENTRY_COUNT; i++) {
    if (s_dtcState.persistPending[i]) {
      continue;
    }
    applyFlagsToIndex(i, s_dtcState.legacyFlags[i]);
    s_dtcState.dtcs[i].active = false;
  }
}

/**
 * @brief Compute the effective KV storage span available in EEPROM.
 * @return Even-sized usable KV span in bytes, or 0 when out of range.
 */
static uint16_t dtcKvEffectiveSpanForBase(uint16_t base) {
  uint32_t start = (uint32_t)base;
  uint32_t end = start + (uint32_t)ECU_KV_SIZE;
  uint16_t eepromSize = hal_eeprom_size();
  uint32_t maxEnd = (uint32_t)eepromSize;

  if (start >= maxEnd) {
    return 0u;
  }

  if (end > maxEnd) {
    end = maxEnd;
  }

  uint16_t span = (uint16_t)(end - start);
  if ((span & 1u) != 0u) {
    span--;
  }
  return span;
}

TESTABLE_STATIC uint16_t dtcKvEffectiveSpan(void) {
  return dtcKvEffectiveSpanForBase(ECU_KV_BASE);
}

static hal_status_t initializeKvOperation(const void *user) {
  (void)user;
  uint16_t eepromSize = 0u;
  hal_status_t status = hal_eeprom_size_ex(&eepromSize);
  if (status != HAL_OK || eepromSize != ECU_EEPROM_SIZE_BYTES) {
    status = hal_eeprom_init(HAL_EEPROM_FLASH, ECU_EEPROM_SIZE_BYTES, 0u);
    if (status != HAL_OK) {
      return status;
    }
    status = hal_eeprom_size_ex(&eepromSize);
    if (status != HAL_OK) {
      return status;
    }
  }

  status = captureLegacyState();
  if (status != HAL_OK) {
    return status;
  }

  const uint16_t span = dtcKvEffectiveSpan();
  if (span < 2u) {
    return HAL_EOVERFLOW;
  }
  status = hal_kv_init_ex(ECU_KV_BASE, span);
  if (status != HAL_OK) {
    return status;
  }
  /* ECU commands (e.g. SC_SET_PARAM) gate on live storage health, and DTC/ECU
   * param recovery must notice a fault that develops after init, not only
   * one caught at init or at the next write. hal_kv defaults to serving gets
   * from its RAM cache for speed; opt into read-through so a live EEPROM
   * fault surfaces through hal_kv_get_*_ex() instead of being masked. */
  return hal_kv_set_read_through(true);
}

static hal_status_t initializeKvStorage(uint16_t *outSpan) {
  if (outSpan == NULL) {
    return HAL_EINVAL;
  }
  const hal_status_t status =
      ecuPersistenceExecute(initializeKvOperation, NULL, NULL);
  *outSpan = dtcKvEffectiveSpan();
  return status;
}

static hal_status_t loadOrPrepareSchema(bool preserveRuntime) {
  uint32_t schemaVersion = 0u;
  hal_status_t schemaStatus =
      hal_kv_get_u32_ex(DTC_KV_SCHEMA_KEY, &schemaVersion);
  if (schemaStatus != HAL_OK && schemaStatus != HAL_ENOENT) {
    return schemaStatus;
  }
  uint32_t migratedVersion = 0u;
  const hal_status_t migratedStatus =
      hal_kv_get_u32_ex(DTC_KV_LEGACY_MIGRATED_KEY, &migratedVersion);
  if (migratedStatus != HAL_OK && migratedStatus != HAL_ENOENT) {
    return migratedStatus;
  }

  const bool schemaValid =
      schemaStatus == HAL_OK && schemaVersion == DTC_KV_SCHEMA_VERSION;
  const bool migratedValid = migratedStatus == HAL_OK &&
                             migratedVersion == DTC_KV_LEGACY_MIGRATED_VERSION;
  const bool useLegacy =
      !schemaValid && !migratedValid && s_dtcState.legacyValid;

  hal_status_t loadStatus = HAL_OK;
  if (useLegacy) {
    if (!preserveRuntime) {
      resetAllState();
      applyLegacyState();
    } else if (!s_dtcState.clearPending) {
      mergeNonPendingLegacyState();
    }
  } else if (!preserveRuntime) {
    loadStatus = loadAllFromKv();
  } else if (!s_dtcState.clearPending) {
    loadStatus = mergeNonPendingFromKv();
  }
  if (loadStatus != HAL_OK) {
    return loadStatus;
  }

  if (schemaValid && migratedValid) {
    s_dtcState.schemaPending = false;
    return HAL_OK;
  }

  markAllSnapshotsPending();
  s_dtcState.schemaPending = true;
  return HAL_OK;
}

/**
 * @brief Delete only DTC-owned keys from shared key-value storage.
 * @return True on success, otherwise false.
 */
static hal_status_t clearDtcKeysOperation(const void *user) {
  (void)user;
  hal_status_t status = hal_kv_set_auto_commit(false);
  if (status != HAL_OK) {
    return status;
  }

  for (uint8_t i = 0u; i < DTC_COUNT; i++) {
    hal_status_t deleteStatus = hal_kv_delete_ex(dtcKvKey(i));
    if (status == HAL_OK && deleteStatus != HAL_OK) {
      status = deleteStatus;
    }
    deleteStatus = hal_kv_delete_ex(dtcKvTimestampKey(i));
    if (status == HAL_OK && deleteStatus != HAL_OK) {
      status = deleteStatus;
    }
  }
  return completeKvBatch(status);
}

static bool clearDtcKeys(void) {
  return hal_status_to_bool(
      ecuPersistenceExecute(clearDtcKeysOperation, NULL, NULL));
}

void dtcManagerLogStorageStats(void) {
  const uint16_t eepromSize = hal_eeprom_size();
  const uint32_t kvStart = (uint32_t)ECU_KV_BASE;
  const uint16_t kvSpan = dtcKvEffectiveSpan();
  const uint32_t kvEndExclusive = kvStart + (uint32_t)kvSpan;
  const uint16_t bankSize = kvSpan / 2u;
  const uint16_t approxKeys =
      2u +
      ((uint16_t)DTC_COUNT * 2u); // schema + migration + flags + timestamps
  const uint32_t approxMinBytes =
      (uint32_t)approxKeys * 21u; // ~u32 record footprint

  deb("DTC storage: EEPROM=%uB, base=%u", (unsigned)eepromSize,
      (unsigned)DTC_EEPROM_BASE);
  if (kvSpan > 0u) {
    deb("DTC storage: KV base=%lu size=%uB range=[%lu..%lu], bank=%uB",
        (unsigned long)kvStart, (unsigned)kvSpan, (unsigned long)kvStart,
        (unsigned long)(kvEndExclusive - 1u), (unsigned)bankSize);
  } else {
    deb("DTC storage: KV base=%lu size=0B (out of EEPROM range)",
        (unsigned long)kvStart);
  }

  deb("DTC storage: approx u32 keys=%u, approx min live footprint=%luB (active "
      "bank)",
      (unsigned)approxKeys, (unsigned long)approxMinBytes);

  hal_kv_stats_t stats;
  if (hal_kv_get_stats(&stats)) {
    uint16_t freeBytes = 0u;
    if (stats.capacity_bytes > stats.used_bytes) {
      freeBytes = (uint16_t)(stats.capacity_bytes - stats.used_bytes);
    }
    deb("DTC storage: KV stats gen=%lu used=%uB free=%uB cap=%uB keys=%u "
        "nextSeq=%lu",
        (unsigned long)stats.generation, (unsigned)stats.used_bytes,
        (unsigned)freeBytes, (unsigned)stats.capacity_bytes,
        (unsigned)stats.key_count, (unsigned long)stats.next_sequence);
  } else {
    derr("DTC storage: hal_kv_get_stats failed");
  }
}

void dtcManagerInit(void) {
  ensureDtcMutexInited();
  m_mutex_enter_blocking(dtcManagerMutex);

  if (s_dtcState.initialized) {
    m_mutex_exit(dtcManagerMutex);
    return;
  }

  uint16_t kvSpan = 0u;
  const hal_status_t initStatus = initializeKvStorage(&kvSpan);
  if (initStatus != HAL_OK) {
    derr("DTC: storage initialization failed: %s (base=%u requested=%u "
         "effective=%u)",
         hal_status_to_string(initStatus), (unsigned)ECU_KV_BASE,
         (unsigned)ECU_KV_SIZE, (unsigned)kvSpan);
    resetAllState();
    s_dtcState.storageReady = false;
    s_dtcState.schemaPending = true;
    s_dtcState.lastRetryMs = hal_millis();
    s_dtcState.initialized = true;
    m_mutex_exit(dtcManagerMutex);
    return;
  }

  s_dtcState.storageReady = true;
  const hal_status_t loadStatus = loadOrPrepareSchema(false);
  if (loadStatus != HAL_OK) {
    s_dtcState.storageReady = false;
    s_dtcState.schemaPending = true;
    s_dtcState.lastRetryMs = hal_millis();
    derr("DTC: storage load failed: %s", hal_status_to_string(loadStatus));
  } else if (s_dtcState.schemaPending) {
    if (saveSchemaSnapshotToKv()) {
      s_dtcState.schemaPending = false;
      s_dtcState.clearPending = false;
      clearPendingSnapshots();
    } else {
      s_dtcState.lastRetryMs = hal_millis();
      derr("DTC: schema snapshot persistence failed; retry queued");
    }
  }
  s_dtcState.initialized = true;
  m_mutex_exit(dtcManagerMutex);
}

/**
 * @brief Ensure the module is initialised before a public API mutates state.
 * @note Call BEFORE acquiring dtcManagerMutex - dtcManagerInit() takes the
 *       mutex internally.
 */
static void ensureInitialized(void) {
  if (!s_dtcState.initialized) {
    dtcManagerInit();
  }
}

/**
 * @brief Shared body of the set-active API.
 * @param code DTC code to update.
 * @param active New active state.
 * @param updateDetail True to store @p detail, false keeps the recorded one.
 * @param detail Failure type byte applied when @p updateDetail is set.
 */
static void setActiveImpl(uint16_t code, bool active, bool updateDetail,
                          uint8_t detail) {
  ensureInitialized();

  m_mutex_enter_blocking(dtcManagerMutex);

  int idx = findDtcIndex(code);
  if (idx < 0) {
    m_mutex_exit(dtcManagerMutex);
    return;
  }

  bool changed = false;

  if (updateDetail && (s_dtcState.dtcs[idx].detail != detail)) {
    s_dtcState.dtcs[idx].detail = detail;
    changed = true;
  }

  if (s_dtcState.dtcs[idx].active != active) {
    s_dtcState.dtcs[idx].active = active;
    deb("DTC 0x%04X (%s) active=%d detail=0x%02X", code,
        dtcManagerGetName(code), active ? 1 : 0,
        (unsigned)s_dtcState.dtcs[idx].detail);
  }

  if (active) {
    if (!s_dtcState.dtcs[idx].stored) {
      s_dtcState.dtcs[idx].stored = true;
      changed = true;
      // Record GPS timestamp on first occurrence
      if (s_dtcState.dtcs[idx].firstOccurrence == 0) {
        s_dtcState.dtcs[idx].firstOccurrence = gpsGetEpoch();
      }
    }
    if (!s_dtcState.dtcs[idx].permanent) {
      s_dtcState.dtcs[idx].permanent = true;
      changed = true;
    }
  }

  uint8_t savedIdx = (uint8_t)idx;
  uint8_t savedFlags = changed ? makeFlagsForIndex(savedIdx) : 0u;
  uint8_t savedDetail = changed ? s_dtcState.dtcs[savedIdx].detail : 0u;
  uint32_t savedTs = changed ? s_dtcState.dtcs[savedIdx].firstOccurrence : 0u;
  if (changed) {
    s_dtcState.persistPending[savedIdx] = true;
  }
  /* A queued clear must delete stale keys first. The poll path then writes the
   * current snapshot, including this transition, in one batch. */
  if (changed && s_dtcState.storageReady && !s_dtcState.schemaPending &&
      !s_dtcState.clearPending) {
    const hal_status_t saved =
        saveDtcSnapshotToKv(savedIdx, savedFlags, savedDetail, savedTs);
    if (saved == HAL_OK) {
      s_dtcState.persistPending[savedIdx] = false;
    } else {
      s_dtcState.lastRetryMs = hal_millis();
      derr("DTC: failed to persist key=%u: %s", (unsigned)dtcKvKey(savedIdx),
           hal_status_to_string(saved));
    }
  }
  m_mutex_exit(dtcManagerMutex);
}

void dtcManagerSetActive(uint16_t code, bool active) {
  setActiveImpl(code, active, false, DTC_DETAIL_NONE);
}

void dtcManagerSetActiveDetail(uint16_t code, bool active, uint8_t detail) {
  setActiveImpl(code, active, true, detail);
}

uint8_t dtcManagerGetDetail(uint16_t code) {
  ensureInitialized();

  m_mutex_enter_blocking(dtcManagerMutex);
  const int idx = findDtcIndex(code);
  const uint8_t detail =
      (idx < 0) ? DTC_DETAIL_NONE : s_dtcState.dtcs[idx].detail;
  m_mutex_exit(dtcManagerMutex);
  return detail;
}

void dtcManagerPoll(void) {
  ensureInitialized();
  m_mutex_enter_blocking(dtcManagerMutex);
  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_dtcState.lastRetryMs) < DTC_PERSIST_RETRY_MS) {
    m_mutex_exit(dtcManagerMutex);
    return;
  }
  if (s_dtcState.storageReady && !s_dtcState.schemaPending &&
      !s_dtcState.clearPending && !hasPendingSnapshots()) {
    m_mutex_exit(dtcManagerMutex);
    return;
  }
  s_dtcState.lastRetryMs = now;

  if (!s_dtcState.storageReady) {
    uint16_t kvSpan = 0u;
    const bool preserveRuntime =
        s_dtcState.clearPending || hasPendingSnapshots();
    const hal_status_t initStatus = initializeKvStorage(&kvSpan);
    if (initStatus != HAL_OK) {
      m_mutex_exit(dtcManagerMutex);
      derr("DTC: storage initialization retry failed: %s",
           hal_status_to_string(initStatus));
      return;
    }
    s_dtcState.storageReady = true;
    const hal_status_t loadStatus = loadOrPrepareSchema(preserveRuntime);
    if (loadStatus != HAL_OK) {
      s_dtcState.storageReady = false;
      m_mutex_exit(dtcManagerMutex);
      derr("DTC: storage reload failed: %s", hal_status_to_string(loadStatus));
      return;
    }
  }

  if (s_dtcState.schemaPending) {
    if (!saveSchemaSnapshotToKv()) {
      m_mutex_exit(dtcManagerMutex);
      derr("DTC: schema snapshot retry failed");
      return;
    }
    s_dtcState.schemaPending = false;
    s_dtcState.clearPending = false;
    clearPendingSnapshots();
  }

  if (s_dtcState.clearPending) {
    if (!clearDtcKeys()) {
      m_mutex_exit(dtcManagerMutex);
      derr("DTC: persisted clear retry failed");
      return;
    }
    s_dtcState.clearPending = false;
  }

  if (hasPendingSnapshots()) {
    const hal_status_t saved = saveAllToKv();
    if (saved == HAL_OK) {
      clearPendingSnapshots();
    } else {
      m_mutex_exit(dtcManagerMutex);
      derr("DTC: snapshot retry failed: %s", hal_status_to_string(saved));
      return;
    }
  }
  m_mutex_exit(dtcManagerMutex);
}

bool dtcManagerClearAll(void) {
  ensureInitialized();

  m_mutex_enter_blocking(dtcManagerMutex);
  resetAllState();
  s_dtcState.clearPending =
      !s_dtcState.storageReady || s_dtcState.schemaPending || !clearDtcKeys();
  const bool clearPending = s_dtcState.clearPending;
  if (clearPending) {
    s_dtcState.lastRetryMs = hal_millis();
  }
  m_mutex_exit(dtcManagerMutex);

  if (clearPending) {
    derr("DTC runtime cleared; persisted clear queued");
  } else {
    deb("DTC memory cleared");
  }

  dtcManagerLogStorageStats();
  return !clearPending;
}

#ifdef UNIT_TEST
TESTABLE_STATIC void dtcManagerResetRuntimeStateForTest(void) {
  ensureDtcMutexInited();
  m_mutex_enter_blocking(dtcManagerMutex);
  resetAllState();
  s_dtcState.clearPending = false;
  s_dtcState.storageReady = false;
  s_dtcState.schemaPending = false;
  s_dtcState.legacyChecked = false;
  s_dtcState.legacyValid = false;
  for (uint8_t i = 0u; i < DTC_LEGACY_ENTRY_COUNT; i++) {
    s_dtcState.legacyFlags[i] = 0u;
  }
  s_dtcState.lastRetryMs = 0u;
  s_dtcState.initialized = false;
  m_mutex_exit(dtcManagerMutex);
}
#endif

uint8_t dtcManagerCount(dtc_kind_t kind) {
  ensureInitialized();

  m_mutex_enter_blocking(dtcManagerMutex);
  uint8_t count = 0;
  for (uint8_t i = 0; i < DTC_COUNT; i++) {
    switch (kind) {
    case DTC_KIND_STORED:
      if (s_dtcState.dtcs[i].stored) {
        count++;
      }
      break;
    case DTC_KIND_PENDING:
    case DTC_KIND_ACTIVE:
      if (s_dtcState.dtcs[i].active) {
        count++;
      }
      break;
    case DTC_KIND_PERMANENT:
      if (s_dtcState.dtcs[i].permanent) {
        count++;
      }
      break;
    default:
      break;
    }
  }
  m_mutex_exit(dtcManagerMutex);
  return count;
}

uint8_t dtcManagerGetCodes(dtc_kind_t kind, uint16_t *outCodes,
                           uint8_t maxCodes) {
  ensureInitialized();
  if (outCodes == NULL || maxCodes == 0) {
    return 0;
  }

  m_mutex_enter_blocking(dtcManagerMutex);
  uint8_t idx = 0;
  for (uint8_t i = 0; i < DTC_COUNT && idx < maxCodes; i++) {
    bool take = false;
    switch (kind) {
    case DTC_KIND_STORED:
      take = s_dtcState.dtcs[i].stored;
      break;
    case DTC_KIND_PENDING:
    case DTC_KIND_ACTIVE:
      take = s_dtcState.dtcs[i].active;
      break;
    case DTC_KIND_PERMANENT:
      take = s_dtcState.dtcs[i].permanent;
      break;
    default:
      break;
    }
    if (take) {
      outCodes[idx++] = s_dtcState.dtcs[i].code;
    }
  }
  m_mutex_exit(dtcManagerMutex);

  return idx;
}

uint32_t dtcManagerGetTimestamp(uint16_t code) {
  ensureInitialized();

  m_mutex_enter_blocking(dtcManagerMutex);
  int idx = findDtcIndex(code);
  uint32_t ts = (idx < 0) ? 0u : s_dtcState.dtcs[idx].firstOccurrence;
  m_mutex_exit(dtcManagerMutex);
  return ts;
}

typedef struct {
  uint16_t code;
  const char *name;
} dtc_name_entry_t;

const char *dtcManagerGetName(uint16_t code) {
  static const dtc_name_entry_t names[] = {
      {DTC_OBD_CAN_INIT_FAIL, "U1900 Network CAN communication fault"},
      {DTC_PCF8574_COMM_FAIL, "U190D PCF8574 I/O expander communication fault"},
      {DTC_CAN_BUS_FAULT, "U0073 Control module communication bus off"},
      {DTC_PWM_CHANNEL_NOT_INIT,
       "P0657 Actuator supply voltage A circuit/open"},
      {DTC_DPF_COMM_LOST,
       "U0100 Lost communication with DPF module (project mapping)"},
      {DTC_EGT_COMM_LOST, "U1902 Lost communication with EGT module"},
      {DTC_CAN0_INIT_FAIL, "U1903 CAN0 bus init failure"},
      {DTC_GPS_SIGNAL_LOST, "U1904 GPS data unavailable/stale"},
      {DTC_ISOTP_FC_TIMEOUT, "U1906 ISO-TP flow-control timeout"},
      {DTC_ISOTP_FC_ABORT, "U1907 ISO-TP flow-control abort from tester"},
      {DTC_ENGINE_OVERSPEED, "P0219 Engine overspeed condition"},
      {DTC_ECM_EEPROM_FAULT, "P062F Internal control module EEPROM error"},
      {DTC_SYSTEM_VOLTAGE_LOW, "P0562 System voltage low"},
      {DTC_SYSTEM_VOLTAGE_HIGH, "P0563 System voltage high"},
      {DTC_THROTTLE_RANGE_PERF,
       "P0121 Throttle/Pedal position range/performance"},
      {DTC_COOLANT_TEMP_RANGE,
       "P0116 Engine coolant temperature range/performance"},
      {DTC_INTAKE_TEMP_RANGE, "P0111 Intake air temperature range/performance"},
      {DTC_MAP_BARO_RANGE, "P0106 MAP/BARO pressure range/performance"},
      {DTC_FUEL_LEVEL_RANGE, "P0460 Fuel level sensor range/performance"},
      {DTC_ADJ_COMM_LOST, "U1908 Lost communication with Adjustometer module"},
      {DTC_ADJ_SIGNAL_LOST, "U1909 Adjustometer oscillator signal lost"},
      {DTC_ADJ_FUEL_TEMP_BROKEN,
       "U190A Adjustometer fuel temperature sensor fault"},
      {DTC_ADJ_VOLTAGE_BAD, "U190B Adjustometer supply voltage out of range"},
      {DTC_RPM_IRQ_INIT_FAIL, "U190C RPM interrupt core-affinity/init failure"},
  };
  const char *name = "Unknown DTC";

  for (size_t i = 0u; i < COUNTOF(names); i++) {
    if (names[i].code == code) {
      name = names[i].name;
      break;
    }
  }

  return name;
}

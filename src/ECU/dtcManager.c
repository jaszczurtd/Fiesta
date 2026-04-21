#include "dtcManager.h"
#include "ecu_unit_testing.h"
#include "gps.h"

#define DTC_EEPROM_MAGIC         0x4454434Du // "DTCM"
#define DTC_EEPROM_VERSION       2u
#define DTC_EEPROM_BASE          (HAL_TOOLS_EEPROM_FIRST_ADDR + 96)
#define DTC_EEPROM_HEADER_SIZE   5u
#define DTC_EEPROM_SLOT_SIZE     2u
#define DTC_FLAG_STORED          0x01u
#define DTC_FLAG_PERMANENT       0x02u

#define DTC_KV_BASE              DTC_EEPROM_BASE
#define DTC_KV_SIZE              (ECU_EEPROM_SIZE_BYTES / 2)
#define DTC_KV_SCHEMA_KEY        0xD700u
#define DTC_KV_SCHEMA_VERSION    1u
#define DTC_KV_KEY_FLAGS_BASE    0xD800u
#define DTC_KV_KEY_TIMESTAMP_BASE 0xD900u

#if ECU_EEPROM_SIZE_BYTES < 1024u
#error "DTC KV requires at least 1024B EEPROM"
#endif

typedef struct {
  uint16_t code;
  bool active;
  bool stored;
  bool permanent;
  uint32_t firstOccurrence;  // unix epoch from GPS, 0 = unknown
} dtc_entry_t;

typedef struct {
  dtc_entry_t dtcs[9];
  bool initialized;
} dtc_manager_state_t;

static dtc_manager_state_t s_dtcState = {
  .dtcs = {
    {DTC_OBD_CAN_INIT_FAIL, false, false, false, 0},
    {DTC_PCF8574_COMM_FAIL, false, false, false, 0},
    {DTC_PWM_CHANNEL_NOT_INIT, false, false, false, 0},
    {DTC_DPF_COMM_LOST, false, false, false, 0},
    {DTC_EGT_COMM_LOST, false, false, false, 0},
    {DTC_ADJ_COMM_LOST, false, false, false, 0},
    {DTC_ADJ_SIGNAL_LOST, false, false, false, 0},
    {DTC_ADJ_FUEL_TEMP_BROKEN, false, false, false, 0},
    {DTC_ADJ_VOLTAGE_BAD, false, false, false, 0}
  },
  .initialized = false
};

#define DTC_COUNT ((uint8_t)COUNTOF(s_dtcState.dtcs))

/**
 * @brief Get the legacy EEPROM slot address for a DTC index.
 * @param idx Index of the DTC entry.
 * @return EEPROM address of the legacy storage slot.
 */
static uint16_t dtcSlotAddr(uint8_t idx) {
  return (uint16_t)(DTC_EEPROM_BASE + DTC_EEPROM_HEADER_SIZE + (idx * DTC_EEPROM_SLOT_SIZE));
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
 * @brief Find the internal slot index for a DTC code.
 * @param code DTC code to search for.
 * @return Matching index, or -1 when the code is unknown.
 */
TESTABLE_STATIC int findDtcIndex(uint16_t code) {
  for(uint8_t i = 0; i < DTC_COUNT; i++) {
    if(s_dtcState.dtcs[i].code == code) {
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
  if(s_dtcState.dtcs[idx].stored) {
    flags |= DTC_FLAG_STORED;
  }
  if(s_dtcState.dtcs[idx].permanent) {
    flags |= DTC_FLAG_PERMANENT;
  }
  return flags;
}

/**
 * @brief Apply a persisted flag byte to one DTC entry.
 * @param idx Index of the DTC entry.
 * @param flags Packed flag byte to apply.
 * @return None.
 */
static void applyFlagsToIndex(uint8_t idx, uint8_t flags) {
  s_dtcState.dtcs[idx].stored = (flags & DTC_FLAG_STORED) != 0u;
  s_dtcState.dtcs[idx].permanent = (flags & DTC_FLAG_PERMANENT) != 0u;
}

/**
 * @brief Save one DTC entry to key-value storage.
 * @param idx Index of the DTC entry to save.
 * @return True on success, otherwise false.
 */
static bool saveDtcToKv(uint8_t idx) {
  bool ok = hal_kv_set_u32(dtcKvKey(idx), (uint32_t)makeFlagsForIndex(idx));
  if(s_dtcState.dtcs[idx].firstOccurrence != 0) {
    ok = hal_kv_set_u32(dtcKvTimestampKey(idx), s_dtcState.dtcs[idx].firstOccurrence) && ok;
  }
  return ok;
}

/**
 * @brief Save all DTC entries to key-value storage.
 * @return True when all writes succeed, otherwise false.
 */
static bool saveAllToKv(void) {
  bool ok = true;
  for(uint8_t i = 0; i < DTC_COUNT; i++) {
    ok = saveDtcToKv(i) && ok;
  }
  return ok;
}

/**
 * @brief Clear all runtime and persisted flags in memory.
 * @return None.
 */
static void resetAllState(void) {
  for(uint8_t i = 0; i < DTC_COUNT; i++) {
    s_dtcState.dtcs[i].active = false;
    s_dtcState.dtcs[i].stored = false;
    s_dtcState.dtcs[i].permanent = false;
    s_dtcState.dtcs[i].firstOccurrence = 0;
  }
}

/**
 * @brief Check whether the legacy EEPROM DTC header is valid.
 * @return True when legacy DTC storage contains a recognized header.
 */
static bool legacyHeaderIsValid(void) {
  int32_t magic = hal_eeprom_read_int(DTC_EEPROM_BASE);
  uint8_t version = hal_eeprom_read_byte((uint16_t)(DTC_EEPROM_BASE + 4));
  return (magic == (int32_t)DTC_EEPROM_MAGIC) && (version == DTC_EEPROM_VERSION);
}

/**
 * @brief Try to migrate DTC flags from legacy EEPROM storage into KV storage.
 * @return True when migration succeeded, otherwise false.
 */
static bool tryMigrateLegacyFromEeprom(void) {
  if(!legacyHeaderIsValid()) {
    return false;
  }

  for(uint8_t i = 0; i < DTC_COUNT; i++) {
    uint8_t flags = hal_eeprom_read_byte(dtcSlotAddr(i));
    applyFlagsToIndex(i, flags);
    s_dtcState.dtcs[i].active = false;
  }

  return saveAllToKv();
}

/**
 * @brief Load all DTC state from key-value storage.
 * @return True when the load completed.
 */
static bool loadAllFromKv(void) {
  for(uint8_t i = 0; i < DTC_COUNT; i++) {
    uint32_t flags = 0u;
    if(!hal_kv_get_u32(dtcKvKey(i), &flags)) {
      flags = 0u;
    }
    applyFlagsToIndex(i, (uint8_t)flags);
    s_dtcState.dtcs[i].active = false;

    uint32_t ts = 0u;
    hal_kv_get_u32(dtcKvTimestampKey(i), &ts);
    s_dtcState.dtcs[i].firstOccurrence = ts;
  }
  return true;
}

/**
 * @brief Write the active KV schema version marker.
 * @return True on success, otherwise false.
 */
static bool writeKvSchemaVersion(void) {
  return hal_kv_set_u32(DTC_KV_SCHEMA_KEY, DTC_KV_SCHEMA_VERSION);
}

/**
 * @brief Compute the effective KV storage span available in EEPROM.
 * @return Even-sized usable KV span in bytes, or 0 when out of range.
 */
TESTABLE_STATIC uint16_t dtcKvEffectiveSpan(void) {
  uint32_t start = (uint32_t)DTC_KV_BASE;
  uint32_t end = start + (uint32_t)DTC_KV_SIZE;
  uint16_t eepromSize = hal_eeprom_size();
  uint32_t maxEnd = (uint32_t)eepromSize;

  if(start >= maxEnd) {
    return 0u;
  }

  if(end > maxEnd) {
    end = maxEnd;
  }

  uint16_t span = (uint16_t)(end - start);
  if((span & 1u) != 0u) {
    span--;
  }
  return span;
}

/**
 * @brief Clear the EEPROM area reserved for DTC key-value storage.
 * @return True on success, otherwise false.
 */
static bool resetDtcKvRegion(void) {
  uint32_t start = (uint32_t)DTC_KV_BASE;
  uint16_t eepromSize = hal_eeprom_size();
  uint16_t span = dtcKvEffectiveSpan();

  if(span == 0u) {
    derr("DTC: KV reset failed, base out of EEPROM range (base=%lu size=%u eeprom=%u)",
      (unsigned long)start, (unsigned)DTC_KV_SIZE, (unsigned)eepromSize);
    return false;
  }

  if(span < 2u) {
    derr("DTC: KV reset failed, effective KV span too small (%u)", (unsigned)span);
    return false;
  }

  for(uint32_t addr = start; addr < (start + span); addr++) {
    hal_eeprom_write_byte((uint16_t)addr, 0u);
  }
  hal_eeprom_commit();

  if(!hal_kv_init((uint16_t)start, span)) {
    derr("DTC: hal_kv_init failed after KV reset (base=%lu size=%u)",
      (unsigned long)start, (unsigned)span);
    return false;
  }

  return true;
}

void dtcManagerLogStorageStats(void) {
  const uint16_t eepromSize = hal_eeprom_size();
  const uint32_t kvStart = (uint32_t)DTC_KV_BASE;
  const uint16_t kvSpan = dtcKvEffectiveSpan();
  const uint32_t kvEndExclusive = kvStart + (uint32_t)kvSpan;
  const uint16_t bankSize = kvSpan / 2u;
  const uint16_t approxKeys = 1u + ((uint16_t)DTC_COUNT * 2u); // schema + flags + timestamps
  const uint32_t approxMinBytes = (uint32_t)approxKeys * 21u;   // ~u32 record footprint

  deb("DTC storage: EEPROM=%uB, FIRST_ADDR=%u", (unsigned)eepromSize, (unsigned)HAL_TOOLS_EEPROM_FIRST_ADDR);
  if(kvSpan > 0u) {
    deb("DTC storage: KV base=%lu size=%uB range=[%lu..%lu], bank=%uB",
      (unsigned long)kvStart,
      (unsigned)kvSpan,
      (unsigned long)kvStart,
      (unsigned long)(kvEndExclusive - 1u),
      (unsigned)bankSize);
  } else {
    deb("DTC storage: KV base=%lu size=0B (out of EEPROM range)", (unsigned long)kvStart);
  }

  deb("DTC storage: approx u32 keys=%u, approx min live footprint=%luB (active bank)",
    (unsigned)approxKeys, (unsigned long)approxMinBytes);

  hal_kv_stats_t stats;
  if(hal_kv_get_stats(&stats)) {
    uint16_t freeBytes = 0u;
    if(stats.capacity_bytes > stats.used_bytes) {
      freeBytes = (uint16_t)(stats.capacity_bytes - stats.used_bytes);
    }
    deb("DTC storage: KV stats gen=%lu used=%uB free=%uB cap=%uB keys=%u nextSeq=%lu",
      (unsigned long)stats.generation,
      (unsigned)stats.used_bytes,
      (unsigned)freeBytes,
      (unsigned)stats.capacity_bytes,
      (unsigned)stats.key_count,
      (unsigned long)stats.next_sequence);
  } else {
    derr("DTC storage: hal_kv_get_stats failed");
  }
}

void dtcManagerInit(void) {
  if(s_dtcState.initialized) {
    return;
  }

  uint16_t kvSpan = dtcKvEffectiveSpan();
  if(kvSpan < 2u) {
    derr("DTC: invalid KV span (base=%u requested=%u effective=%u eeprom=%u)",
      (unsigned)DTC_KV_BASE, (unsigned)DTC_KV_SIZE, (unsigned)kvSpan, (unsigned)hal_eeprom_size());
    resetAllState();
    s_dtcState.initialized = true;
    return;
  }

  if(!hal_kv_init(DTC_KV_BASE, kvSpan)) {
    derr("DTC: hal_kv_init failed (base=%u requested=%u effective=%u)",
      (unsigned)DTC_KV_BASE, (unsigned)DTC_KV_SIZE, (unsigned)kvSpan);
    resetAllState();
    s_dtcState.initialized = true;
    return;
  }

  uint32_t schemaVersion = 0u;
  bool hasSchema = hal_kv_get_u32(DTC_KV_SCHEMA_KEY, &schemaVersion);
  if(!hasSchema || schemaVersion != DTC_KV_SCHEMA_VERSION) {
    resetAllState();

    bool migrated = tryMigrateLegacyFromEeprom();
    bool wroteSchema = writeKvSchemaVersion();
    (void)migrated;
    if(!wroteSchema) {
      derr("DTC: failed to write KV schema version");
    }

    s_dtcState.initialized = true;
    return;
  }

  loadAllFromKv();

  s_dtcState.initialized = true;
}

void dtcManagerSetActive(uint16_t code, bool active) {
  if(!s_dtcState.initialized) {
    dtcManagerInit();
  }

  int idx = findDtcIndex(code);
  if(idx < 0) {
    return;
  }

  bool changed = false;

  if(s_dtcState.dtcs[idx].active != active) {
    s_dtcState.dtcs[idx].active = active;
    deb("DTC 0x%04X (%s) active=%d", code, getDtcName(code), active ? 1 : 0);
  }

  if(active) {
    if(!s_dtcState.dtcs[idx].stored) {
      s_dtcState.dtcs[idx].stored = true;
      changed = true;
      // Record GPS timestamp on first occurrence
      if(s_dtcState.dtcs[idx].firstOccurrence == 0) {
        s_dtcState.dtcs[idx].firstOccurrence = gpsGetEpoch();
      }
    }
    if(!s_dtcState.dtcs[idx].permanent) {
      s_dtcState.dtcs[idx].permanent = true;
      changed = true;
    }
  }

  if(changed) {
    if(!saveDtcToKv((uint8_t)idx)) {
      derr("DTC: failed to persist key=%u", (unsigned)dtcKvKey((uint8_t)idx));
    }
  }
}

void dtcManagerClearAll(void) {
  if(!s_dtcState.initialized) {
    dtcManagerInit();
  }

  resetAllState();
  if(!resetDtcKvRegion()) {
    derr("DTC: KV region reset failed");
  }

  if(!writeKvSchemaVersion()) {
    derr("DTC: failed to write KV schema after clear");
  }

  deb("DTC memory cleared");

  dtcManagerLogStorageStats();
}

uint8_t dtcManagerCount(dtc_kind_t kind) {
  if(!s_dtcState.initialized) {
    dtcManagerInit();
  }

  uint8_t count = 0;
  for(uint8_t i = 0; i < DTC_COUNT; i++) {
    switch(kind) {
      case DTC_KIND_STORED:
        if(s_dtcState.dtcs[i].stored) {
          count++;
        }
        break;
      case DTC_KIND_PENDING:
      case DTC_KIND_ACTIVE:
        if(s_dtcState.dtcs[i].active) {
          count++;
        }
        break;
      case DTC_KIND_PERMANENT:
        if(s_dtcState.dtcs[i].permanent) {
          count++;
        }
        break;
      default:
        break;
    }
  }
  return count;
}

uint8_t dtcManagerGetCodes(dtc_kind_t kind, uint16_t *outCodes, uint8_t maxCodes) {
  if(!s_dtcState.initialized) {
    dtcManagerInit();
  }
  if(outCodes == NULL || maxCodes == 0) {
    return 0;
  }

  uint8_t idx = 0;
  for(uint8_t i = 0; i < DTC_COUNT && idx < maxCodes; i++) {
    bool take = false;
    switch(kind) {
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
    if(take) {
      outCodes[idx++] = s_dtcState.dtcs[i].code;
    }
  }

  return idx;
}

uint32_t dtcManagerGetTimestamp(uint16_t code) {
  if(!s_dtcState.initialized) {
    dtcManagerInit();
  }

  int idx = findDtcIndex(code);
  if(idx < 0) {
    return 0;
  }
  return s_dtcState.dtcs[idx].firstOccurrence;
}

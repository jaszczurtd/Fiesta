#include "dtcManager.h"
#include "gps.h"

#define DTC_EEPROM_MAGIC         0x4454434Du // "DTCM"
#define DTC_EEPROM_VERSION       2u
#define DTC_EEPROM_BASE          (EEPROM_FIRST_ADDR + 96)
#define DTC_EEPROM_HEADER_SIZE   5u
#define DTC_EEPROM_SLOT_SIZE     2u
#define DTC_FLAG_STORED          0x01u
#define DTC_FLAG_PERMANENT       0x02u

#define DTC_KV_BASE              DTC_EEPROM_BASE
#define DTC_KV_SIZE              256u
#define DTC_KV_SCHEMA_KEY        0xD700u
#define DTC_KV_SCHEMA_VERSION    1u
#define DTC_KV_KEY_FLAGS_BASE    0xD800u
#define DTC_KV_KEY_TIMESTAMP_BASE 0xD900u

typedef struct {
  uint16_t code;
  bool active;
  bool stored;
  bool permanent;
  uint32_t firstOccurrence;  // unix epoch from GPS, 0 = unknown
} dtc_entry_t;

static dtc_entry_t s_dtcs[] = {
  {DTC_OBD_CAN_INIT_FAIL, false, false, false, 0},
  {DTC_PCF8574_COMM_FAIL, false, false, false, 0},
  {DTC_PWM_CHANNEL_NOT_INIT, false, false, false, 0},
  {DTC_DPF_COMM_LOST, false, false, false, 0},
};

static const uint8_t s_dtcCount = sizeof(s_dtcs) / sizeof(s_dtcs[0]);
static bool s_dtcInitialized = false;

static uint16_t dtcSlotAddr(uint8_t idx) {
  return (uint16_t)(DTC_EEPROM_BASE + DTC_EEPROM_HEADER_SIZE + (idx * DTC_EEPROM_SLOT_SIZE));
}

static uint16_t dtcKvKey(uint8_t idx) {
  return (uint16_t)(DTC_KV_KEY_FLAGS_BASE + idx);
}

static uint16_t dtcKvTimestampKey(uint8_t idx) {
  return (uint16_t)(DTC_KV_KEY_TIMESTAMP_BASE + idx);
}

static int findDtcIndex(uint16_t code) {
  for(uint8_t i = 0; i < s_dtcCount; i++) {
    if(s_dtcs[i].code == code) {
      return i;
    }
  }
  return -1;
}

static uint8_t makeFlagsForIndex(uint8_t idx) {
  uint8_t flags = 0u;
  if(s_dtcs[idx].stored) {
    flags |= DTC_FLAG_STORED;
  }
  if(s_dtcs[idx].permanent) {
    flags |= DTC_FLAG_PERMANENT;
  }
  return flags;
}

static void applyFlagsToIndex(uint8_t idx, uint8_t flags) {
  s_dtcs[idx].stored = (flags & DTC_FLAG_STORED) != 0u;
  s_dtcs[idx].permanent = (flags & DTC_FLAG_PERMANENT) != 0u;
}

static bool saveDtcToKv(uint8_t idx) {
  bool ok = hal_kv_set_u32(dtcKvKey(idx), (uint32_t)makeFlagsForIndex(idx));
  if(s_dtcs[idx].firstOccurrence != 0) {
    ok = hal_kv_set_u32(dtcKvTimestampKey(idx), s_dtcs[idx].firstOccurrence) && ok;
  }
  return ok;
}

static bool saveAllToKv(void) {
  bool ok = true;
  for(uint8_t i = 0; i < s_dtcCount; i++) {
    ok = saveDtcToKv(i) && ok;
  }
  return ok;
}

static void resetAllState(void) {
  for(uint8_t i = 0; i < s_dtcCount; i++) {
    s_dtcs[i].active = false;
    s_dtcs[i].stored = false;
    s_dtcs[i].permanent = false;
    s_dtcs[i].firstOccurrence = 0;
  }
}

static bool legacyHeaderIsValid(void) {
  int32_t magic = hal_eeprom_read_int(DTC_EEPROM_BASE);
  uint8_t version = hal_eeprom_read_byte((uint16_t)(DTC_EEPROM_BASE + 4));
  return (magic == (int32_t)DTC_EEPROM_MAGIC) && (version == DTC_EEPROM_VERSION);
}

static bool tryMigrateLegacyFromEeprom(void) {
  if(!legacyHeaderIsValid()) {
    return false;
  }

  for(uint8_t i = 0; i < s_dtcCount; i++) {
    uint8_t flags = hal_eeprom_read_byte(dtcSlotAddr(i));
    applyFlagsToIndex(i, flags);
    s_dtcs[i].active = false;
  }

  return saveAllToKv();
}

static bool loadAllFromKv(void) {
  for(uint8_t i = 0; i < s_dtcCount; i++) {
    uint32_t flags = 0u;
    if(!hal_kv_get_u32(dtcKvKey(i), &flags)) {
      flags = 0u;
    }
    applyFlagsToIndex(i, (uint8_t)flags);
    s_dtcs[i].active = false;

    uint32_t ts = 0u;
    hal_kv_get_u32(dtcKvTimestampKey(i), &ts);
    s_dtcs[i].firstOccurrence = ts;
  }
  return true;
}

static bool writeKvSchemaVersion(void) {
  return hal_kv_set_u32(DTC_KV_SCHEMA_KEY, DTC_KV_SCHEMA_VERSION);
}

void dtcManagerInit(void) {
  if(s_dtcInitialized) {
    return;
  }

  if(!hal_kv_init(DTC_KV_BASE, DTC_KV_SIZE)) {
    derr("DTC: hal_kv_init failed (base=%u size=%u)",
      (unsigned)DTC_KV_BASE, (unsigned)DTC_KV_SIZE);
    resetAllState();
    s_dtcInitialized = true;
    return;
  }

  uint32_t schemaVersion = 0u;
  bool hasSchema = hal_kv_get_u32(DTC_KV_SCHEMA_KEY, &schemaVersion);
  if(!hasSchema || schemaVersion != DTC_KV_SCHEMA_VERSION) {
    resetAllState();

    bool migrated = tryMigrateLegacyFromEeprom();
    bool wroteSchema = writeKvSchemaVersion();
    if(!migrated) {
      saveAllToKv();
    }
    if(!wroteSchema) {
      derr("DTC: failed to write KV schema version");
    }

    s_dtcInitialized = true;
    return;
  }

  loadAllFromKv();

  s_dtcInitialized = true;
}

void dtcManagerSetActive(uint16_t code, bool active) {
  if(!s_dtcInitialized) {
    dtcManagerInit();
  }

  int idx = findDtcIndex(code);
  if(idx < 0) {
    return;
  }

  bool changed = false;

  if(s_dtcs[idx].active != active) {
    s_dtcs[idx].active = active;
    deb("DTC 0x%04X (%s) active=%d", code, getDtcName(code), active ? 1 : 0);
  }

  if(active) {
    if(!s_dtcs[idx].stored) {
      s_dtcs[idx].stored = true;
      changed = true;
      // Record GPS timestamp on first occurrence
      if(s_dtcs[idx].firstOccurrence == 0) {
        s_dtcs[idx].firstOccurrence = gpsGetEpoch();
      }
    }
    if(!s_dtcs[idx].permanent) {
      s_dtcs[idx].permanent = true;
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
  if(!s_dtcInitialized) {
    dtcManagerInit();
  }

  resetAllState();
  for(uint8_t i = 0; i < s_dtcCount; i++) {
    hal_kv_delete(dtcKvKey(i));
    hal_kv_delete(dtcKvTimestampKey(i));
  }
  writeKvSchemaVersion();
  deb("DTC memory cleared");
}

uint8_t dtcManagerCount(dtc_kind_t kind) {
  if(!s_dtcInitialized) {
    dtcManagerInit();
  }

  uint8_t count = 0;
  for(uint8_t i = 0; i < s_dtcCount; i++) {
    switch(kind) {
      case DTC_KIND_STORED:
        if(s_dtcs[i].stored) {
          count++;
        }
        break;
      case DTC_KIND_PENDING:
      case DTC_KIND_ACTIVE:
        if(s_dtcs[i].active) {
          count++;
        }
        break;
      case DTC_KIND_PERMANENT:
        if(s_dtcs[i].permanent) {
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
  if(!s_dtcInitialized) {
    dtcManagerInit();
  }
  if(outCodes == NULL || maxCodes == 0) {
    return 0;
  }

  uint8_t idx = 0;
  for(uint8_t i = 0; i < s_dtcCount && idx < maxCodes; i++) {
    bool take = false;
    switch(kind) {
      case DTC_KIND_STORED:
        take = s_dtcs[i].stored;
        break;
      case DTC_KIND_PENDING:
      case DTC_KIND_ACTIVE:
        take = s_dtcs[i].active;
        break;
      case DTC_KIND_PERMANENT:
        take = s_dtcs[i].permanent;
        break;
      default:
        break;
    }
    if(take) {
      outCodes[idx++] = s_dtcs[i].code;
    }
  }

  return idx;
}

uint32_t dtcManagerGetTimestamp(uint16_t code) {
  if(!s_dtcInitialized) {
    dtcManagerInit();
  }

  int idx = findDtcIndex(code);
  if(idx < 0) {
    return 0;
  }
  return s_dtcs[idx].firstOccurrence;
}

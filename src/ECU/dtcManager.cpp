#include "dtcManager.h"

#define DTC_EEPROM_MAGIC       0x4454434Du // "DTCM"
#define DTC_EEPROM_VERSION     2u
#define DTC_EEPROM_BASE        (EEPROM_FIRST_ADDR + 96)
#define DTC_EEPROM_HEADER_SIZE 5u
#define DTC_EEPROM_SLOT_SIZE   2u
#define DTC_FLAG_STORED        0x01u
#define DTC_FLAG_PERMANENT     0x02u

typedef struct {
  uint16_t code;
  bool active;
  bool stored;
  bool permanent;
} dtc_entry_t;

static dtc_entry_t s_dtcs[] = {
  {DTC_OBD_CAN_INIT_FAIL, false, false, false},
  {DTC_PCF8574_COMM_FAIL, false, false, false},
  {DTC_PWM_CHANNEL_NOT_INIT, false, false, false},
  {DTC_DPF_COMM_LOST, false, false, false},
};

static const uint8_t s_dtcCount = sizeof(s_dtcs) / sizeof(s_dtcs[0]);
static bool s_dtcInitialized = false;

static uint16_t dtcSlotAddr(uint8_t idx) {
  return (uint16_t)(DTC_EEPROM_BASE + DTC_EEPROM_HEADER_SIZE + (idx * DTC_EEPROM_SLOT_SIZE));
}

static int findDtcIndex(uint16_t code) {
  for(uint8_t i = 0; i < s_dtcCount; i++) {
    if(s_dtcs[i].code == code) {
      return i;
    }
  }
  return -1;
}

static void saveDtcToEeprom(uint8_t idx) {
  uint8_t flags = 0;
  if(s_dtcs[idx].stored) {
    flags |= DTC_FLAG_STORED;
  }
  if(s_dtcs[idx].permanent) {
    flags |= DTC_FLAG_PERMANENT;
  }

  uint16_t addr = dtcSlotAddr(idx);
  hal_eeprom_write_byte(addr, flags);
  hal_eeprom_write_byte((uint16_t)(addr + 1), 0);
}

static void saveAllToEeprom(void) {
  for(uint8_t i = 0; i < s_dtcCount; i++) {
    saveDtcToEeprom(i);
  }
  hal_eeprom_commit();
}

static void resetAllState(void) {
  for(uint8_t i = 0; i < s_dtcCount; i++) {
    s_dtcs[i].active = false;
    s_dtcs[i].stored = false;
    s_dtcs[i].permanent = false;
  }
}

static void writeHeader(void) {
  hal_eeprom_write_int(DTC_EEPROM_BASE, (int32_t)DTC_EEPROM_MAGIC);
  hal_eeprom_write_byte((uint16_t)(DTC_EEPROM_BASE + 4), DTC_EEPROM_VERSION);
}

void dtcManagerInit(void) {
  if(s_dtcInitialized) {
    return;
  }

  int32_t magic = hal_eeprom_read_int(DTC_EEPROM_BASE);
  uint8_t version = hal_eeprom_read_byte((uint16_t)(DTC_EEPROM_BASE + 4));
  bool validHeader = (magic == (int32_t)DTC_EEPROM_MAGIC) && (version == DTC_EEPROM_VERSION);

  if(!validHeader) {
    resetAllState();
    writeHeader();
    saveAllToEeprom();
    s_dtcInitialized = true;
    return;
  }

  for(uint8_t i = 0; i < s_dtcCount; i++) {
    uint8_t flags = hal_eeprom_read_byte(dtcSlotAddr(i));
    s_dtcs[i].stored = (flags & DTC_FLAG_STORED) != 0;
    s_dtcs[i].permanent = (flags & DTC_FLAG_PERMANENT) != 0;
    s_dtcs[i].active = false;
  }

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
    }
    if(!s_dtcs[idx].permanent) {
      s_dtcs[idx].permanent = true;
      changed = true;
    }
  }

  if(changed) {
    saveDtcToEeprom((uint8_t)idx);
    hal_eeprom_commit();
  }
}

void dtcManagerClearAll(void) {
  if(!s_dtcInitialized) {
    dtcManagerInit();
  }

  resetAllState();
  saveAllToEeprom();
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

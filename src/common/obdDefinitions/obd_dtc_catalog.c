#include "obd_dtc_catalog.h"

#include <hal/core/hal_array.h>

#define OBD_DTC_TABLE_ENTRY(name, value, description)                          \
  {(uint16_t)(value), description},

/* Both lists ascend by value, so the table is sorted for the binary search
 * below; test_obd_dtc_catalog checks that order on the host. */
static const obd_dtc_entry_t s_catalog[] = {
    OBD_DTC_CATALOG(OBD_DTC_TABLE_ENTRY)};

#undef OBD_DTC_TABLE_ENTRY

size_t obdDtcCatalogCount(void) { return COUNTOF(s_catalog); }

const obd_dtc_entry_t *obdDtcCatalogEntry(size_t index) {
  return (index < COUNTOF(s_catalog)) ? &s_catalog[index] : NULL;
}

const char *obdDtcDescription(uint16_t code) {
  const char *description = NULL;
  size_t low = 0U;
  size_t high = COUNTOF(s_catalog);

  while ((description == NULL) && (low < high)) {
    const size_t middle = low + ((high - low) / 2U);
    const uint16_t candidate = s_catalog[middle].code;

    if (candidate == code) {
      description = s_catalog[middle].description;
    } else if (candidate < code) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }

  return description;
}

hal_status_t obdDtcFormatCode(uint16_t code, char *buffer, size_t size) {
  static const char letters[] = "PCBU";
  static const char digits[] = "0123456789ABCDEF";
  hal_status_t status = HAL_EINVAL;

  if ((buffer != NULL) && (size >= OBD_DTC_TEXT_SIZE)) {
    buffer[0] = letters[(code >> 14) & 0x3U];
    buffer[1] = digits[(code >> 12) & 0x3U];
    buffer[2] = digits[(code >> 8) & 0xFU];
    buffer[3] = digits[(code >> 4) & 0xFU];
    buffer[4] = digits[code & 0xFU];
    buffer[5] = '\0';
    status = HAL_OK;
  }

  return status;
}

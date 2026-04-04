#ifndef T_DTC_MANAGER
#define T_DTC_MANAGER

#include <tools_c.h>
#include "obd-2_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DTC_KIND_STORED = 0,
  DTC_KIND_PENDING,
  DTC_KIND_PERMANENT,
  DTC_KIND_ACTIVE,
} dtc_kind_t;

void dtcManagerInit(void);
void dtcManagerSetActive(uint16_t code, bool active);
void dtcManagerClearAll(void);
void dtcManagerLogStorageStats(void);

uint8_t dtcManagerCount(dtc_kind_t kind);
uint8_t dtcManagerGetCodes(dtc_kind_t kind, uint16_t *outCodes, uint8_t maxCodes);
uint32_t dtcManagerGetTimestamp(uint16_t code);

#ifdef __cplusplus
}
#endif

#endif

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

/**
 * @brief Initialize DTC storage and load persisted state.
 * @return None.
 */
void dtcManagerInit(void);

/**
 * @brief Set or clear the active state of one DTC.
 * @param code DTC code to update.
 * @param active True to mark the code active, false to clear it.
 * @return None.
 */
void dtcManagerSetActive(uint16_t code, bool active);

/**
 * @brief Clear all DTC runtime and persisted state.
 * @return None.
 */
void dtcManagerClearAll(void);

/**
 * @brief Print DTC storage statistics for diagnostics.
 * @return None.
 */
void dtcManagerLogStorageStats(void);

/**
 * @brief Count DTCs that belong to the selected category.
 * @param kind DTC category to count.
 * @return Number of matching DTC entries.
 */
uint8_t dtcManagerCount(dtc_kind_t kind);

/**
 * @brief Copy DTC codes of the selected category into an output buffer.
 * @param kind DTC category to export.
 * @param outCodes Output buffer receiving matching codes.
 * @param maxCodes Maximum number of codes that fit in the output buffer.
 * @return Number of codes written to the output buffer.
 */
uint8_t dtcManagerGetCodes(dtc_kind_t kind, uint16_t *outCodes, uint8_t maxCodes);

/**
 * @brief Get the first-occurrence timestamp for a DTC.
 * @param code DTC code to query.
 * @return Unix epoch timestamp, or 0 when unknown or missing.
 */
uint32_t dtcManagerGetTimestamp(uint16_t code);

#ifdef __cplusplus
}
#endif

#endif

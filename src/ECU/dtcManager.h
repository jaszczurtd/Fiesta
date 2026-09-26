#ifndef T_DTC_MANAGER
#define T_DTC_MANAGER

#include "dtc_codes.h"
#include <JaszczurHAL.h>

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
 * @brief Translate a project DTC code into a human-readable label.
 * @param code Diagnostic trouble code to describe.
 * @return Pointer to a static name string.
 */
const char *dtcManagerGetName(uint16_t code);

/**
 * @brief Initialize DTC storage and load persisted state.
 */
void dtcManagerInit(void);

/**
 * @brief Set or clear the active state of one DTC.
 * @param code DTC code to update.
 * @param active True to mark the code active, false to clear it.
 */
void dtcManagerSetActive(uint16_t code, bool active);

/**
 * @brief Set or clear the active state of one DTC and record its detail.
 * @param code DTC code to update.
 * @param active True to mark the code active, false to clear it.
 * @param detail ISO 14229-1 failure type byte (DTC_DETAIL_*) that identifies
 *        the specific report behind this code. Persisted with the entry.
 */
void dtcManagerSetActiveDetail(uint16_t code, bool active, uint8_t detail);

/**
 * @brief Get the detail byte recorded for a DTC.
 * @param code DTC code to query.
 * @return Failure type byte, or DTC_DETAIL_NONE when unknown or never set.
 */
uint8_t dtcManagerGetDetail(uint16_t code);

/**
 * @brief Retry DTC persistence work left pending by a transient storage error.
 *
 * Call from the core-0 service loop. Retries are rate-limited internally.
 */
void dtcManagerPoll(void);

/**
 * @brief Clear all DTC runtime and persisted state.
 * @return True when persisted DTC keys were cleared immediately. False means
 *         runtime state was cleared and a durable retry was queued.
 */
bool dtcManagerClearAll(void);

/**
 * @brief Print DTC storage statistics for diagnostics.
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
uint8_t dtcManagerGetCodes(dtc_kind_t kind, uint16_t *outCodes,
                           uint8_t maxCodes);

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

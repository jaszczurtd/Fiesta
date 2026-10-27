#ifndef ECU_PERSISTENCE_H
#define ECU_PERSISTENCE_H

#include <hal/core/hal_status.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef hal_status_t (*ecu_persistence_operation_fn)(const void *user);

/** @brief Initialize the shared persistence serializer on core 0. */
hal_status_t ecuPersistenceInit(void);

/**
 * @brief Serialize one core-0 storage operation.
 *
 * All ECU EEPROM/KV writes must remain on core 0. The HAL flash provider
 * coordinates interrupts, the other core and DMA around physical writes.
 * @return The status of the storage operation, including a failed write
 * preparation; HAL_EINVAL without @p operation.
 */
hal_status_t ecuPersistenceExecute(ecu_persistence_operation_fn operation,
                                   const void *user);

/**
 * @brief Erase the spare key-value bank ahead of the next compaction.
 *
 * Key-value writes only program the active bank; erasing stops both cores
 * for tens of milliseconds per flash sector. Call this only where such a pause
 * is harmless: at start-up, before core 1 runs the pump. Serialized like
 * every other storage operation.
 * @return HAL_OK when the spare bank is erased (at once when it already was),
 * HAL_EUNINIT before the key-value store is initialized, or the failure of
 * the erase.
 */
hal_status_t ecuPersistencePrepareStorage(void);

#ifdef __cplusplus
}
#endif

#endif

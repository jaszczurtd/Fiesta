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
 * @brief Run one core-0 storage operation with GPS transport quiesced.
 *
 * Calls are serialized. The operation runs only after GPS is paused, and GPS
 * resume is attempted before the mutex is released. The return value reports
 * the pause or storage result. A failed resume is retained for automatic retry
 * by ecuPersistencePoll() and reported through @p outResumeStatus when
 * provided.
 */
hal_status_t ecuPersistenceExecute(ecu_persistence_operation_fn operation,
                                   const void *user,
                                   hal_status_t *outResumeStatus);

/**
 * @brief Retry a GPS resume left pending by a completed storage operation.
 *
 * Call from the core-0 service loop. Attempts are rate-limited internally.
 */
void ecuPersistencePoll(void);

#ifdef __cplusplus
}
#endif

#endif

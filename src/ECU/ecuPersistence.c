#include "ecuPersistence.h"

#include <hal/gps/hal_gps.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_sync.h>
#include <hal/system/hal_system.h>

#define ECU_PERSISTENCE_RESUME_RETRY_MS 1000u

static hal_mutex_t s_persistenceMutex = NULL;
static bool s_resumePending = false;
static uint32_t s_lastResumeAttemptMs = 0u;

static hal_status_t ensurePersistenceMutex(void) {
  if (s_persistenceMutex == NULL) {
    s_persistenceMutex = hal_mutex_create();
  }
  return s_persistenceMutex != NULL ? HAL_OK : HAL_ENOMEM;
}

hal_status_t ecuPersistenceInit(void) { return ensurePersistenceMutex(); }

hal_status_t ecuPersistenceExecute(ecu_persistence_operation_fn operation,
                                   const void *user,
                                   hal_status_t *outResumeStatus) {
  if (outResumeStatus != NULL) {
    *outResumeStatus = HAL_NONE;
  }
  if (operation == NULL) {
    return HAL_EINVAL;
  }

  const hal_status_t mutexStatus = ensurePersistenceMutex();
  if (mutexStatus != HAL_OK) {
    return mutexStatus;
  }

  hal_mutex_lock(s_persistenceMutex);
  const hal_status_t pauseStatus = hal_gps_pause();
  if (pauseStatus != HAL_OK) {
    hal_mutex_unlock(s_persistenceMutex);
    return pauseStatus;
  }

  const hal_status_t operationStatus = operation(user);
  const hal_status_t resumeStatus = hal_gps_resume();
  s_lastResumeAttemptMs = hal_millis();
  s_resumePending = resumeStatus != HAL_OK;
  if (outResumeStatus != NULL) {
    *outResumeStatus = resumeStatus;
  }
  if (resumeStatus != HAL_OK) {
    hal_derr("ECU persistence GPS resume failed: %s",
             hal_status_to_string(resumeStatus));
  }
  hal_mutex_unlock(s_persistenceMutex);
  return operationStatus;
}

void ecuPersistencePoll(void) {
  if (ensurePersistenceMutex() != HAL_OK) {
    return;
  }

  hal_mutex_lock(s_persistenceMutex);
  const uint32_t now = hal_millis();
  if (s_resumePending && (uint32_t)(now - s_lastResumeAttemptMs) >=
                             ECU_PERSISTENCE_RESUME_RETRY_MS) {
    s_lastResumeAttemptMs = now;
    const hal_status_t resumeStatus = hal_gps_resume();
    s_resumePending = resumeStatus != HAL_OK;
    if (resumeStatus != HAL_OK) {
      hal_derr("ECU persistence GPS resume retry failed: %s",
               hal_status_to_string(resumeStatus));
    }
  }
  hal_mutex_unlock(s_persistenceMutex);
}

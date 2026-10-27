#include "ecuPersistence.h"

#include <hal/core/hal_mutex_once.h>
#include <hal/storage/hal_kv.h>
#include <hal/system/hal_sync.h>

static hal_mutex_t s_persistenceMutex = NULL;

hal_status_t ecuPersistenceInit(void) {
  return jh_hal_mutex_create_once(&s_persistenceMutex) != NULL ? HAL_OK
                                                               : HAL_ENOMEM;
}

hal_status_t ecuPersistenceExecute(ecu_persistence_operation_fn operation,
                                   const void *user) {
  if (operation == NULL) {
    return HAL_EINVAL;
  }

  const hal_status_t initStatus = ecuPersistenceInit();
  if (initStatus != HAL_OK) {
    return initStatus;
  }

  hal_mutex_lock(s_persistenceMutex);
  const hal_status_t operationStatus = operation(user);
  hal_mutex_unlock(s_persistenceMutex);
  return operationStatus;
}

static hal_status_t prepareSpareBank(const void *user) {
  (void)user;
  return hal_kv_prepare_ex();
}

hal_status_t ecuPersistencePrepareStorage(void) {
  return ecuPersistenceExecute(prepareSpareBank, NULL);
}

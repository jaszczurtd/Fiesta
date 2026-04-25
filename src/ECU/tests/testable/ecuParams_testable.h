#ifndef ECU_TESTABLE_ECU_PARAMS_H
#define ECU_TESTABLE_ECU_PARAMS_H

#include "config.h"
#include <stdbool.h>

#ifdef UNIT_TEST
#ifdef __cplusplus
extern "C" {
#endif

void ecuParamsLoadDefaults(ecu_params_values_t *outValues);
bool ecuParamsValidate(const ecu_params_values_t *candidate, const char **reason);
bool ecuParamsStage(const ecu_params_values_t *candidate, const char **reason);
void ecuParamsApply(void);
bool ecuParamsLoadPersisted(ecu_params_values_t *outValues);
bool ecuParamsPersist(const ecu_params_values_t *values);
uint16_t ecuParamsBlobKeyForTest(void);
void ecuParamsResetRuntimeStateForTest(void);

#ifdef __cplusplus
}
#endif
#endif

#endif

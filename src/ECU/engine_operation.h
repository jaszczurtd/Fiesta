#ifndef T_ENGINE_OPERATION
#define T_ENGINE_OPERATION

#include <JaszczurHAL.h>

#include "config.h"
#include "rpm.h"
#include "sensors.h"
#include "vp37.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ENGINE_OP_STATE_STOPPED = 0,
  ENGINE_OP_STATE_CRANKING,
  ENGINE_OP_STATE_IDLE,
  ENGINE_OP_STATE_DRIVER
} engine_operation_state_t;

typedef struct {
  engine_operation_state_t state;
  float lastDemand;
  float idleIntegrator;
  float startDemand;
  float afterStartBaseAdd;
  bool afterstartActive;
  bool lastEngineRunning;
  uint32_t afterStartStartMs;
  uint32_t afterStartDurationMs;
  uint32_t lastIdleUpdateMs;
  uint32_t lastRampUpdateMs;
  uint32_t lastStartRampMs;
} engineOperation;

/**
 * @brief Initialize engine-operation state for idle and start control.
 */
void engineOperation_init(engineOperation *self);

/**
 * @brief Compute and apply the current VP37 demand for start/idle behavior.
 */
void engineOperation_process(engineOperation *self);

/**
 * @brief Print engine-operation state for diagnostics.
 */
void engineOperation_showDebug(const engineOperation *self);

/**
 * @brief Get the shared engine-operation instance from ECU context.
 * @return Pointer to the global engine-operation instance.
 */
engineOperation *getEngineOperationInstance(void);

/**
 * @brief Create and initialize the shared engine-operation instance.
 */
void createEngineOperation(void);

#ifdef __cplusplus
}
#endif

#endif

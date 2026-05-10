#include "engine_operation.h"
#include "ecuContext.h"
#include <hal/hal_soft_timer.h>

#define ENGINE_OP_DEMAND_INVALID (-1.0f)
#define ENGINE_OP_DEBUG_UPDATE_MS 500

//-----------------------------------------------------------------------------//
// Internal helpers
//-----------------------------------------------------------------------------//

static int32_t engineOperation_getIdleTargetRpm(void) {
  int32_t target = ecuParamsNominalRpm();
  int32_t coolant = (int32_t)getGlobalValue(F_COOLANT_TEMP);

  if(coolant > TEMP_LOWEST && coolant <= TEMP_COLD_ENGINE) {
    target = COLD_RPM_VALUE;
  }
  if(isDPFRegenerating()) {
    target = REGEN_RPM_VALUE;
  }
  return target;
}

static float engineOperation_computeAfterstartScale(int32_t coolant) {
  if(coolant <= TEMP_LOWEST) {
    return 0.0f;
  }
  if(coolant <= ENGINE_OP_AFTERSTART_TEMP_LOW) {
    return 1.0f;
  }
  if(coolant >= ENGINE_OP_AFTERSTART_TEMP_HIGH) {
    return 0.0f;
  }

  return (float)(ENGINE_OP_AFTERSTART_TEMP_HIGH - coolant) /
         (float)(ENGINE_OP_AFTERSTART_TEMP_HIGH - ENGINE_OP_AFTERSTART_TEMP_LOW);
}

static void engineOperation_startAfterstart(engineOperation *self, uint32_t nowMs) {
  int32_t coolant = (int32_t)getGlobalValue(F_COOLANT_TEMP);
  float scale = engineOperation_computeAfterstartScale(coolant);
  float addRange = ENGINE_OP_AFTERSTART_ADD_MAX - ENGINE_OP_AFTERSTART_ADD_MIN;
  float durationRange = (float)(ENGINE_OP_AFTERSTART_DURATION_COLD_MS -
                                ENGINE_OP_AFTERSTART_DURATION_WARM_MS);

  self->afterStartBaseAdd = ENGINE_OP_AFTERSTART_ADD_MIN + (addRange * scale);
  self->afterStartDurationMs = (uint32_t)((float)ENGINE_OP_AFTERSTART_DURATION_WARM_MS +
                                          (durationRange * scale));
  self->afterStartStartMs = nowMs;
  self->afterstartActive = (self->afterStartDurationMs > 0u) &&
                           (self->afterStartBaseAdd > 0.0f);
}

static void engineOperation_updateAfterstartState(engineOperation *self,
                                                  bool engineRunning,
                                                  uint32_t nowMs) {
  if(engineRunning && !self->lastEngineRunning) {
    engineOperation_startAfterstart(self, nowMs);
  }
  if(!engineRunning) {
    self->afterstartActive = false;
    self->afterStartDurationMs = 0u;
  }
  self->lastEngineRunning = engineRunning;
}

static float engineOperation_getAfterstartAdder(engineOperation *self, uint32_t nowMs) {
  if(!self->afterstartActive || self->afterStartDurationMs == 0u) {
    return 0.0f;
  }

  uint32_t elapsed = nowMs - self->afterStartStartMs;
  if(elapsed >= self->afterStartDurationMs) {
    self->afterstartActive = false;
    return 0.0f;
  }

  float taper = 1.0f - ((float)elapsed / (float)self->afterStartDurationMs);
  if(taper < 0.0f) {
    taper = 0.0f;
  }
  return self->afterStartBaseAdd * taper;
}

static void engineOperation_reset(engineOperation *self, uint32_t nowMs) {
  if(self == NULL) {
    return;
  }
  self->state = ENGINE_OP_STATE_STOPPED;
  self->lastDemand = ENGINE_OP_DEMAND_INVALID;
  self->idleIntegrator = 0.0f;
  self->startDemand = ENGINE_OP_START_DEMAND_MIN;
  self->afterStartBaseAdd = 0.0f;
  self->afterstartActive = false;
  self->lastEngineRunning = false;
  self->afterStartStartMs = nowMs;
  self->afterStartDurationMs = 0u;
  self->lastIdleUpdateMs = nowMs;
  self->lastRampUpdateMs = nowMs;
  self->lastStartRampMs = nowMs;
}

static float engineOperation_updateStartDemand(engineOperation *self, uint32_t nowMs) {
  if(self->startDemand < (float)ENGINE_OP_START_DEMAND_MIN) {
    self->startDemand = ENGINE_OP_START_DEMAND_MIN;
  }

  uint32_t elapsed = nowMs - self->lastStartRampMs;
  if(elapsed >= ENGINE_OP_START_RAMP_INTERVAL_MS) {
    float steps = (float)elapsed / (float)ENGINE_OP_START_RAMP_INTERVAL_MS;
    self->startDemand += ENGINE_OP_START_DEMAND_STEP * steps;
    if(self->startDemand > (float)ENGINE_OP_START_DEMAND_MAX) {
      self->startDemand = ENGINE_OP_START_DEMAND_MAX;
    }
    self->lastStartRampMs = nowMs;
  }

  return self->startDemand;
}

static float engineOperation_updateIdleDemand(engineOperation *self, int32_t rpm,
                                              int32_t targetRpm, uint32_t nowMs) {
  uint32_t elapsed = nowMs - self->lastIdleUpdateMs;
  if(elapsed >= ENGINE_OP_IDLE_UPDATE_INTERVAL_MS) {
    float error = (float)(targetRpm - rpm);
    self->idleIntegrator += (ENGINE_OP_IDLE_I_GAIN * error);
    self->idleIntegrator = hal_constrain(self->idleIntegrator,
                                         -ENGINE_OP_IDLE_I_LIMIT,
                                         ENGINE_OP_IDLE_I_LIMIT);
    self->lastIdleUpdateMs = nowMs;
  }

  float error = (float)(targetRpm - rpm);
  float demand = (float)ENGINE_OP_IDLE_BASE_DEMAND_PERCENT +
                 (ENGINE_OP_IDLE_P_GAIN * error) +
                 self->idleIntegrator;

  return hal_constrain(demand,
                       (float)ENGINE_OP_IDLE_DEMAND_MIN,
                       (float)ENGINE_OP_IDLE_DEMAND_MAX);
}

static float engineOperation_applyRampDown(engineOperation *self, float requested,
                                           uint32_t nowMs) {
  if(self->lastDemand == ENGINE_OP_DEMAND_INVALID) {
    self->lastDemand = requested;
    self->lastRampUpdateMs = nowMs;
    return requested;
  }

  if(requested >= self->lastDemand) {
    self->lastDemand = requested;
    self->lastRampUpdateMs = nowMs;
    return requested;
  }

  uint32_t elapsed = nowMs - self->lastRampUpdateMs;
  if(elapsed >= VP37_THROTTLE_RAMP_DOWN_INTERVAL_MS) {
    float steps = (float)elapsed / (float)VP37_THROTTLE_RAMP_DOWN_INTERVAL_MS;
    self->lastDemand -= VP37_THROTTLE_RAMP_DOWN_STEP * steps;
    if(self->lastDemand < requested) {
      self->lastDemand = requested;
    }
    self->lastRampUpdateMs = nowMs;
  }

  return self->lastDemand;
}

//-----------------------------------------------------------------------------//
// Public API
//-----------------------------------------------------------------------------//

void createEngineOperation(void) {
  engineOperation_init(getEngineOperationInstance());
}

engineOperation *getEngineOperationInstance(void) {
  return &getECUContext()->engineOp;
}

void engineOperation_init(engineOperation *self) {
  if(self == NULL) {
    return;
  }
  engineOperation_reset(self, hal_millis());
}

void engineOperation_process(engineOperation *self) {
  if(self == NULL) {
    return;
  }

  VP37Pump *pump = &getECUContext()->injectionPump;
  if(!pump->vp37Initialized || !pump->calibrationDone) {
    engineOperation_reset(self, hal_millis());
    return;
  }

  uint32_t nowMs = hal_millis();
  float driverDemand = (float)getThrottlePercentage();
  int32_t rpm = RPM_getCurrentRPM(getRPMInstance());
  bool engineRunning = (rpm >= RPM_MIN);
  bool engineCranking = (!engineRunning && (rpm > ENGINE_OP_CRANKING_RPM_MIN));
  bool throttleClosed = (driverDemand <= ACCELERATE_MIN_PERCENTAGE_THROTTLE_VALUE);
  float requested = 0.0f;

  engineOperation_updateAfterstartState(self, engineRunning, nowMs);

  if(!engineRunning && !engineCranking) {
    if(self->state != ENGINE_OP_STATE_STOPPED) {
      engineOperation_reset(self, nowMs);
    }
    self->state = ENGINE_OP_STATE_STOPPED;
    requested = ENGINE_OP_START_DEMAND_MIN;
  } else if(engineCranking) {
    self->state = ENGINE_OP_STATE_CRANKING;
    self->idleIntegrator = 0.0f;

    if(throttleClosed) {
      requested = engineOperation_updateStartDemand(self, nowMs);
    } else {
      requested = driverDemand;
      self->startDemand = ENGINE_OP_START_DEMAND_MIN;
      self->lastStartRampMs = nowMs;
    }
  } else if(throttleClosed) {
    self->state = ENGINE_OP_STATE_IDLE;
    self->startDemand = ENGINE_OP_START_DEMAND_MIN;
    requested = engineOperation_updateIdleDemand(self, rpm,
                                                 engineOperation_getIdleTargetRpm(),
                                                 nowMs);
  } else {
    self->state = ENGINE_OP_STATE_DRIVER;
    self->idleIntegrator = 0.0f;
    self->startDemand = ENGINE_OP_START_DEMAND_MIN;
    self->lastIdleUpdateMs = nowMs;
    requested = driverDemand;
  }

  if(throttleClosed) {
    requested += engineOperation_getAfterstartAdder(self, nowMs);
  }

  float demand = engineOperation_applyRampDown(self, requested, nowMs);
  demand = hal_constrain(demand,
                         (float)VP37_PERCENT_MIN,
                         (float)VP37_PERCENT_MAX);

  VP37_setVP37Throttle(pump, demand);
}

void engineOperation_showDebug(const engineOperation *self) {
  static uint32_t lastPeriodicLogMs = 0;
  if(self == NULL) {
    return;
  }

  uint32_t now = hal_millis();
  if (now - lastPeriodicLogMs >= ENGINE_OP_DEBUG_UPDATE_MS) {
    lastPeriodicLogMs = now;
    int32_t rpm = RPM_getCurrentRPM(getRPMInstance());
    bool crankingDetected = (rpm > ENGINE_OP_CRANKING_RPM_MIN) && (rpm < RPM_MIN);
    deb("engineOp state:%d crank:%d ase:%d rpm:%d demand:%.1f", self->state,
        crankingDetected, self->afterstartActive, rpm, self->lastDemand);
  }
}

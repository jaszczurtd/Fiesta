#include "engineFan.h"
#include "ecuContext.h"

//-----------------------------------------------------------------------------
// fan
//-----------------------------------------------------------------------------

void createFan(void) {
  engineFan_init(getFanInstance());
}

engineFan *getFanInstance(void) {
  return &getECUContext()->fan;
}

/**
 * @brief Return the bitmask that explains why the fan is enabled.
 * @param self Fan controller instance to inspect.
 * @return Reason bitmask stored in the controller state.
 */
static int engineFan_fanEnabledReason(const engineFan *self) {
  return self->fanEnabled;
}

void engineFan_init(engineFan *self) {
  self->fanEnabled = self->lastFanStatus = FAN_REASON_NONE;
}

void engineFan_fan(engineFan *self, bool enable) {
  (void)self;
  pcf8574_write(PCF8574_O_FAN, enable);
}

bool engineFan_isFanEnabled(const engineFan *self) {
  return self->fanEnabled != FAN_REASON_NONE;
}

void engineFan_process(engineFan *self) {

  float coolant = getGlobalValue(F_COOLANT_TEMP);
  int32_t rpm = (int32_t)getGlobalValue(F_RPM);
  int32_t air = (int32_t)getGlobalValue(F_INTAKE_TEMP);
  const int16_t fanAirStop = ecuParamsFanAirStop();
  const int16_t fanAirStart = ecuParamsFanAirStart();
  const int16_t fanCoolantStop = ecuParamsFanCoolantStop();
  const int16_t fanCoolantStart = ecuParamsFanCoolantStart();

  if(rpm > RPM_MIN) {

    //works only if the temp. sensor is plugged
    if(coolant > TEMP_LOWEST) {

      if(engineFan_isFanEnabled(self)) {
        if(self->fanEnabled & FAN_REASON_AIR) {
          if(air <= fanAirStop) {
            self->fanEnabled &= ~FAN_REASON_AIR;
          }
        }

        if(self->fanEnabled & FAN_REASON_COOLANT) {
          if(coolant <= fanCoolantStop) {
            self->fanEnabled &= ~FAN_REASON_COOLANT;
          }
        }
      } else {
        if(!(self->fanEnabled & FAN_REASON_AIR)) {
          if(air > fanAirStart) {
            self->fanEnabled |= FAN_REASON_AIR;
          }
        }

        if(!(self->fanEnabled & FAN_REASON_COOLANT)) {
          if(coolant > fanCoolantStart) {
            self->fanEnabled |= FAN_REASON_COOLANT;
          }
        }
      }
    } else {
      //temp sensor read fail, fan enabled by default
      self->fanEnabled |= FAN_REASON_COOLANT;
    }
  } else {
    self->fanEnabled = FAN_REASON_NONE;
  }

  setGlobalValue(F_FAN_ENABLED, self->fanEnabled);
  if(self->lastFanStatus != self->fanEnabled) {
    engineFan_fan(self, engineFan_isFanEnabled(self));
    self->lastFanStatus = self->fanEnabled;

    engineFan_showDebug(self);
  }

}

void engineFan_showDebug(const engineFan *self) {
  deb("fan enabled: %d reason: %d", engineFan_isFanEnabled(self), engineFan_fanEnabledReason(self));
}

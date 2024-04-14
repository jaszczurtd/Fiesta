#include "canDefinitions.h"
#include "engineFan.h"

//-----------------------------------------------------------------------------
// fan
//-----------------------------------------------------------------------------

void fan(bool enable) {
  pcf8574_write(PCF8574_O_FAN, enable);
}

static int fanEnabled = FAN_REASON_NONE;
static int lastFanStatus = FAN_REASON_NONE;

int fanEnabledReason(void) {
  return fanEnabled;
}
bool isFanEnabled(void) {
  return fanEnabled != FAN_REASON_NONE;  
}

void fanMainLoop(void) {

  float coolant = valueFields[F_COOLANT_TEMP];
  int rpm = valueFields[F_RPM];
  int air = valueFields[F_INTAKE_TEMP];

  if(rpm > RPM_MIN) {

    //works only if the temp. sensor is plugged
    if(coolant > TEMP_LOWEST) {

      if(isFanEnabled()) {
        if(fanEnabled & FAN_REASON_AIR) {
          if(air <= AIR_TEMP_FAN_STOP) {
            fanEnabled &= ~FAN_REASON_AIR;
          }
        }

        if(fanEnabled & FAN_REASON_COOLANT) {
          if(coolant <= TEMP_FAN_STOP) {
            fanEnabled &= ~FAN_REASON_COOLANT;
          }
        }
      } else {
        if((fanEnabled & FAN_REASON_AIR) == false) {
          if(air > AIR_TEMP_FAN_START) {
            fanEnabled |= FAN_REASON_AIR;
          }
        }

        if((fanEnabled & FAN_REASON_COOLANT) == false) {
          if(coolant > TEMP_FAN_START) {
            fanEnabled |= FAN_REASON_COOLANT;
          }
        }
      }
    } else {
      //temp sensor read fail, fan enabled by default
      //but only if engine has minimum RPM
      if(rpm > RPM_MIN) {
        fanEnabled |= FAN_REASON_COOLANT;
      }
    }
  } else {
    fanEnabled = FAN_REASON_NONE;    
  }

  valueFields[F_FAN_ENABLED] = fanEnabled;
  if(lastFanStatus != fanEnabled) {
    fan(isFanEnabled());
    lastFanStatus = fanEnabled;     

    deb("fan enabled: %d reason: %d", isFanEnabled(), fanEnabled);     
  }

}


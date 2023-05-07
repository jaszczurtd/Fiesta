#include "engineFan.h"

//-----------------------------------------------------------------------------
// fan
//-----------------------------------------------------------------------------

void fan(bool enable) {
  pcf8574_write(O_FAN, enable);
}

static bool fanEnabled = false;
static bool lastFanStatus = false;

bool isFanEnabled(void) {
  return fanEnabled;  
}

void fanMainLoop(void) {

  float coolant = valueFields[F_COOLANT_TEMP];
  int rpm = valueFields[F_RPM];

  //works only if the temp. sensor is plugged
  if(coolant > TEMP_LOWEST) {

    if(fanEnabled && coolant <= TEMP_FAN_STOP) {
      fanEnabled = false;
    }

    if(!fanEnabled && coolant >= TEMP_FAN_START) {
      fanEnabled = true;
    }

    if(rpm < RPM_MIN) {
      fanEnabled = false;
    }

  } else {
    //temp sensor read fail, fan enabled by default
    //but only if engine works
    if(rpm > RPM_MIN) {
      fanEnabled = true;
    }
  }

  if(lastFanStatus != fanEnabled) {
    fan(fanEnabled);
    lastFanStatus = fanEnabled;     

    deb("fan enabled: %d", fanEnabled);     
  }

}


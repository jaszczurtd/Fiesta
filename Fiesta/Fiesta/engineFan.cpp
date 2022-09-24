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
  //works only if the temp. sensor is plugged
  if(coolant > TEMP_LOWEST) {

    if(fanEnabled && coolant <= TEMP_FAN_STOP) {
      fanEnabled = false;
    }

    if(!fanEnabled && coolant >= TEMP_FAN_START) {
      fanEnabled = true;
    }

  } else {
    //temp sensor read fail, fan enabled by default
    //but only if engine works
    if(valueFields[F_RPM] > RPM_MIN) {
      fanEnabled = true;
    }
  }

  if(lastFanStatus != fanEnabled) {
    fan(fanEnabled);
    lastFanStatus = fanEnabled;     

    deb("fan enabled: %d", fanEnabled);     
  }

}



#include "vp37.h"


void enableVP37(bool enable) {
  pcf8574_write(PCF8574_O_VP37_ENABLE, enable);
  //delay(1);
  deb("vp37 enabled: %d", isVP37Enabled()); 
}

bool isVP37Enabled(void) {
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

void vp37Process(void) {

  int rpm = int(valueFields[F_RPM]);
  if(rpm > RPM_MAX_EVER) {
    enableVP37(false);
    derr("RPM was too high! (%d)", rpm);
    return;
  }

  setAccelRPMPercentage(getEnginePercentageThrottle());
  int cRPM = getCurrentRPMSolenoid();
  valToPWM(PIO_VP37_RPM, cRPM);
  valToPWM(PIO_VP37_ANGLE, cRPM);

}
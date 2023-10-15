
#include "vp37.h"


void enableVP37(bool enable) {
  pcf8574_write(PCF8574_O_VP37_ENABLE, enable);
  deb("vp37 enabled: %d", isVP37Enabled()); 
}

bool isVP37Enabled(void) {
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

int percentToPWMVal(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  return VP37_PWM_MIN + (((VP37_PWM_MAX - VP37_PWM_MIN) * percent) / 100);
}

void vp37Process(void) {

  int rpm = int(valueFields[F_RPM]);
  if(rpm > RPM_MAX_EVER) {
    enableVP37(false);
    derr("RPM was too high! (%d)", rpm);
    return;
  }

  int thr = getThrottlePercentage();
  int pwm = percentToPWMVal(thr);

  valToPWM(PIO_VP37_RPM, pwm);
  valToPWM(PIO_VP37_ANGLE, pwm);

}
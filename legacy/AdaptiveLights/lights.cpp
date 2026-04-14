
#include "lights.h"

static bool error = false;
static bool initialized = false;
Timer generalTimer;

void setup_a(void) {
  error = setupPeripherials();
  generalTimer = timer_create_default();
  generalTimer.every(SECOND / 2, callAtHalfSecond);
  generalTimer.every(CAN_CHECK_CONNECTION, canCheckConnection);  
  initialized = true;
}

#define PWM_10V 180
#define PWM_16V 450

static float light;
static float voltage;
static int adcValuePot;

int pwmValueFinal;

void loop_a(void) {
  if(!initialized) {
    return;
  }
  generalTimer.tick();

  light = getLumens();
  if(valueFields[F_OUTSIDE_LUMENS] != light) {
    valueFields[F_OUTSIDE_LUMENS] = light;
  }

  voltage = getSystemVoltage();
  adcValuePot = getValuePot();

  int cor = mapfloat(voltage, 9.0, 16.0, 0.0, 100.0);
  int pwmValue = adcValuePot + cor;
  
  pwmValueFinal = map(pwmValue, 0, (1 << ADC_BITS), 0, (1 << ANALOG_WRITE_RESOLUTION));

  int max = 18000;
  if(light > max) {
    light = max;
  }
  //pwmValueFinal = mapfloat(light, max, 1000, pwmValueFinal, 30);

  analogWrite(PIN_LAMP, pwmValueFinal);
  delay(10);
}

void loop_b(void) {
  deb("Light:%.1f lx voltage:%.1f pot:%d PWM:%d ECU_ON:%d", 
    light, voltage, adcValuePot, pwmValueFinal, isEcuConnected());
  delay(5);
}
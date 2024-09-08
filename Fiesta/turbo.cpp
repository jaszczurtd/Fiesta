#include "turbo.h"

Turbo::Turbo() { }

int Turbo::scaleTurboValues(int value, bool reversed) {
  if(value < PWM_MIN_PERCENT) {
    value = PWM_MIN_PERCENT;
  }
  if(value > PWM_MAX_PERCENT) {
    value = PWM_MAX_PERCENT;
  }
  if(reversed) {
    return map(value, PWM_MIN_PERCENT, PWM_MAX_PERCENT, TURBO_ACTUATOR_HIGH, TURBO_ACTUATOR_LOW);
  }
  return map(value, PWM_MIN_PERCENT, PWM_MAX_PERCENT, TURBO_ACTUATOR_LOW, TURBO_ACTUATOR_HIGH);
}

int Turbo::correctPressureFactor(void) {
  int temperature = valueFields[F_INTAKE_TEMP];
  return (temperature < MIN_TEMPERATURE_CORRECTION) ? 
      0 : ((temperature - MIN_TEMPERATURE_CORRECTION) / 5) + 1; //each 5 degrees
}

void Turbo::init() {
  desiredPWM = engineThrottlePercentageValue = posThrottle = pressurePercentage = RPM_index = 0;
  pedalPressed = false;
  lastSolenoidUpdate = 0;
}

void Turbo::process() {
  engineThrottlePercentageValue = getThrottlePercentage();
  posThrottle = (engineThrottlePercentageValue / 10);
  pedalPressed = false;
  pressurePercentage = 0;

#ifdef JUST_TEST_BY_THROTTLE
  engineThrottlePercentageValue = scaleTurboValues(engineThrottlePercentageValue);
  desiredPWM = percentToGivenVal(engineThrottlePercentageValue, PWM_RESOLUTION);
#else
  if(valueFields[F_PRESSURE] < MAX_BOOST_PRESSURE) {
    if(engineThrottlePercentageValue > 0) {
      pedalPressed = true;
    }

    int rpm = int(valueFields[F_RPM]);
    if(rpm > RPM_MAX_EVER) {
      rpm = RPM_MAX_EVER;
    }

    RPM_index = (int(rpm - 1500) / 500); // determine RPM index
    if(RPM_index < 0) {
      RPM_index = 0;
    }
    if(RPM_index > RPM_PRESCALERS - 1) {
      RPM_index = RPM_PRESCALERS - 1;    
    }

    pressurePercentage = RPM_table[0][0];

    for (int i = 0; i < N75_PERCENT_VALS; i++) {
      if (posThrottle == i + 1) {
        pressurePercentage = RPM_table[RPM_index][i];
        break;
      }
    }

    if (!pedalPressed) {
      pressurePercentage = RPM_table[0][0];
    }

    pressurePercentage -= correctPressureFactor();

  } else {

    unsigned long currentTime = millis();

    if ((long)(currentTime - lastSolenoidUpdate) >= SOLENOID_UPDATE_TIME) {
        if (valueFields[F_PRESSURE] > MAX_BOOST_PRESSURE) {
            pressurePercentage = max(pressurePercentage - PRESSURE_LIMITER_FACTOR, PWM_MIN_PERCENT); 
        } else if (valueFields[F_PRESSURE] < MAX_BOOST_PRESSURE) {
            pressurePercentage = min(pressurePercentage + PRESSURE_LIMITER_FACTOR, PWM_MAX_PERCENT); 
        }
        lastSolenoidUpdate = currentTime;
    }
  }

  pressurePercentage = constrain(pressurePercentage, PWM_MIN_PERCENT, PWM_MAX_PERCENT);

  valueFields[F_PRESSURE_PERCENTAGE] = pressurePercentage;

  desiredPWM = scaleTurboValues(pressurePercentage, false);
#endif

  valToPWM(PIO_TURBO, desiredPWM);
}

void Turbo::showDebug(void) {
  deb("r:%d throttle:%d pressed:%d rpm:%d pressure:%d n75:%d", 
    int(valueFields[F_THROTTLE_POS]), posThrottle, pedalPressed, RPM_index, pressurePercentage, desiredPWM);
}


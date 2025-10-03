#include "turbo.h"

#define TURBO_PID_TIME_UPDATE 6.0
#define TURBO_PID_KP 0.7
#define TURBO_PID_KI 0.1
#define TURBO_PID_KD 0.05

Turbo::Turbo() { }


int Turbo::scaleTurboValues(int value) {
#ifdef GTB2260VZK  
  value = map(value, 0, 100, 100, 0);
  value = map(value, 0, 100, TURBO_ACTUATOR_LOW, TURBO_ACTUATOR_HIGH);
#endif
  return value;
}

int Turbo::correctPressureFactor(void) {
  int temperature = valueFields[F_INTAKE_TEMP];
  return (temperature < MIN_TEMPERATURE_CORRECTION) ? 
      0 : ((temperature - MIN_TEMPERATURE_CORRECTION) / 5) + 1; //each 5 degrees
}

void Turbo::init() {

}

void Turbo::turboTest(void) {

}

void Turbo::process() {

  engineThrottlePercentageValue = getThrottlePercentage();
  posThrottle = (engineThrottlePercentageValue / 10);
  bool pedalPressed = false;
  bool pressurePercentage = 0;

#ifdef JUST_TEST_BY_THROTTLE
  engineThrottlePercentageValue = scaleTurboValues(engineThrottlePercentageValue);
  n75 = percentToGivenVal(engineThrottlePercentageValue, PWM_RESOLUTION);
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
    if (currentTime - lastSolenoidUpdate >= SOLENOID_UPDATE_TIME) {
      if (valueFields[F_PRESSURE] > MAX_BOOST_PRESSURE) {
        pressurePercentage -= PRESSURE_LIMITER_FACTOR;
        if (pressurePercentage < 0) {
          pressurePercentage = 0;
        }
      } else {
        pressurePercentage += PRESSURE_LIMITER_FACTOR;
        if (pressurePercentage > 100) {
          pressurePercentage = 100;
        }
      }
      lastSolenoidUpdate = currentTime;
    }
  }

  pressurePercentage = scaleTurboValues(pressurePercentage);
  pressurePercentage = constrain(pressurePercentage, 0, 100);

  valueFields[F_PRESSURE_PERCENTAGE] = pressurePercentage;

  n75 = percentToGivenVal(pressurePercentage, PWM_RESOLUTION);

#endif

  valToPWM(PIO_TURBO, n75);
}

void Turbo::showDebug(void) {
  bool pr = false;

  if(int(valueFields[F_THROTTLE_POS]) != lastThrottlePos){
    lastThrottlePos = int(valueFields[F_THROTTLE_POS]);
    pr = true;
  }
  if(posThrottle != lastPosThrottle) {
    lastPosThrottle = posThrottle;
    pr = true;
  }
  bool pp = getThrottlePercentage() > 0;
  if(pp != lastPedalPressed) {
    lastPedalPressed = pp;
    pr = true;
  }
  if(RPM_index != lastRPM_index) {
    lastRPM_index = RPM_index;
    pr = true;
  }
  if(int(valueFields[F_PRESSURE_PERCENTAGE]) != lastPressurePercentage) {
    lastPressurePercentage = int(valueFields[F_PRESSURE_PERCENTAGE]);
    pr = true;
  }
  if(n75 != lastN75) {
    lastN75 = n75;
    pr = true;
  }

  if(pr) {
    deb("r:%d throttle:%d pressed:%d rpm:%d pressure:%d n75:%d", 
      lastThrottlePos, lastPosThrottle, lastPedalPressed, lastRPM_index, 
      lastPressurePercentage, n75);
  }
}


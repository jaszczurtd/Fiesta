#include "turbo.h"

#define TURBO_PID_TIME_UPDATE 6.0
#define TURBO_PID_KP 0.7
#define TURBO_PID_KI 0.1
#define TURBO_PID_KD 0.05

static int32_t Turbo_scaleTurboValues(Turbo *self, int32_t value) {
  (void)self;
#ifdef GTB2260VZK
  value = hal_map(value, 0, 100, 100, 0);
  value = hal_map(value, 0, 100, TURBO_ACTUATOR_LOW, TURBO_ACTUATOR_HIGH);
#endif
  return value;
}

static int32_t Turbo_correctPressureFactor(Turbo *self) {
  (void)self;
  int32_t temperature = (int32_t)getGlobalValue(F_INTAKE_TEMP);
  return (temperature < MIN_TEMPERATURE_CORRECTION) ?
      0 : ((temperature - MIN_TEMPERATURE_CORRECTION) / 5) + 1; //each 5 degrees
}

void Turbo_init(Turbo *self) {
  (void)self;
}

void Turbo_turboTest(Turbo *self) {
  (void)self;
}

void Turbo_process(Turbo *self) {

  self->engineThrottlePercentageValue = getThrottlePercentage();
  self->posThrottle = (self->engineThrottlePercentageValue / 10);

#ifdef JUST_TEST_BY_THROTTLE
  self->engineThrottlePercentageValue = Turbo_scaleTurboValues(self, self->engineThrottlePercentageValue);
  self->n75 = percentToGivenVal(self->engineThrottlePercentageValue, PWM_RESOLUTION);
#else
  int32_t pressurePercentage = 0;

  if(getGlobalValue(F_PRESSURE) < MAX_BOOST_PRESSURE) {
    bool pedalPressed = false;
    
    if(self->engineThrottlePercentageValue > 0) {
      pedalPressed = true;
    }

    int32_t rpm = (int32_t)getGlobalValue(F_RPM);
    if(rpm > RPM_MAX_EVER) {
      rpm = RPM_MAX_EVER;
    }

    self->RPM_index = (rpm - 1500) / 500; // determine RPM index
    if(self->RPM_index < 0) {
      self->RPM_index = 0;
    }
    if(self->RPM_index > RPM_PRESCALERS - 1) {
      self->RPM_index = RPM_PRESCALERS - 1;
    }

    pressurePercentage = RPM_table[0][0];

    for (int i = 0; i < N75_PERCENT_VALS; i++) {
      if (self->posThrottle == i + 1) {
        pressurePercentage = RPM_table[self->RPM_index][i];
        break;
      }
    }

    if (!pedalPressed) {
      pressurePercentage = RPM_table[0][0];
    }

    pressurePercentage -= Turbo_correctPressureFactor(self);

  } else {

    unsigned long currentTime = hal_millis();
    if (currentTime - self->lastSolenoidUpdate >= SOLENOID_UPDATE_TIME) {
      if (getGlobalValue(F_PRESSURE) > MAX_BOOST_PRESSURE) {
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
      self->lastSolenoidUpdate = currentTime;
    }
  }

  pressurePercentage = Turbo_scaleTurboValues(self, pressurePercentage);
  pressurePercentage = hal_constrain(pressurePercentage, 0, 100);

  setGlobalValue(F_PRESSURE_PERCENTAGE, pressurePercentage);

  self->n75 = percentToGivenVal(pressurePercentage, PWM_RESOLUTION);

#endif

  valToPWM(PIO_TURBO, self->n75);
}

void Turbo_showDebug(Turbo *self) {
  bool pr = false;

  if((int32_t)getGlobalValue(F_THROTTLE_POS) != self->lastThrottlePos){
    self->lastThrottlePos = (int32_t)getGlobalValue(F_THROTTLE_POS);
    pr = true;
  }
  if(self->posThrottle != self->lastPosThrottle) {
    self->lastPosThrottle = self->posThrottle;
    pr = true;
  }
  bool pp = getThrottlePercentage() > 0;
  if(pp != self->lastPedalPressed) {
    self->lastPedalPressed = pp;
    pr = true;
  }
  if(self->RPM_index != self->lastRPM_index) {
    self->lastRPM_index = self->RPM_index;
    pr = true;
  }
  if((int32_t)getGlobalValue(F_PRESSURE_PERCENTAGE) != self->lastPressurePercentage) {
    self->lastPressurePercentage = (int32_t)getGlobalValue(F_PRESSURE_PERCENTAGE);
    pr = true;
  }
  if(self->n75 != self->lastN75) {
    self->lastN75 = self->n75;
    pr = true;
  }

  if(pr) {
    static uint32_t lastPeriodicLogMs = 0;

    uint32_t now = hal_millis();
    if (now - lastPeriodicLogMs >= TURBO_DEBUG_UPDATE) {
      lastPeriodicLogMs = now;

      deb("r:%d throttle:%d pressed:%d rpm:%d pressure:%d n75:%d",
       self->lastThrottlePos, self->lastPosThrottle, self->lastPedalPressed, self->lastRPM_index,
       self->lastPressurePercentage, self->n75);
    }
  }
}

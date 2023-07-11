#include "canDefinitions.h"
#include "turbo.h"

#define RPM_PRESCALERS 8
#define N75_PERCENT_VALS 10

//*** n75 percentage values in relation to RPM
uint8_t RPM_table[RPM_PRESCALERS][N75_PERCENT_VALS] = {
  { 92, 91, 90, 89, 88, 87, 85, 82, 80, 78 }, // 1500 RPM
  { 90, 89, 88, 87, 85, 83, 80, 77, 74, 71 }, // 2000 RPM
  { 88, 87, 86, 85, 83, 81, 78, 75, 71, 68 }, // 2500 RPM
  { 87, 86, 85, 83, 81, 79, 76, 73, 70, 67 }, // 3000 RPM
  { 86, 85, 83, 81, 79, 77, 74, 71, 68, 65 }, // 3500 RPM
  { 84, 83, 81, 79, 77, 75, 72, 69, 66, 63 }, // 4000 RPM
  { 80, 79, 77, 75, 73, 71, 68, 65, 62, 59 }, // 4500 RPM
  { 77, 75, 73, 71, 69, 67, 64, 61, 58, 55 }  // 5000 RPM
};

static unsigned long lastSolenoidUpdate = 0;

int correctPressureFactor(void) {
  int temperature = valueFields[F_INTAKE_TEMP];
  return (temperature < MIN_TEMPERATURE_CORRECTION) ? 
      0 : ((temperature - MIN_TEMPERATURE_CORRECTION) / 5) + 1;
}

void turboMainLoop(void) {

  int engineThrottleRAWValue = int(valueFields[F_THROTTLE_POS]);
  int engineThrottlePercentageValue = getThrottlePercentage(engineThrottleRAWValue);
  int posThrottle = (engineThrottlePercentageValue / 10);
  bool pedalPressed = false;
  int n75;
  int pressurePercentage;

  if(valueFields[F_PRESSURE] < MAX_BOOST_PRESSURE) {
    if(engineThrottlePercentageValue > 0) {
      pedalPressed = true;
    }

    int rpm = int(valueFields[F_RPM]);
    if(rpm > RPM_MAX_EVER) {
      rpm = RPM_MAX_EVER;
    }

    int RPM_index = (int(rpm - 1500) / 500); // determine RPM index
    if(RPM_index < 0) {
      RPM_index = 0;
    }
    if(RPM_index > RPM_PRESCALERS - 1) {
      RPM_index = RPM_PRESCALERS - 1;    
    }

    pressurePercentage = IDLE_BOOST_PERCENTAGE_SET;

    for (int i = 0; i < N75_PERCENT_VALS; i++) {
      if (posThrottle == i + 1) {
        pressurePercentage = RPM_table[RPM_index][i];
        break;
      }
    }

    if (!pedalPressed) {
      pressurePercentage = IDLE_BOOST_PERCENTAGE_SET;
    }

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

  pressurePercentage += correctPressureFactor();
  if(pressurePercentage > 100) {
    pressurePercentage = 100;
  }

  n75 = percentToGivenVal(pressurePercentage, PWM_RESOLUTION);

#ifdef DEBUG
  deb("r:%d throttle:%d pressed:%d rpm:%d pressure:%d n75:%d", engineLoadRAWValue, posThrottle, pedalPressed, RPM_index, pressurePercentage, n75);
#endif

  valToPWM(PIO_TURBO, n75);
}
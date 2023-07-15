#include "canDefinitions.h"
#include "turbo.h"

#define RPM_PRESCALERS 8
#define N75_PERCENT_VALS 10

//*** n75 percentage values in relation to RPM
uint8_t RPM_table[RPM_PRESCALERS][N75_PERCENT_VALS] = {
  { 77, 76, 75, 74, 73, 72, 70, 67, 65, 63 }, // 1500 RPM
  { 75, 74, 73, 72, 70, 68, 65, 62, 59, 56 }, // 2000 RPM
  { 73, 72, 71, 70, 68, 66, 63, 60, 56, 53 }, // 2500 RPM
  { 72, 71, 70, 68, 66, 64, 61, 58, 55, 52 }, // 3000 RPM
  { 71, 70, 68, 66, 64, 62, 59, 56, 53, 50 }, // 3500 RPM
  { 69, 68, 66, 64, 62, 60, 57, 54, 51, 48 }, // 4000 RPM
  { 65, 64, 62, 60, 58, 56, 53, 50, 47, 44 }, // 4500 RPM
  { 62, 60, 58, 56, 54, 52, 49, 46, 43, 40 }  // 5000 RPM
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

  n75 = percentToGivenVal(pressurePercentage, PWM_RESOLUTION);

#ifdef DEBUG
  deb("r:%d throttle:%d pressed:%d rpm:%d pressure:%d n75:%d", engineLoadRAWValue, posThrottle, pedalPressed, RPM_index, pressurePercentage, n75);
#endif

  valToPWM(PIO_TURBO, n75);
}
#include "turbo.h"

#define SOLENOID_UPDATE_TIME 500
#define PRESSURE_LIMITER_FACTOR 2

#define RPM_PRESCALERS 8
#define N75_PERCENT_VALS 10

//*** n75 percentage values in relation to RPM
uint8_t RPM_table[RPM_PRESCALERS][N75_PERCENT_VALS] = {
  { 82, 81, 80, 79, 78, 77, 75, 72, 70, 68 }, // 1500 RPM
  { 80, 79, 78, 77, 75, 73, 70, 67, 64, 61 }, // 2000 RPM
  { 78, 77, 76, 75, 73, 71, 68, 65, 61, 58 }, // 2500 RPM
  { 77, 76, 75, 73, 71, 69, 66, 63, 60, 57 }, // 3000 RPM
  { 76, 75, 73, 71, 69, 67, 64, 61, 58, 55 }, // 3500 RPM
  { 74, 73, 71, 69, 67, 65, 62, 59, 56, 53 }, // 4000 RPM
  { 70, 69, 67, 65, 63, 61, 58, 55, 52, 49 }, // 4500 RPM
  { 67, 65, 63, 61, 59, 57, 54, 51, 48, 45 }  // 5000 RPM
};

static unsigned long lastSolenoidUpdate = 0;

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

    int RPM_index = (int(valueFields[F_RPM] - 1500) / 500); // determine RPM index
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

  n75 = percentToGivenVal(pressurePercentage, PWM_RESOLUTION);

#ifdef DEBUG
  deb("r:%d throttle:%d pressed:%d rpm:%d pressure:%d n75:%d", engineLoadRAWValue, posThrottle, pedalPressed, RPM_index, pressurePercentage, n75);
#endif

  valToPWM(PIO_TURBO, n75);
}
#include "turbo.h"

#define RPM_PRESCALERS 8
#define N75_PERCENT_VALS 10

//*** n75 percentage values in relation to RPM
uint8_t RPM_table[RPM_PRESCALERS][N75_PERCENT_VALS] = {
  { 97, 96, 95, 94, 93, 92, 90, 87, 85, 83 }, // 1500 RPM
  { 95, 94, 93, 92, 90, 88, 85, 82, 79, 76 }, // 2000 RPM
  { 93, 92, 91, 90, 88, 86, 83, 80, 76, 73 }, // 2500 RPM
  { 92, 91, 90, 88, 86, 84, 81, 78, 75, 72 }, // 3000 RPM
  { 91, 90, 88, 86, 84, 82, 79, 76, 73, 70 }, // 3500 RPM
  { 89, 88, 86, 84, 82, 80, 77, 74, 71, 68 }, // 4000 RPM
  { 85, 84, 82, 80, 78, 76, 73, 70, 67, 64 }, // 4500 RPM
  { 82, 80, 78, 76, 74, 72, 69, 66, 63, 60 }  // 5000 RPM
};

void turboMainLoop(void) {

  int engineLoadRAWValue = int(valueFields[F_ENGINE_LOAD]);
  int engineLoadPercentageValue = getThrottlePercentage(engineLoadRAWValue);
  int posThrottle = (engineLoadPercentageValue / 10);
  bool pedalPressed = false;
  if(engineLoadPercentageValue > 0) {
    pedalPressed = true;
  }

  int RPM_index = (int(valueFields[F_RPM] - 1500) / 500); // determine RPM index
  if(RPM_index < 0) {
    RPM_index = 0;
  }
  if(RPM_index > RPM_PRESCALERS - 1) {
    RPM_index = RPM_PRESCALERS - 1;    
  }

  int pressurePercentage = IDLE_BOOST_PERCENTAGE_SET;

  for (int i = 0; i < N75_PERCENT_VALS; i++) {
    if (posThrottle == i + 1) {
      pressurePercentage = RPM_table[RPM_index][i];
      break;
    }
  }

  if (!pedalPressed) {
    pressurePercentage = IDLE_BOOST_PERCENTAGE_SET;
  }

  int n75 = percentToGivenVal(pressurePercentage, PWM_RESOLUTION);

  float currentPressure = valueFields[F_PRESSURE];
  if(currentPressure > MAX_BOOST_PRESSURE) {
    float pressureRatio = ((float)pressurePercentage / 100.0) * (MAX_BOOST_PRESSURE / currentPressure);
    if (pressureRatio > 1.0) {
      pressureRatio = 1.0;
    }
    n75 = int(pressureRatio * PWM_RESOLUTION);
  }

#ifdef DEBUG
  deb("r:%d throttle:%d pressed:%d rpm:%d pressure:%d n75:%d", engineLoadRAWValue, posThrottle, pedalPressed, RPM_index, pressurePercentage, n75);
#endif

  valToPWM(PIO_TURBO, n75);
}
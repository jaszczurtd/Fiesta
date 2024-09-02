#include "turbo.h"

#define TURBO_PID_TIME_UPDATE 6.0
#define TURBO_PID_KP 0.7
#define TURBO_PID_KI 0.1
#define TURBO_PID_KD 0.05

#define RPM_ROWS 9
#define TPS_COLUMNS 21  //TPS position in 5% increments from 0% to 100%
#define MIN_TPS 0    // 0%
#define MAX_TPS 100  // 100%

float boostMap[RPM_ROWS][THROTTLE_COLUMNS] = {
    {0.0, 0.0, 0.0, 0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85},   // 850 rpm
    {0.0, 0.0, 0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9},   // 1200 rpm
    {0.0, 0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95},  // 1500 rpm
    {0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1.0},   // 1800 rpm
    {0.0, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1.0, 1.05},   // 2200 rpm
    {0.0, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1.0, 1.05, 1.1},   // 2600 rpm
    {0.0, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.15},  // 3000 rpm
    {0.0, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.15, 1.2},   // 3500 rpm
    {0.0, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.15, 1.2, 1.25}    // 4000 rpm
};

static PIDController *turboController;
static bool turboInitialized = false;
static float minBoost, maxBoost;
static int lastTurboPWM = -1;

int getRPMIndex(int rpm) {
  int rpmIndex = (rpm - NOMINAL_RPM_VALUE) * (RPM_ROWS - 1) / (RPM_MAX_EVER - NOMINAL_RPM_VALUE);
  if (rpmIndex < 0) rpmIndex = 0;
  if (rpmIndex >= RPM_ROWS) rpmIndex = RPM_ROWS - 1;
  return rpmIndex;
}

int getTPSIndex(int tps) {
  int tpsIndex = tps * (TPS_COLUMNS - 1) / MAX_TPS;
  if (tpsIndex < 0) tpsIndex = 0;
  if (tpsIndex >= TPS_COLUMNS) tpsIndex = TPS_COLUMNS - 1;
  return tpsIndex;
}

float getBoostPressure(int rpm, int tps) {
  int rpmIndex = getRPMIndex(rpm);
  int tpsIndex = getTPSIndex(tps);
  return boostMap[rpmIndex][tpsIndex];
}

int scaleTurboValues(float value, bool reverse) {
  if(value < minBoost) {
    value = minBoost;
  }
  if(value > maxBoost) {
    value = maxBoost;
  }
  int pwmValue = mapfloat(value, minBoost, maxBoost, TURBO_ACTUATOR_LOW, TURBO_ACTUATOR_HIGH);
  if(reverse) {
    pwmValue = map(pwmValue, TURBO_ACTUATOR_LOW, TURBO_ACTUATOR_HIGH, TURBO_ACTUATOR_HIGH, TURBO_ACTUATOR_LOW);
  }
  return pwmValue;
}

void turboInit(void) {
  if(!turboInitialized) {
    minBoost = FLT_MAX; 
    maxBoost = FLT_MIN;

    for (int i = 0; i < RPM_ROWS; i++) {
      for (int j = 0; j < TPS_COLUMNS; j++) {
        if (boostMap[i][j] < minBoost) {
            minBoost = boostMap[i][j];
        }
        if (boostMap[i][j] > maxBoost) {
            maxBoost = boostMap[i][j];
        }
      }
    }
    deb("min turbo val: %f", minBoost);
    deb("max turbo val: %f", maxBoost);
    
    turboController = new PIDController(TURBO_PID_KP, TURBO_PID_KI, TURBO_PID_KD, maxBoost * 10);

    turboTest();

    turboInitialized = true;
  }
}

#define TEST_DURATION_MS 1000 
#define PWM_MIN_PERCENT 0
#define PWM_MAX_PERCENT 100 
#define STEP_PERCENT 5
#define UPDATE_INTERVAL_MS 50

void turboTest(void) {
  unsigned long startTime = millis();
  int currentPWMValue = TURBO_ACTUATOR_LOW;
  int pwmStep = (STEP_PERCENT * TURBO_ACTUATOR_HIGH) / 100;

  while (millis() - startTime < TEST_DURATION_MS) {
    valToPWM(PIO_TURBO, currentPWMValue);
    m_delay(UPDATE_INTERVAL_MS);

    currentPWMValue += pwmStep;
    if (currentPWMValue >= TURBO_ACTUATOR_HIGH || currentPWMValue <= TURBO_ACTUATOR_LOW) {
      pwmStep = -pwmStep; 
      currentPWMValue = constrain(currentPWMValue, TURBO_ACTUATOR_LOW, TURBO_ACTUATOR_HIGH);
    }
  }
}

void turboMainLoop(void) {
  if(!turboInitialized) {
    return;
  }

  float turbo;

  valueFields[F_PRESSURE_DESIRED] = getBoostPressure(int(valueFields[F_RPM]), getThrottlePercentage());
  turboController->updatePIDtime(TURBO_PID_TIME_UPDATE);
  turbo = constrain(turboController->updatePIDcontroller(
                    valueFields[F_PRESSURE_DESIRED] - valueFields[F_PRESSURE]), minBoost, maxBoost);

  valueFields[F_PRESSURE_PERCENTAGE] = ((turbo - minBoost) / (maxBoost - minBoost)) * 100.0;

  int pwm = scaleTurboValues(turbo, false);

#ifdef DEBUG
  deb("%f %f %f", valueFields[F_PRESSURE_DESIRED], valueFields[F_PRESSURE], turbo);
#endif

  if(lastTurboPWM != pwm) {
    valToPWM(PIO_TURBO, pwm);
    lastTurboPWM = pwm;
  }
}


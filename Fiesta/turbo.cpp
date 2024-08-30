#include "turbo.h"

#define TURBO_PID_TIME_UPDATE 30.0
#define TURBO_PID_KP 0.8
#define TURBO_PID_KI 0.1
#define TURBO_PID_KD 0.01

#define RPM_ROWS 9
#define TPS_COLUMNS 21  // tps position each 5% from 0% to 100%
#define MIN_TPS 0    // 0%
#define MAX_TPS 100  // 100%
#define MIN_RPM NOMINAL_RPM_VALUE
#define MAX_RPM RPM_MAX_EVER

static float boostMap[RPM_ROWS][TPS_COLUMNS] = {
    {0.10, 0.12, 0.14, 0.16, 0.18, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85, 0.90, 1.00},
    {0.20, 0.22, 0.24, 0.26, 0.28, 0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 1.00, 1.10},
    {0.30, 0.32, 0.34, 0.36, 0.38, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 1.00, 1.05, 1.10, 1.20},
    {0.40, 0.42, 0.44, 0.46, 0.48, 0.50, 0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 1.00, 1.05, 1.10, 1.15, 1.20, 1.30},
    {0.50, 0.52, 0.54, 0.56, 0.58, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 1.00, 1.05, 1.10, 1.15, 1.20, 1.25, 1.30, 1.40},
    {0.60, 0.62, 0.64, 0.66, 0.68, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 1.00, 1.05, 1.10, 1.15, 1.20, 1.25, 1.30, 1.35, 1.40, 1.50},
    {0.70, 0.72, 0.74, 0.76, 0.78, 0.80, 0.85, 0.90, 0.95, 1.00, 1.05, 1.10, 1.15, 1.20, 1.25, 1.30, 1.35, 1.40, 1.45, 1.50, 1.60},
    {0.80, 0.82, 0.84, 0.86, 0.88, 0.90, 0.95, 1.00, 1.05, 1.10, 1.15, 1.20, 1.25, 1.30, 1.35, 1.40, 1.45, 1.50, 1.55, 1.60, 1.70},
    {0.90, 0.92, 0.94, 0.96, 0.98, 1.00, 1.05, 1.10, 1.15, 1.20, 1.25, 1.30, 1.35, 1.40, 1.45, 1.50, 1.55, 1.60, 1.65, 1.70, 2.10}
};

static PIDController *turboController;
static bool turboInitialized = false;
static int turbo;
static float minBoost, maxBoost;

int getRPMIndex(int rpm) {
  int rpmIndex = (rpm - MIN_RPM) * (RPM_ROWS - 1) / (MAX_RPM - MIN_RPM);
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
    turboController = new PIDController(TURBO_PID_KP, TURBO_PID_KI, TURBO_PID_KD, FLT_MAX);

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
    
    turboTest();

    turboInitialized = true;
  }
}

#define TEST_DURATION_MS 1000 
#define PWM_MIN_PERCENT 0
#define PWM_MAX_PERCENT 100 
#define STEP_PERCENT 5
#define UPDATE_INTERVAL_MS 50

static unsigned long previousMillis = 0;
static unsigned long startTime = 0;
static int currentPWMValue = 0;
static int pwmDirection = 1;

void turboTest(void) {
  unsigned long startTime = millis();  
  int currentPWMValue = TURBO_ACTUATOR_LOW;
  int pwmDirection = 1;

  while (millis() - startTime < TEST_DURATION_MS) {
    valToPWM(PIO_TURBO, currentPWMValue);

    delay(UPDATE_INTERVAL_MS);

    currentPWMValue += (STEP_PERCENT * TURBO_ACTUATOR_HIGH / 100) * pwmDirection;

    if (currentPWMValue >= TURBO_ACTUATOR_HIGH || currentPWMValue <= TURBO_ACTUATOR_LOW) {
      pwmDirection *= -1;  
      currentPWMValue = constrain(currentPWMValue, TURBO_ACTUATOR_LOW, TURBO_ACTUATOR_HIGH); 
    }
  }  
}

void turboMainLoop(void) {
  if(!turboInitialized) {
    return;
  }

  float desiredPressure;
  float turbo;

  desiredPressure = getBoostPressure(int(valueFields[F_RPM]), getThrottlePercentage());
  turboController->updatePIDtime(TURBO_PID_TIME_UPDATE);
  turbo = turboController->updatePIDcontroller(desiredPressure - valueFields[F_PRESSURE]);

  valueFields[F_PRESSURE_PERCENTAGE] = ((constrain(turbo, minBoost, maxBoost) - minBoost) / (maxBoost - minBoost)) * 100.0;

  deb("%f %f %f", desiredPressure, valueFields[F_PRESSURE], turbo);

  valToPWM(PIO_TURBO, scaleTurboValues(turbo, false));
}


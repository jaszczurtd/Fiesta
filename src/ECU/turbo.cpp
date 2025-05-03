#include "turbo.h"

#define TURBO_PID_TIME_UPDATE 6.0
#define TURBO_PID_KP 0.7
#define TURBO_PID_KI 0.1
#define TURBO_PID_KD 0.05

Turbo::Turbo() { }

int Turbo::getRPMIndex(int rpm) {
  int rpmIndex = (rpm - NOMINAL_RPM_VALUE) * (RPM_ROWS - 1) / (RPM_MAX_EVER - NOMINAL_RPM_VALUE);
  if (rpmIndex < 0) rpmIndex = 0;
  if (rpmIndex >= RPM_ROWS) rpmIndex = RPM_ROWS - 1;
  return rpmIndex;
}

int Turbo::getTPSIndex(int tps) {
  int tpsIndex = tps * (TPS_COLUMNS - 1) / MAX_TPS;
  if (tpsIndex < 0) tpsIndex = 0;
  if (tpsIndex >= TPS_COLUMNS) tpsIndex = TPS_COLUMNS - 1;
  return tpsIndex;
}

float Turbo::getBoostPressure(int rpm, int tps) {
  int rpmIndex = getRPMIndex(rpm);
  int tpsIndex = getTPSIndex(tps);
  return boostMap[rpmIndex][tpsIndex];
}

int Turbo::scaleTurboValues(float value, bool reverse) {
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

void Turbo::init() {
  if (!turboInitialized) {
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

void Turbo::turboTest(void) {
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

void Turbo::process() {
  if(!turboInitialized) {
    return;
  }

  valueFields[F_PRESSURE_DESIRED] = getBoostPressure(int(valueFields[F_RPM]), getThrottlePercentage());
  turboController->updatePIDtime(TURBO_PID_TIME_UPDATE);
  valueDesired = constrain(turboController->updatePIDcontroller(
                    valueFields[F_PRESSURE_DESIRED] - valueFields[F_PRESSURE]), minBoost, maxBoost);

  valueDesired = 0.5;

  valueFields[F_PRESSURE_PERCENTAGE] = ((valueDesired - minBoost) / (maxBoost - minBoost)) * 100.0;

  int pwm = scaleTurboValues(valueDesired, true);

  if(lastTurboPWM != pwm) {
    valToPWM(PIO_TURBO, pwm);
    lastTurboPWM = pwm;
  }
}

void Turbo::showDebug(void) {
  deb("actual:%f desired:%f valDesired:%f", valueFields[F_PRESSURE], valueFields[F_PRESSURE_DESIRED], valueDesired);
}


#include "speed.h"

#define IMPULSES_PER_ROTATION 43 

void onImpulse(void);

static volatile unsigned long impulseCount = 0;
static unsigned long lastImpulseCount = 0;
static unsigned long lastCalcTime = 0;
static int lastKmph = 0;
static double circumferenceMeters = 0.0f;

double getCircumference(void) {
  return circumferenceMeters;
}

int parseNumber(const char **str) {
  int value = 0;
  while (isdigit(**str)) {
    value = value * 10 + (**str - '0');
    (*str)++;
  }
  return value;
}

bool calculateCircumferenceMeters(const char *tireString, double correctionFactor) {
  char buffer[32];
  strncpy(buffer, tireString, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  removeSpaces(buffer);

  const char *ptr = buffer;

  int width = parseNumber(&ptr);
  if (*ptr != '/') return false;
  ptr++;

  int profile = parseNumber(&ptr);
  if (toupper(*ptr) != 'R') return false;
  ptr++;

  int rim = parseNumber(&ptr);
  if (width <= 0 || profile <= 0 || rim <= 0) return false;

  double sidewallHeight = (width * (profile / 100.0)) / 1000.0; // mm -> m
  double rimDiameterMeters = rim * 0.0254; // cale -> m
  double totalDiameter = rimDiameterMeters + (2 * sidewallHeight);
  circumferenceMeters = totalDiameter * PI * correctionFactor;

  return true;
}

bool setupSpeedometer(void) {
  bool success = calculateCircumferenceMeters(TIRE_DIMENSIONS, TIRE_CORRECTION_FACTOR);
  if(success) {
    attachInterrupt(digitalPinToInterrupt(ABS_INPUT_PIN), onImpulse, RISING);
    return success;
  }
  derr("error while calculating tire dimension");
  return 0;
}

void onImpulse(void) {
  impulseCount++;
}

void onImpulseTranslating(void) {
  if(circumferenceMeters > 0) {
    unsigned long currentTime = millis();

    if (currentTime - lastCalcTime >= 250) { //ms
      noInterrupts(); 
      unsigned long count = impulseCount;
      interrupts();

      //deb("count: %ld", count);

      unsigned long impulsesInInterval = count - lastImpulseCount;
      lastImpulseCount = count;
      lastCalcTime = currentTime;

      float rotationsPerSecond = (impulsesInInterval * 2.0f) / IMPULSES_PER_ROTATION;
      float speed_mps = rotationsPerSecond * circumferenceMeters;
      float speed_kph = speed_mps * 3.6f; // km/h

      if(valueFields[F_ABS_CAR_SPEED] != speed_kph) {
        valueFields[F_ABS_CAR_SPEED] = speed_kph;
        deb("Speed: %fkm/h %f %f", valueFields[F_ABS_CAR_SPEED], rotationsPerSecond, speed_mps);
      }
    }  
  }
}

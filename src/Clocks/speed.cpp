#include "speed.h"

#include <ctype.h>

#define IMPULSES_PER_ROTATION 43
#define CALC_INTERVAL 125
#define PULSE_REPORT_INTERVAL_MS 1000u

void onImpulse(void);

static volatile unsigned long impulseCount = 0;
static unsigned long lastImpulseCount = 0;
static unsigned long lastCalcTime = 0;
static double circumferenceMeters = 0.0f;
static uint32_t lastPulseReportMs = 0u;
static uint32_t lastPulseReportCount = 0u;

double getCircumference(void) { return circumferenceMeters; }

bool calculateCircumferenceMeters(const char *tireString,
                                  double correctionFactor) {
  char buffer[32];
  strncpy(buffer, tireString, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  hal_text_remove_whitespace(buffer);

  const char *ptr = buffer;

  int width = hal_text_parse_number(&ptr);
  if (*ptr != '/')
    return false;
  ptr++;

  int profile = hal_text_parse_number(&ptr);
  if (toupper(*ptr) != 'R')
    return false;
  ptr++;

  int rim = hal_text_parse_number(&ptr);
  if (width <= 0 || profile <= 0 || rim <= 0)
    return false;

  double sidewallHeight = (width * (profile / 100.0)) / 1000.0; // mm -> m
  double rimDiameterMeters = rim * 0.0254;                      // cale -> m
  double totalDiameter = rimDiameterMeters + (2 * sidewallHeight);
  circumferenceMeters = totalDiameter * HAL_MATH_PI * correctionFactor;

  return true;
}

bool setupSpeedometer(void) {
  bool success =
      calculateCircumferenceMeters(TIRE_DIMENSIONS, TIRE_CORRECTION_FACTOR);
  if (success) {
    hal_gpio_set_mode(JH_HW_NODE_ABS_SPEED_PIN_IN,
                      JH_HW_NODE_ABS_SPEED_INPUT_MODE);
    hal_critical_section_enter();
    lastPulseReportMs = hal_millis();
    lastPulseReportCount = (uint32_t)impulseCount;
    hal_critical_section_exit();
    hal_gpio_attach_interrupt(JH_HW_NODE_ABS_SPEED_PIN_IN, onImpulse,
                              HAL_GPIO_IRQ_RISING);
    return success;
  }
  derr("error while calculating tire dimension");
  return 0;
}

void onImpulse(void) { impulseCount++; }

static void reportPulseFrequency(void) {
  if (!hal_millis_deadline_expired(lastPulseReportMs,
                                   PULSE_REPORT_INTERVAL_MS)) {
    return;
  }

  // Capture the time and the same counter the speed calculation reads.
  // Both the GPIO interrupt and this snapshot run on core 0.
  hal_critical_section_enter();
  const uint32_t nowMs = hal_millis();
  const uint32_t count = (uint32_t)impulseCount;
  hal_critical_section_exit();

  const uint32_t elapsedMs = nowMs - lastPulseReportMs;
  const uint32_t pulses = count - lastPulseReportCount;
  lastPulseReportMs = nowMs;
  lastPulseReportCount = count;
  const double hz = (double)pulses * 1000.0 / (double)elapsedMs;
  deb("ABS pulses: %.2f Hz, delta=%lu, window_ms=%lu, total=%lu", hz,
      (unsigned long)pulses, (unsigned long)elapsedMs, (unsigned long)count);
}

void onImpulseTranslating(void) {
  if (circumferenceMeters > 0) {
    unsigned long currentTime = hal_millis();

    if (currentTime - lastCalcTime >= CALC_INTERVAL) {
      hal_critical_section_enter();
      unsigned long count = impulseCount;
      hal_critical_section_exit();

      unsigned long impulsesInInterval = count - lastImpulseCount;
      lastImpulseCount = count;
      lastCalcTime = currentTime;

      float intervalSec = CALC_INTERVAL / 1000.0f;
      float rotationsPerSecond =
          (float)impulsesInInterval / IMPULSES_PER_ROTATION / intervalSec;
      float speed_mps = rotationsPerSecond * circumferenceMeters;
      float speed_kph = speed_mps * 3.6f;

      if (canGetValue(F_ABS_CAR_SPEED) != speed_kph) {
        canSetValue(F_ABS_CAR_SPEED, speed_kph);
        deb("Speed: %fkm/h %f %f", speed_kph, rotationsPerSecond, speed_mps);
      }
    }
    reportPulseFrequency();
  }
}

#ifndef T_BUZZER
#define T_BUZZER

#include <JaszczurHAL.h>

#include "config.h"
#include <hal/hal.h>

enum { BUZZER_SHORT, BUZZER_MIDDLE, BUZZER_LONG, BUZZER_COUNT };

/**
 * @brief Duration of the longest signal on the loop (LONG) [ms].
 * @note Buzzer::start() waits the duration of the first entry and
 * Buzzer::loop() then applies that entry with its duration again, so the
 * opening delay counts twice; the last entry ends the signal at once.
 * test_clocksTests measures every signal on loopBuzzers().
 */
#define BUZZER_LONGEST_SIGNAL_MS 1010u
JB_STATIC_ASSERT(CLOCKS_TEST_STEP_MS_MIN > BUZZER_LONGEST_SIGNAL_MS,
                 "a test step must let the longest buzzer signal finish");

enum { BUZZER_ON, BUZZER_OFF, BUZZER_DELAY };

struct BuzzerSignal {
  int type;
  unsigned int duration;
};

class Buzzer {
public:
  Buzzer(int type);
  static void initHardware(int pin);
  static bool isActive();
  void start();
  void stop();
  void loop();

private:
  void buzzerOn(bool state);
  static void acquire();
  static void disposal();
  int type;
  bool started;
  int currentIndex;
  uint32_t phaseStartMs; // hal_millis() at the start of the current entry
  uint32_t phaseMs;      // its duration; compared wrap-safe
  static int pin;
  static int buzzerActive;
  BuzzerSignal *currentBuzzer;
};

void initBuzzers(void);
void loopBuzzers(void);
void stopAllBuzzers(void);
void startBuzzer(int number);

#endif

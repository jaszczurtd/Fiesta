#ifndef T_BUZZER
#define T_BUZZER

#include <Arduino.h>
#include <tools.h>

#include "config.h"
#include "logic.h"
#include "peripherials.h"

enum {
  BUZZER_SHORT, BUZZER_MIDDLE, BUZZER_LONG, BUZZER_COUNT 
};

enum {
  BUZZER_ON, BUZZER_OFF, BUZZER_DELAY
};

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
  unsigned int timeBuzzer;
  static int pin;
  static int buzzerActive;
  BuzzerSignal *currentBuzzer;
};

void initBuzzers(void);
void loopBuzzers(void);
void stopAllBuzzers(void);
void startBuzzer(int number);

#endif

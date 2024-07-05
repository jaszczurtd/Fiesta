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

class Buzzer {
public:
  Buzzer(int type);
  static void initHardware(int pin);
  void start();
  void stop();
  void loop();
private:
  void buzzerOn(bool state);
  int type;
  static int pin;
  static bool buzzerActive;
};

void initBuzzers(void);
void loopBuzzers(void);

#endif

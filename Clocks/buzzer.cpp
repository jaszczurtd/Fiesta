
#include "buzzer.h"

Buzzer *buzzers[BUZZER_COUNT] = { NULL, NULL, NULL };

void initBuzzers(void) {
  buzzers[BUZZER_SHORT] = new Buzzer(BUZZER_SHORT);
  buzzers[BUZZER_MIDDLE] = new Buzzer(BUZZER_MIDDLE);
  buzzers[BUZZER_LONG] = new Buzzer(BUZZER_LONG);
}

void loopBuzzers(void) {
  for(int a = 0; a < BUZZER_COUNT; a++) {
    if(buzzers[a] != NULL) {
      buzzers[a]->loop();
    }
  }
}

int Buzzer::pin;
bool Buzzer::buzzerActive;

void Buzzer::initHardware(int pin) {
  Buzzer::buzzerActive = true;
  Buzzer::pin = pin;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, true);
  Buzzer::buzzerActive = false;
}

Buzzer::Buzzer(int type) {
  this->type = type;
  buzzerOn(false);
}

void Buzzer::buzzerOn(bool state) {
  digitalWrite(Buzzer::pin, !state);
}

void Buzzer::start() {

}

void Buzzer::stop() {
  
}

void Buzzer::loop() {

}

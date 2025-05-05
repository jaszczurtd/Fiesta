
#include "buzzer.h"

Buzzer *buzzers[BUZZER_COUNT] = { NULL, NULL, NULL };

void initBuzzers(void) {
  buzzers[BUZZER_SHORT] = new Buzzer(BUZZER_SHORT);
  buzzers[BUZZER_MIDDLE] = new Buzzer(BUZZER_MIDDLE);
  buzzers[BUZZER_LONG] = new Buzzer(BUZZER_LONG);
}

void startBuzzer(int number) {
  if(buzzers[number] != NULL) {
    buzzers[number]->start();
  }
}

BuzzerSignal buzzer_short[] = {
  {BUZZER_DELAY, 10},
  {BUZZER_ON, 20},
  {BUZZER_OFF, 250}, 
  {BUZZER_ON, 20},
  {BUZZER_OFF, 250}, 
  {BUZZER_ON, 20},
  {BUZZER_DELAY, 50},
  {-1, 0}
};

BuzzerSignal buzzer_middle[] = {
  {BUZZER_DELAY, 100},
  {BUZZER_ON, 50},
  {BUZZER_OFF, 100}, 
  {BUZZER_ON, 50},
  {BUZZER_OFF, 100}, 
  {BUZZER_ON, 50},
  {BUZZER_DELAY, 10},
  {-1, 0}
};

BuzzerSignal buzzer_long[] = {
  {BUZZER_DELAY, 300},
  {BUZZER_ON, 70},
  {BUZZER_OFF, 100}, 
  {BUZZER_ON, 70},
  {BUZZER_OFF, 100}, 
  {BUZZER_ON, 70},
  {BUZZER_DELAY, 10},
  {-1, 0}
};

void loopBuzzers(void) {
  for(int a = 0; a < BUZZER_COUNT; a++) {
    if(buzzers[a] != NULL) {
      buzzers[a]->loop();
    }
  }
}

void stopAllBuzzers(void) {
  for(int a = 0; a < BUZZER_COUNT; a++) {
    if(buzzers[a] != NULL) {
      buzzers[a]->stop();
    }
  }
}

int Buzzer::pin;
int Buzzer::buzzerActive;

void Buzzer::initHardware(int pin) {
  Buzzer::buzzerActive = 0;
  Buzzer::acquire();
  Buzzer::pin = pin;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, true);
  Buzzer::disposal();
}

void Buzzer::acquire() {
  Buzzer::buzzerActive++;
}

void Buzzer::disposal() {
  Buzzer::buzzerActive--;
  if(Buzzer::buzzerActive < 0) {
    Buzzer::buzzerActive = 0;
  }
}

bool Buzzer::isActive() {
  return Buzzer::buzzerActive > 0;
}

Buzzer::Buzzer(int type) {
  this->type = type;
  started = false;
  currentBuzzer = NULL;
  currentIndex = 0;
  buzzerOn(false);
}

void Buzzer::buzzerOn(bool state) {
  digitalWrite(Buzzer::pin, !state);
}

void Buzzer::start() {
  if(Buzzer::isActive()) {
    return;
  }
  Buzzer::acquire();
  switch(type) {
    case BUZZER_SHORT:
      currentBuzzer = buzzer_short;
      break;
    case BUZZER_MIDDLE:
      currentBuzzer = buzzer_middle;
      break;
    case BUZZER_LONG:
      currentBuzzer = buzzer_long;
      break;
    default:
      currentBuzzer = NULL;
      break;
  }
  if(currentBuzzer != NULL) {
    currentIndex = 0;
    timeBuzzer = millis() + currentBuzzer[currentIndex].duration;
    started = true;
  }
}

void Buzzer::stop() {
  buzzerOn(false);
  Buzzer::disposal();
  started = false;
  currentBuzzer = NULL;
}

void Buzzer::loop() {
  if(started) {
    if(currentBuzzer == NULL) {
      derr("unknown buzzer!\n");
      stop();
      return;
    }

    if(timeBuzzer <= millis()) {
      timeBuzzer = millis() + currentBuzzer[currentIndex].duration;        
      switch(currentBuzzer[currentIndex].type) {
        case BUZZER_ON:
          buzzerOn(true);
          break;
        case BUZZER_OFF:
        case BUZZER_DELAY:
          buzzerOn(false);
          break;
      }
      
      currentIndex++;
      if (currentBuzzer[currentIndex].type == -1) {
        stop();
      }
    }
  }
}

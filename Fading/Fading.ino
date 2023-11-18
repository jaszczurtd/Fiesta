#define rings 0
#define biled 2

#define MIN 120
#define MAX 251
#define STEP 1
#define DEL 35

static bool lastRead = false;
static unsigned char fadeValue = MIN;

void setup() {
  pinMode(biled, INPUT);
  analogWrite(rings, 0);
  lastRead = digitalRead(biled);
}

void fadeToMax(void) {
  for (; fadeValue <= MAX; fadeValue += STEP) {
    analogWrite(rings, fadeValue);
    delay(DEL);
    if (lastRead != digitalRead(biled)) {
      break;
    }
  }
}

void fadeToMin(void) {
  for (; fadeValue >= MIN; fadeValue -= STEP) {
    analogWrite(rings, fadeValue);
    delay(DEL);
    if (lastRead != digitalRead(biled)) {
      break;
    }    
  }
}

void loop() {
  lastRead = digitalRead(biled);

  if (lastRead) {
    fadeToMin();
  } else {
    fadeToMax();
  }

  if(fadeValue <= MIN + 1) {
    analogWrite(rings, 0);
  } else {
    analogWrite(rings, fadeValue);
  }
}

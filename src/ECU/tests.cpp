
#include "tests.h"

static Timer testsMainTimer;
static bool testsInitialized = false;
static Keyboard kb;

//test for the VP37 pump actuator
#ifdef TEST_CYCLIC
static PIDValues pidVal;
static CyclicTest ct;
#endif

void updateValues(void) {

#ifdef TEST_CYCLIC
  getInjectionPump().setVP37PID(pidVal.kP, pidVal.kI, pidVal.kD, true);
#endif
}

bool getKeyboardInput(void *arg) {
  kb.key = readKeyboard();
  if(kb.key != 0xff) {
    deb("keyPressed: (%d) %s ", kb.key, printBinaryAndSize(kb.key));
  }

  if (kb.keyPressed && kb.key == 0xFF) {
    kb.keyPressed = false;
    updateValues(); 
  }

#ifdef TEST_CYCLIC
  for (int i = 0; i < 8; i++) {
    if ((kb.key & (1 << i)) == 0 && (kb.lastKeyState & (1 << i)) != 0) {
      kb.keyPressed = true;
      switch (i) {
        case 2: pidVal.kP += ct.uv; break;
        case 5: pidVal.kP = fmaxf(0.0f, pidVal.kP - ct.uv); break;
        case 1: pidVal.kI += ct.uv; break;
        case 4: pidVal.kI = fmaxf(0.0f, pidVal.kI - ct.uv); break;
        case 0: pidVal.kD += ct.uv; break;
        case 3: pidVal.kD = fmaxf(0.0f, pidVal.kD - ct.uv); break;
      }
    }
  }
#endif

  kb.lastKeyState = kb.key;
  return true;
}

#ifdef TEST_CYCLIC
int VP37cyclicTest() {
  unsigned long currentMillis = millis(); 

  if (currentMillis - ct.previousMillis >= CYCLIC_DELAYTIME) {
    ct.previousMillis = currentMillis; 

    ct.value += ct.increment;

    if (ct.value >= 100 || ct.value <= 0) {
      ct.increment = -ct.increment;
    }
  }
  return ct.value;
}
#endif

bool initTests(void) {

  kb.lastKeyState = 0xFF;

  testsMainTimer = timer_create_default();
  testsMainTimer.every(ENGINE_KEYBOARD_UPDATE, getKeyboardInput);

#ifdef TEST_CYCLIC
  getInjectionPump().getVP37PIDValues(&pidVal.kP, &pidVal.kI, &pidVal.kD);
  ct.increment = 1;
  ct.uv = 0.001;
#endif

  testsInitialized = true;

  return true;
}

bool startTests(void) {

  

  //tbd
  return true;
}

void tickTests(void) {
  if(!testsInitialized) {
    return;
  }

#ifdef TEST_CYCLIC
  int thr = VP37cyclicTest();
  int desiredAdjustometer = map(thr, 0, 100, getInjectionPump().getMinVP37ThrottleValue(), 
                                          getInjectionPump().getMaxVP37ThrottleValue());
  getInjectionPump().setVP37Throttle(desiredAdjustometer);
#endif

  testsMainTimer.tick();  
}

#ifdef DEBUG_SCREEN
void debugFunc(void) {

  Adafruit_ST7735 tft = returnReference();

  int x = 0;
  int y = 0;
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(x, y); y += 9;
  tft.println(glowPlugsTime);

}
#endif

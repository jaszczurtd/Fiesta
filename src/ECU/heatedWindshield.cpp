#include "heatedWindshield.h"

//-----------------------------------------------------------------------------
// heated windshield
//-----------------------------------------------------------------------------

static heatedWindshields *windows = nullptr;
void createHeatedWindshields(void) {
  windows = new heatedWindshields();
  windows->init();
}

heatedWindshields *getHeatedWindshieldsInstance(void) {
  if(windows == nullptr) {
    createHeatedWindshields();
  }
  return windows;
}

heatedWindshields::heatedWindshields() { }

void heatedWindshields::heatedWindow(bool enable, int side) {
  pcf8574_write(side, enable);
}

void heatedWindshields::init(void) {
  pinMode(HEATED_WINDOWS_PIN, INPUT_PULLUP);
  
  heatedWindowEnabled = false;
  lastHeatedWindowEnabled = false;
  waitingForUnpress = false;

  heatedWindowsOverallTimer = 0;
  lastHeatedWindowsSecond = 0;
}

bool heatedWindshields::isHeatedButtonPressed(void) {
  return digitalRead(HEATED_WINDOWS_PIN);
}

bool heatedWindshields::isHeatedWindowEnabled(void) {
  return heatedWindowEnabled;
}

void heatedWindshields::disableHeatedWindows(void) {
  heatedWindowEnabled = false;
  lastHeatedWindowEnabled = !heatedWindowEnabled;
  heatedWindowsOverallTimer = 0;
  lastHeatedWindowsSecond = 0;
}

void heatedWindshields::process(void) {

  float volts = valueFields[F_VOLTS];
  if(volts < MINIMUM_VOLTS_AMOUNT_FOR_HEATERS) {
      if(isHeatedWindowEnabled()) {
        disableHeatedWindows();
        return;
      }
  }

  if(waitingForUnpress) {
    if(isHeatedButtonPressed()) {
      waitingForUnpress = false;
    }
    return;
  } else {

    bool pressed = false;

    if(!isHeatedButtonPressed()) {
      pressed = true;
      waitingForUnpress = true;
    }

    if(pressed) {

      if(isHeatedWindowEnabled()) {
        disableHeatedWindows();
        deb("disable heated windshield");

      } else {

        if(volts < MINIMUM_VOLTS_AMOUNT_FOR_HEATERS) {
          deb("voltage too low to enable heated windshield");
          return;
        }

        heatedWindowsOverallTimer = (HEATED_WINDOWS_TIME * 60);
        lastHeatedWindowsSecond = getSeconds();

        heatedWindowEnabled = true;

        deb("enable heated windshield");

        //if not enough energy, disable heated windshield
        volts = valueFields[F_VOLTS];
        if(volts < MINIMUM_VOLTS_AMOUNT_FOR_HEATERS) {
          deb("low voltage, disabling heated windshield");   
          disableHeatedWindows();
        }
      }

      pressed = false;
      return;
    }

    if(isHeatedWindowEnabled()) {
      if(lastHeatedWindowsSecond != getSeconds()) {
        lastHeatedWindowsSecond = getSeconds();

        if(heatedWindowsOverallTimer-- <= 0) {
          disableHeatedWindows();
        }

      }
    }

    //execute action
    if(heatedWindowEnabled != lastHeatedWindowEnabled) {
      lastHeatedWindowEnabled = heatedWindowEnabled;
      heatedWindow(heatedWindowEnabled, PCF8574_O_HEATED_WINDOW_L);
      heatedWindow(heatedWindowEnabled, PCF8574_O_HEATED_WINDOW_P);
    }
  }
}

void heatedWindshields::showDebug() {
  deb("heatedWindshields enabled:%d", heatedWindowEnabled);
}

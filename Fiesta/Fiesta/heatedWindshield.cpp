#include "heatedWindshield.h"

//-----------------------------------------------------------------------------
// heated windshield
//-----------------------------------------------------------------------------

void heatedWindow(bool enable, int side) {
  pcf8574_write(side, enable);
}

static bool heatedWindowEnabled = false;
static bool lastHeatedWindowEnabled = false;
static bool waitingForUnpress = false;

static int heatedWindowsOverallTimer = 0;
static int lastHeatedWindowsSecond = 0;

void initHeatedWindow(void) {
  pinMode(HEATED_WINDOWS_PIN, INPUT_PULLUP);
}
bool isHeatedButtonPressed(void) {
  return digitalRead(HEATED_WINDOWS_PIN);
}

bool isHeatedWindowEnabled(void) {
  return heatedWindowEnabled;
}

static void disableHeatedWindows(void) {
  heatedWindowEnabled = false;
  lastHeatedWindowEnabled = !heatedWindowEnabled;
  heatedWindowsOverallTimer = 0;
  lastHeatedWindowsSecond = 0;
}

void heatedWindowMainLoop(void) {

  float volts = valueFields[F_VOLTS];
  if(volts < MINIMUM_VOLTS_AMOUNT) {
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

        if(volts < MINIMUM_VOLTS_AMOUNT) {
          deb("voltage too low to enable heated windshield");
          return;
        }

        heatedWindowsOverallTimer = (HEATED_WINDOWS_TIME * 60);
        lastHeatedWindowsSecond = getSeconds();

        heatedWindowEnabled = true;

        deb("enable heated windshield");

        //if not enough energy, disable heated windshield
        float volts = valueFields[F_VOLTS];
        if(volts < MINIMUM_VOLTS_AMOUNT) {
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


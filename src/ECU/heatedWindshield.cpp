#include "heatedWindshield.h"

//-----------------------------------------------------------------------------
// heated windshield
//-----------------------------------------------------------------------------

static heatedWindshields windows;
void createHeatedWindshields(void) {
  windows.init();
}

heatedWindshields *getHeatedWindshieldsInstance(void) {
  return &windows;
}

heatedWindshields::heatedWindshields() { }

void heatedWindshields::heatedWindow(bool enable, int side) {
  pcf8574_write(side, enable);
}

void heatedWindshields::init(void) {
  hal_gpio_set_mode(HEATED_WINDOWS_PIN, HAL_GPIO_INPUT_PULLUP);
  
  heatedWindowEnabled = false;
  lastHeatedWindowEnabled = false;
  waitingForUnpress = false;

  heatedWindowsOverallTimer = 0;
  lastHeatedWindowsSecond = 0;
}

bool heatedWindshields::isHeatedButtonPressed(void) {
  return hal_gpio_read(HEATED_WINDOWS_PIN);
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

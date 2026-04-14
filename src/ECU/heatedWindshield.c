#include "heatedWindshield.h"
#include "ecuContext.h"

//-----------------------------------------------------------------------------
// heated windshield
//-----------------------------------------------------------------------------

void createHeatedWindshields(void) {
  heatedWindshields_init(getHeatedWindshieldsInstance());
}

heatedWindshields *getHeatedWindshieldsInstance(void) {
  return &getECUContext()->windows;
}

static bool heatedWindshields_isHeatedButtonPressed(heatedWindshields *self) {
  (void)self;
  return hal_gpio_read(HEATED_WINDOWS_PIN);
}

static void heatedWindshields_disableHeatedWindows(heatedWindshields *self) {
  self->heatedWindowEnabled = false;
  self->lastHeatedWindowEnabled = !self->heatedWindowEnabled;
  self->heatedWindowsOverallTimer = 0;
  self->lastHeatedWindowsSecond = 0;
}

void heatedWindshields_heatedWindow(heatedWindshields *self, bool enable, int32_t side) {
  (void)self;
  pcf8574_write(side, enable);
}

void heatedWindshields_init(heatedWindshields *self) {
  hal_gpio_set_mode(HEATED_WINDOWS_PIN, HAL_GPIO_INPUT_PULLUP);

  self->heatedWindowEnabled = false;
  self->lastHeatedWindowEnabled = false;
  self->waitingForUnpress = false;

  self->heatedWindowsOverallTimer = 0;
  self->lastHeatedWindowsSecond = 0;
}

bool heatedWindshields_isHeatedWindowEnabled(const heatedWindshields *self) {
  return self->heatedWindowEnabled;
}

void heatedWindshields_process(heatedWindshields *self) {

  float volts = getGlobalValue(F_VOLTS);
  if(volts < MINIMUM_VOLTS_AMOUNT) {
    if(heatedWindshields_isHeatedWindowEnabled(self)) {
      heatedWindshields_disableHeatedWindows(self);
      return;
    }
  }

  if(self->waitingForUnpress) {
    if(heatedWindshields_isHeatedButtonPressed(self)) {
      self->waitingForUnpress = false;
    }
    return;
  } else {

    bool pressed = false;

    if(!heatedWindshields_isHeatedButtonPressed(self)) {
      pressed = true;
      self->waitingForUnpress = true;
    }

    if(pressed) {

      if(heatedWindshields_isHeatedWindowEnabled(self)) {
        heatedWindshields_disableHeatedWindows(self);
        deb("disable heated windshield");

      } else {

        if(volts < MINIMUM_VOLTS_AMOUNT) {
          deb("voltage too low to enable heated windshield");
          return;
        }

        self->heatedWindowsOverallTimer = (HEATED_WINDOWS_TIME * 60);
        self->lastHeatedWindowsSecond = getSeconds();

        self->heatedWindowEnabled = true;

        deb("enable heated windshield");
      }

      return;
    }

    if(heatedWindshields_isHeatedWindowEnabled(self)) {
      if(self->lastHeatedWindowsSecond != getSeconds()) {
        self->lastHeatedWindowsSecond = getSeconds();

        if(self->heatedWindowsOverallTimer-- <= 0) {
          heatedWindshields_disableHeatedWindows(self);
        }

      }
    }

    //execute action
    if(self->heatedWindowEnabled != self->lastHeatedWindowEnabled) {
      self->lastHeatedWindowEnabled = self->heatedWindowEnabled;
      heatedWindshields_heatedWindow(self, self->heatedWindowEnabled, PCF8574_O_HEATED_WINDOW_L);
      heatedWindshields_heatedWindow(self, self->heatedWindowEnabled, PCF8574_O_HEATED_WINDOW_P);
    }
  }
}

void heatedWindshields_showDebug(heatedWindshields *self) {
  deb("heatedWindshields enabled:%d", self->heatedWindowEnabled);
}

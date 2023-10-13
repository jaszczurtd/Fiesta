#include "canDefinitions.h"
#include "turbo.h"

#define RPM_PRESCALERS 8
#define N75_PERCENT_VALS 10

//*** n75 percentage values in relation to RPM
uint8_t RPM_table[RPM_PRESCALERS][N75_PERCENT_VALS] = {
  { 75, 74, 73, 72, 71, 70, 68, 65, 63, 61 }, // 1500 RPM
  { 73, 72, 71, 70, 68, 66, 63, 60, 57, 54 }, // 2000 RPM
  { 71, 70, 69, 68, 66, 64, 61, 58, 54, 51 }, // 2500 RPM
  { 70, 69, 68, 66, 64, 62, 59, 56, 53, 50 }, // 3000 RPM
  { 69, 68, 66, 64, 62, 60, 57, 54, 51, 48 }, // 3500 RPM
  { 67, 66, 64, 62, 60, 58, 55, 52, 49, 46 }, // 4000 RPM
  { 63, 62, 60, 58, 56, 54, 51, 48, 45, 42 }, // 4500 RPM
  { 60, 58, 56, 54, 52, 50, 47, 44, 41, 38 }  // 5000 RPM
};

static unsigned long lastSolenoidUpdate = 0;

int scaleTurboValues(int value) {
#ifdef GTB2260VZK  
  value = map(value, 0, 100, 100, 0);
  value = map(value, 0, 100, TURBO_ACTUATOR_LOW, TURBO_ACTUATOR_HIGH);
#endif
  return value;
}

int correctPressureFactor(void) {
  int temperature = valueFields[F_INTAKE_TEMP];
  return (temperature < MIN_TEMPERATURE_CORRECTION) ? 
      0 : ((temperature - MIN_TEMPERATURE_CORRECTION) / 5) + 1; //each 5 degrees
}

void turboMainLoop(void) {

  int engineThrottleRAWValue = int(valueFields[F_THROTTLE_POS]);
  int engineThrottlePercentageValue = getThrottlePercentage(engineThrottleRAWValue);
  int posThrottle = (engineThrottlePercentageValue / 10);
  bool pedalPressed = false;
  int n75;
  int pressurePercentage;
  int RPM_index;

#ifdef JUST_TEST_BY_THROTTLE
  engineThrottlePercentageValue = scaleTurboValues(engineThrottlePercentageValue);
  n75 = percentToGivenVal(engineThrottlePercentageValue, PWM_RESOLUTION);
#else
  if(valueFields[F_PRESSURE] < MAX_BOOST_PRESSURE) {
    if(engineThrottlePercentageValue > 0) {
      pedalPressed = true;
    }

    int rpm = int(valueFields[F_RPM]);
    if(rpm > RPM_MAX_EVER) {
      rpm = RPM_MAX_EVER;
    }

    RPM_index = (int(rpm - 1500) / 500); // determine RPM index
    if(RPM_index < 0) {
      RPM_index = 0;
    }
    if(RPM_index > RPM_PRESCALERS - 1) {
      RPM_index = RPM_PRESCALERS - 1;    
    }

    pressurePercentage = RPM_table[0][0];

    for (int i = 0; i < N75_PERCENT_VALS; i++) {
      if (posThrottle == i + 1) {
        pressurePercentage = RPM_table[RPM_index][i];
        break;
      }
    }

    if (!pedalPressed) {
      pressurePercentage = RPM_table[0][0];
    }

    pressurePercentage -= correctPressureFactor();

  } else {

    unsigned long currentTime = millis();
    if (currentTime - lastSolenoidUpdate >= SOLENOID_UPDATE_TIME) {
      if (valueFields[F_PRESSURE] > MAX_BOOST_PRESSURE) {
        pressurePercentage -= PRESSURE_LIMITER_FACTOR;
        if (pressurePercentage < 0) {
          pressurePercentage = 0;
        }
      } else {
        pressurePercentage += PRESSURE_LIMITER_FACTOR;
        if (pressurePercentage > 100) {
          pressurePercentage = 100;
        }
      }
      lastSolenoidUpdate = currentTime;
    }
  }

  pressurePercentage = scaleTurboValues(pressurePercentage);
  pressurePercentage = constrain(pressurePercentage, 0, 100);

  valueFields[F_PRESSURE_PERCENTAGE] = pressurePercentage;

  n75 = percentToGivenVal(pressurePercentage, PWM_RESOLUTION);

#ifdef DEBUG
  deb("r:%d throttle:%d pressed:%d rpm:%d pressure:%d n75:%d", 
    engineThrottleRAWValue, posThrottle, pedalPressed, RPM_index, pressurePercentage, n75);
#endif

#endif

  valToPWM(PIO_TURBO, n75);
}

//-------------------------------------------------------------------------------------------------
//pressure indicator
//-------------------------------------------------------------------------------------------------

static bool p_drawOnce = true; 
void redrawPressure(void) {
    p_drawOnce = true;
}

const int p_getBaseX(void) {
    return (BIG_ICONS_WIDTH * 2);
}

const int p_getBaseY(void) {
    return BIG_ICONS_OFFSET; 
}

static int lastHI = C_INIT_VAL;
static int lastLO = C_INIT_VAL;
static unsigned short *lastTurboImg = NULL;

void showPressureAmount(float current) {

    int x, y;
    TFT tft = returnReference();

    if(p_drawOnce) {
        drawImage(p_getBaseX(), p_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, ICONS_BG_COLOR, (unsigned short*)pressure);
        x = p_getBaseX() + BIG_ICONS_WIDTH;
        tft.drawLine(x, p_getBaseY(), x, BIG_ICONS_HEIGHT, ICONS_BG_COLOR);

        p_drawOnce = false;
    } else {

        int hi, lo;

        floatToDec(current, &hi, &lo);

        if(hi != lastHI || lo != lastLO) {
            lastHI = hi;
            lastLO = lo;
            drawTextForPressureIndicators(p_getBaseX(), p_getBaseY(), (const char*)F("%d.%d"), hi, lo);
        }

        if(current > TURBO_MIN_PRESSURE_FOR_SPINNING) {
          unsigned short *img = NULL;
          bool draw = false;

          if(seriousAlertSwitch()) {
            img = (unsigned short*)pressure_a;
          } else {
            img = (unsigned short*)pressure_b;
          }

          if(img != lastTurboImg) {
            lastTurboImg = img;
            draw = true;
          }

          if(draw && img != NULL) {
            x = p_getBaseX() + PRESSURE_ICON_X;
            y = p_getBaseY() + PRESSURE_ICON_Y;

            tft.drawRGBBitmap(x, y, img, PRESSURE_ICONS_WIDTH, PRESSURE_ICONS_HEIGHT);
          }
        }
    }

    showPressurePercentage();
}

static int lastTurboPress = C_INIT_VAL;
void showPressurePercentage(void) {

  int val = int(valueFields[F_PRESSURE_PERCENTAGE]);
  if(lastTurboPress != val) {
    lastTurboPress = val;

    int x = p_getBaseX();
    int y = p_getBaseY();
    TFT tft = returnReference();

    x += TURPO_PERCENT_TEXT_POS_X;
    y += TURPO_PERCENT_TEXT_POS_Y;

    tft.defaultFontWithPosAndColor(x, y, TEXT_COLOR);
    
    int w = prepareText((const char*)F("turbo:%d%%"), val);

    tft.fillRect(x, y, w + 10, 8, ICONS_BG_COLOR);

    tft.println(getPreparedText());
  }
}

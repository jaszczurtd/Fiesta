#include "canDefinitions.h"
#include "turbo.h"

#define RPM_PRESCALERS 8
#define N75_PERCENT_VALS 10

//*** n75 percentage values in relation to RPM
uint8_t RPM_table[RPM_PRESCALERS][N75_PERCENT_VALS] = {
  { 77, 76, 75, 74, 73, 72, 70, 70, 70, 68 }, // 1500 RPM
  { 75, 74, 73, 72, 70, 68, 65, 64, 64, 63 }, // 2000 RPM
  { 73, 72, 71, 70, 68, 66, 63, 63, 63, 62 }, // 2500 RPM
  { 72, 71, 70, 68, 66, 64, 61, 61, 61, 60 }, // 3000 RPM
  { 71, 70, 68, 66, 64, 62, 60, 60, 60, 59 }, // 3500 RPM
  { 69, 68, 66, 64, 62, 60, 58, 57, 57, 56 }, // 4000 RPM
  { 65, 64, 62, 60, 58, 56, 56, 55, 55, 53 }, // 4500 RPM
  { 62, 60, 58, 56, 54, 52, 50, 49, 48, 47 }  // 5000 RPM
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
      0 : ((temperature - MIN_TEMPERATURE_CORRECTION) / 5) + 1;
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

    pressurePercentage = IDLE_BOOST_PERCENTAGE_SET;

    for (int i = 0; i < N75_PERCENT_VALS; i++) {
      if (posThrottle == i + 1) {
        pressurePercentage = RPM_table[RPM_index][i];
        break;
      }
    }

    if (!pedalPressed) {
      pressurePercentage = IDLE_BOOST_PERCENTAGE_SET;
    }

    pressurePercentage -= correctPressureFactor();

  } else {

    unsigned long currentTime = millis();
    if (currentTime - lastSolenoidUpdate >= SOLENOID_UPDATE_TIME) {
      if (valueFields[F_PRESSURE] > MAX_BOOST_PRESSURE) {
        pressurePercentage -= PRESSURE_LIMITER_FACTOR;
      } else {
        pressurePercentage += PRESSURE_LIMITER_FACTOR;
      }
      pressurePercentage = constrain(pressurePercentage, 0, 100);
      lastSolenoidUpdate = currentTime;
    }
  }

  pressurePercentage = scaleTurboValues(pressurePercentage);
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
    return 0; 
}

static int lastHI = C_INIT_VAL;
static int lastLO = C_INIT_VAL;

void showPressureAmount(float current) {

    int x, y;
    TFT tft = returnReference();

    if(p_drawOnce) {
        drawImage(p_getBaseX(), p_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, BIG_ICONS_BG_COLOR, (unsigned short*)pressure);
        x = p_getBaseX() + BIG_ICONS_WIDTH;
        tft.drawLine(x, p_getBaseY(), x, BIG_ICONS_HEIGHT, BIG_ICONS_BG_COLOR);

        p_drawOnce = false;
    } else {

        int hi, lo;

        floatToDec(current, &hi, &lo);

        if(hi != lastHI || lo != lastLO) {
            lastHI = hi;
            lastLO = lo;

            prepareText((const char*)F("%d.%d"), hi, lo);

            x = p_getBaseX() + BAR_TEXT_X;
            y = p_getBaseY() + BAR_TEXT_Y - 12;

            tft.fillRect(x, y, 28, 15, BIG_ICONS_BG_COLOR);

            x = p_getBaseX() + BAR_TEXT_X;
            y = p_getBaseY() + BAR_TEXT_Y;

            tft.setFont(&FreeSansBold9pt7b);
            tft.setTextSize(1);
            tft.setTextColor(TEXT_COLOR);
            tft.setCursor(x, y);
            tft.println(getPreparedText());

            tft.setFont();
            tft.setTextSize(1);

            x = p_getBaseX() + BAR_TEXT_X + 27;
            y = p_getBaseY() + BAR_TEXT_Y - 12;
            tft.setCursor(x, y);
            tft.println(F("BAR"));
        }
    }
}


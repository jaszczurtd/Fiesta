#include "peripherals.h"

//default for raspberry pi pico: SDA GPIO 4, SCL GPIO 5 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// display and text
static int textHeight = 0;
static char disp[16];

const char displayError[] PROGMEM = "SSD1306 allocation failed!";
const char hello[] PROGMEM = "DPF Module";

float valueFields[F_LAST];

void displayInit(void) {
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F(displayError));
        for(;;); 
    }
    display.clearDisplay();
    tx(0, 0, F(hello));
    textHeight = getTxHeight(F(hello));
    display.display();

    quickDisplay(2, __DATE__);
    quickDisplay(3, __TIME__);

    unsigned long time = WATCHDOG_TIME - DISPLAY_INIT_MAX_TIME;
    if(time > DISPLAY_INIT_MAX_TIME) {
      delay(time);
    }
    display.clearDisplay();
}

int getDefaultTextHeight(void) {
    return textHeight;
}

void show(void) {
    display.display();    
}

void tx(int x, int y, const __FlashStringHelper *txt) {
    display.setTextSize(1);             // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE);        // Draw white text
    display.setCursor(x, y);             // Start at top-left corner
    display.println(txt);  
}

int getTxHeight(const __FlashStringHelper *txt) {
    uint16_t h;
    display.getTextBounds(txt, 0, 0, NULL, NULL, NULL, &h);
    return h;
}

int getTxWidth(const __FlashStringHelper *txt) {
    uint16_t w;
    display.getTextBounds(txt, 0, 0, NULL, NULL, &w, NULL);
    return w;
}

void quickDisplay(int line, const char *format, ...) {
  va_list valist;
  va_start(valist, format);

  int y = line * textHeight;
  display.fillRect(0, y, 128, textHeight, SSD1306_BLACK);

  char buffer[128];
  memset (buffer, 0, sizeof(buffer));
  vsnprintf(buffer, sizeof(buffer) - 1, format, valist);

  tx(0, line * textHeight, F(buffer));
  show();

  va_end(valist);
}

float adcToVolt(int adc) {
    return adc / 215.10741;    
}

//---------------

void hardwareInit(void) {
  analogReadResolution(12);

  pinMode(VALVES, OUTPUT);
  pinMode(HEATER, OUTPUT);

  pinMode(S_LEFT, INPUT_PULLUP);
  pinMode(S_RIGHT, INPUT_PULLUP);

  for(int a = 0; a < F_LAST; a++) {
    valueFields[a] = 0.0;
  }
}

bool readPeripherals(void *argument) {

  int pressureRAW = getAverageValueFrom(PRESSURE);
  int thermoRAW = getAverageValueFrom(THERMOC);
  int voltsRAW = getAverageValueFrom(VOLTS);

  valueFields[F_VOLTS] = adcToVolt(voltsRAW);
  if(valueFields[F_VOLTS] < MINIMUM_VOLTS) {
    valueFields[F_DPF_TEMP] = 9999.0f;
  } else {
    valueFields[F_DPF_TEMP] = (((float)thermoRAW) / 2.67);
  }
  valueFields[F_DPF_PRESSURE] = (((float)pressureRAW) / 3500.0f);

  deb("raw presssure: %d (%f) raw termo: %d (%f) v:%d (%f)\n", 
    pressureRAW, valueFields[F_DPF_PRESSURE], 
    thermoRAW, valueFields[F_DPF_TEMP], 
    voltsRAW, valueFields[F_VOLTS]);

  return true;
}
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

    quickDisplay(1, M_WHOLE, __DATE__);
    quickDisplay(2, M_WHOLE, __TIME__);

    unsigned long time = WATCHDOG_TIME - DISPLAY_INIT_MAX_TIME;
    if(time > DISPLAY_INIT_MAX_TIME) {
      delay(time);
    }
    display.clearDisplay();
}

void clearDisplay(void) {
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

void quickDisplay(int line, int mode, const char *format, ...) {
  va_list valist;
  va_start(valist, format);

  switch(mode) {
    case M_LEFT:
    case M_RIGHT:
      break;
    default:
      //below options
      line += 2;
      break;
  }

  int y = line * textHeight;
  int x;
  switch(mode) {
    case M_LEFT:
      x = 0; 
      display.fillRect(x, y, 64, textHeight, SSD1306_BLACK);
      break;

    case M_RIGHT:
      x = 128 - getTxWidth((const __FlashStringHelper *)format);
      display.fillRect(x, y, 64, textHeight, SSD1306_BLACK);
      break;

    case M_WHOLE:
    default:
      x = 0;
      display.fillRect(x, y, 128, textHeight, SSD1306_BLACK);
      break;
  }

  char buffer[128];
  memset (buffer, 0, sizeof(buffer));
  vsnprintf(buffer, sizeof(buffer) - 1, format, valist);

  tx(x, line * textHeight, F(buffer));
  show();

  va_end(valist);
}

void clearLines(int from, int to, int mode) {
  switch(mode) {
    case M_LEFT:
    case M_RIGHT:
      break;
    default:
      //below options
      from += 2;
      to += 2;
      break;      
  }

  if(from > to) {
    return;
  } 
  if(from > MAX_LINES) {
    from = MAX_LINES;
  }
  if(to > MAX_LINES) {
    to = MAX_LINES;
  }
  
  for(int a = from; a < to + 1; a++) {
    int y = a * textHeight;
    display.fillRect(0, y, 128, textHeight, SSD1306_BLACK);    
    show();
  }  
}

void clearLine(int line, int mode) {
  switch(mode) {
    case M_LEFT:
    case M_RIGHT:
      break;
    default:
      line += 2;
      break;      
  }
  int y = line * textHeight;
  display.fillRect(0, y, 128, textHeight, SSD1306_BLACK);    
  show();
}

bool displayScreenFrom(const char *first, ...) {
  va_list args;
  va_start(args, first);
  int i = 0;

  const char* string = first;
  while (string != NULL) {
    quickDisplay(i, M_WHOLE, string);
    string = (const char*)va_arg(args, char*);
    i++;
  }  

  va_end(args);

  return i > 0;
}

bool displayOptions(const char *left, const char *right) {
  if(left == NULL && right == NULL) {
    return false;
  }

  int something = 0;
  if(left) {
     quickDisplay(0, M_LEFT, left);
     something++;
  }

  if(right) {
     quickDisplay(0, M_RIGHT, right);
     something++;
  }
  return something > 0;
}

float adcToVolt(int adc, float r1, float r2) {
  const float V_REF = 3.3;
  const float V_DIVIDER_SCALE = (r1 + r2) / r2;

  return adc * (V_REF / pow(2, ADC_BITS)) * V_DIVIDER_SCALE;
}

//---------------


void enableHeater(bool state) {
  digitalWrite(HEATER, state);
}

void enableValves(bool state) {
  digitalWrite(VALVES, state);
}

void hardwareInit(void) {
  analogReadResolution(ADC_BITS);

  pinMode(VALVES, OUTPUT);
  pinMode(HEATER, OUTPUT);

  enableHeater(false);
  enableValves(false);

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

  const float V_DIVIDER_R1 = 47320.0;
  const float V_DIVIDER_R2 = 9900.0;

  valueFields[F_VOLTS] = adcToVolt(voltsRAW, V_DIVIDER_R1, V_DIVIDER_R2);
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
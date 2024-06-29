
#include "TFTExtension.h"

static TFT *tft = NULL;
TFT *initTFT(void) {
  tft = new TFTExtension(TFT_CS, TFT_DC, TFT_RST);
  tft->begin();
  tft->setRotation(1);
  return tft;
}

TFT *returnTFTReference(void) {
  if(tft == NULL) {
    tft = initTFT();
  }
  return tft;
}

bool softInitDisplay(void *arg) {
  TFT *tft = returnTFTReference();
  tft->softInit(75);
  tft->setRotation(1);
  return true;
}

void redrawAllGauges(void) {
  redrawFuel();
  redrawTempGauges();
  redrawSimpleGauges();
  redrawPressureGauges();
}

TFTExtension::TFTExtension(uint8_t cs, uint8_t dc, uint8_t rst) : Adafruit_ILI9341(cs, dc, rst) { }

//unfortunately cannot reuse initcmd from Adafruit_ILI9341 directly
static const uint8_t PROGMEM initcmd[] = {
  0xEF, 3, 0x03, 0x80, 0x02,
  0xCF, 3, 0x00, 0xC1, 0x30,
  0xED, 4, 0x64, 0x03, 0x12, 0x81,
  0xE8, 3, 0x85, 0x00, 0x78,
  0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
  0xF7, 1, 0x20,
  0xEA, 2, 0x00, 0x00,
  ILI9341_PWCTR1  , 1, 0x23,             // Power control VRH[5:0]
  ILI9341_PWCTR2  , 1, 0x10,             // Power control SAP[2:0];BT[3:0]
  ILI9341_VMCTR1  , 2, 0x3e, 0x28,       // VCM control
  ILI9341_VMCTR2  , 1, 0x86,             // VCM control2
  ILI9341_MADCTL  , 1, 0x48,             // Memory Access Control
  ILI9341_VSCRSADD, 1, 0x00,             // Vertical scroll zero
  ILI9341_PIXFMT  , 1, 0x55,
  ILI9341_FRMCTR1 , 2, 0x00, 0x18,
  ILI9341_DFUNCTR , 3, 0x08, 0x82, 0x27, // Display Function Control
  0xF2, 1, 0x00,                         // 3Gamma Function Disable
  ILI9341_GAMMASET , 1, 0x01,             // Gamma curve selected
  ILI9341_GMCTRP1 , 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, // Set Gamma
    0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
  ILI9341_GMCTRN1 , 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, // Set Gamma
    0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
  ILI9341_SLPOUT  , 0x80,                // Exit Sleep
  ILI9341_DISPON  , 0x80,                // Display on
  0x00                                   // End of list
};

void TFTExtension::softInit(int d) {
  uint8_t cmd, x, numArgs;
  const uint8_t *addr = initcmd;
  while ((cmd = pgm_read_byte(addr++)) > 0) {
    x = pgm_read_byte(addr++);
    numArgs = x & 0x7F;
    sendCommand(cmd, addr, numArgs);
    addr += numArgs;
    if (x & 0x80) {
      m_delay(d);  
    }
  }

  _width = ILI9341_TFTWIDTH;
  _height = ILI9341_TFTHEIGHT;
}

void TFTExtension::drawImage(int x, int y, int width, int height, int background, unsigned short *pointer) {
  fillRect(x, y, width, height, background);
  drawRGBBitmap(x, y, pointer, width, height);
}

int TFTExtension::textWidth(const char* text) {
  int16_t x1, y1;
  uint16_t w, h;
  getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

int TFTExtension::textHeight(const char* text) {
  int16_t x1, y1;
  uint16_t w, h;
  getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return h;
}

void TFTExtension::printlnFromPreparedText(char *displayTxt) {
  println(displayTxt);
}

int TFTExtension::prepareText(char *displayTxt, const char *format, ...) {
  va_list valist;
  va_start(valist, format);

  memset(displayTxt, 0, DISPLAY_TXT_SIZE);
  vsnprintf(displayTxt, DISPLAY_TXT_SIZE - 1, format, valist);
  va_end(valist);

  return textWidth((const char*)displayTxt);
}

void TFTExtension::drawTextForPressureIndicators(int x, int y, const char *format, ...) {
  char displayTxt[DISPLAY_TXT_SIZE];
  memset(displayTxt, 0, DISPLAY_TXT_SIZE);

  va_list valist;
  va_start(valist, format);
  vsnprintf(displayTxt, DISPLAY_TXT_SIZE - 1, format, valist);
  va_end(valist);

  int x1 = x + BAR_TEXT_X;
  int y1 = y + BAR_TEXT_Y - 12;

  fillRect(x1, y1, 28, 15, ICONS_BG_COLOR);

  x1 = x + BAR_TEXT_X;
  y1 = y + BAR_TEXT_Y;

  sansBoldWithPosAndColor(x1, y1, TEXT_COLOR);
  printlnFromPreparedText(displayTxt);

  x1 = x + BAR_TEXT_X + 25;
  y1 = y + BAR_TEXT_Y - 6;
  defaultFontWithPosAndColor(x1, y1, TEXT_COLOR);
  println(F("BAR"));
}

void TFTExtension::setDisplayDefaultFont(void) {
  setFont();
  setTextSize(1);
}

void TFTExtension::defaultFontWithPosAndColor(int x, int y, int color) {
  setDisplayDefaultFont();
  setTextColor(color);
  setCursor(x, y);
}

void TFTExtension::setTextSizeOneWithColor(int color) {
  setTextSize(1);
  setTextColor(color);
}

void TFTExtension::sansBoldWithPosAndColor(int x, int y, int color) {
  setFont(&FreeSansBold9pt7b);
  setCursor(x, y);
  setTextSizeOneWithColor(color);
}

void TFTExtension::serif9ptWithColor(int color) {
  setFont(&FreeSerif9pt7b);
  setTextSizeOneWithColor(color);
}



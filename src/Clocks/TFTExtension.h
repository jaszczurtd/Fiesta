#ifndef T_TFT_EXTENSION
#define T_TFT_EXTENSION

#define HAL_DISPLAY_ILI9341
#include <tools.h>
#include <hal/hal_display.h>
#include "hardwareConfig.h"
#include "tempGauge.h"
#include "simpleGauge.h"
#include "pressureGauge.h"
#include "logic.h"
#include "engineFuel.h"
#include "can.h"
#include "icons.h"

#define DISPLAY_TXT_SIZE 32
#define DISPLAY_SOFTINIT_TIME 3500

#define SCREEN_W 320
#define SCREEN_H 240

#define TFT TFTExtension

// RGB565 color constants
#ifndef ILI9341_BLACK
#define ILI9341_BLACK   0x0000
#define ILI9341_WHITE   0xFFFF
#define ILI9341_RED     0xF800
#define ILI9341_GREEN   0x07E0
#define ILI9341_BLUE    0x001F
#define ILI9341_ORANGE  0xFD20
#define ILI9341_PURPLE  0x780F
#define ILI9341_YELLOW  0xFFE0
#define ILI9341_CYAN    0x07FF
#endif
#define COLOR(c) ILI9341_##c

//colors
#define TEXT_COLOR 0xE73C

#define C_GRAY_DARK 0x4208
#define C_GRAY_MEDIUM 0xA514

#define TEMP_INITIAL_COLOR 0x33b7

#define ICONS_BG_COLOR 0x0841

//drawTextForPressureIndicators
#define BAR_TEXT_X 12
#define BAR_TEXT_Y 71

class TFTExtension {
public:
    TFTExtension() {}

    void drawImage(int x, int y, int width, int height, int background, unsigned short *pointer);
    int textWidth(const char* text);
    int textHeight(const char* text);
    void printlnFromPreparedText(char *displayTxt);
    int prepareText(char *displayTxt, const char *format, ...);
    void drawTextForPressureIndicators(int x, int y, const char *format, ...);
    void setDisplayDefaultFont(void);
    void defaultFontWithPosAndColor(int x, int y, int color);
    void setTextSizeOneWithColor(int color);
    void sansBoldWithPosAndColor(int x, int y, int color);
    void serif9ptWithColor(int color);

    // pass-through to hal_display for direct callers
    void fillRect(int x, int y, int w, int h, uint16_t color)       { hal_display_fill_rect(x, y, w, h, color); }
    void drawRect(int x, int y, int w, int h, uint16_t color)       { hal_display_draw_rect(x, y, w, h, color); }
    void fillScreen(uint16_t color)                                  { hal_display_fill_screen(color); }
    void fillCircle(int x, int y, int r, uint16_t color)            { hal_display_fill_circle(x, y, r, color); }
    void drawCircle(int x, int y, int r, uint16_t color)            { hal_display_draw_circle(x, y, r, color); }
    void drawRGBBitmap(int x, int y, uint16_t *d, int w, int h)     { hal_display_draw_rgb_bitmap(x, y, d, w, h); }
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) { hal_display_fill_round_rect(x, y, w, h, r, color); }
    void drawLine(int x0, int y0, int x1, int y1, uint16_t color)   { hal_display_draw_line(x0, y0, x1, y1, color); }
    void invert(bool inv)                                            { hal_display_invert(inv); }
    int  width(void)                                                 { return hal_display_get_width(); }
    int  height(void)                                                { return hal_display_get_height(); }
    void setCursor(int x, int y)                                     { hal_display_set_cursor(x, y); }
    void setTextColor(uint16_t color)                                { hal_display_set_text_color(color); }
    void println(const char *s)                                      { hal_display_println(s); }
};

TFT *initTFT(void);
TFT *returnTFTReference(void);
void softInitDisplay(void);
void redrawAllGauges(void);

#endif

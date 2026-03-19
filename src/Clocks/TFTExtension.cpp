
#include "TFTExtension.h"
#include <hal/hal.h>

static TFT *tft = NULL;

TFT *initTFT(void) {
    hal_gpio_set_mode(TFT_RST, HAL_GPIO_OUTPUT);
    hal_gpio_write(TFT_RST, false);
    m_delay(100);
    hal_gpio_write(TFT_RST, true);

    hal_display_init(TFT_CS, TFT_DC, TFT_RST);
    hal_display_set_rotation(1);

    tft = new TFTExtension();
    return tft;
}

TFT *returnTFTReference(void) {
    if (tft == NULL) {
        tft = initTFT();
    }
    return tft;
}

bool softInitDisplay(void *arg) {
    hal_display_soft_init(75);
    hal_display_set_rotation(1);
    return true;
}

void redrawAllGauges(void) {
    redrawFuel();
    redrawTempGauges();
    redrawSimpleGauges();
    redrawPressureGauges();
}

void TFTExtension::drawImage(int x, int y, int width, int height, int background, unsigned short *pointer) {
    hal_display_fill_rect(x, y, width, height, (uint16_t)background);
    hal_display_draw_rgb_bitmap(x, y, pointer, width, height);
}

int TFTExtension::textWidth(const char* text) {
    int w, h;
    hal_display_get_text_bounds(text, &w, &h);
    return w;
}

int TFTExtension::textHeight(const char* text) {
    int w, h;
    hal_display_get_text_bounds(text, &w, &h);
    return h;
}

void TFTExtension::printlnFromPreparedText(char *displayTxt) {
    hal_display_println(displayTxt);
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

    hal_display_fill_rect(x1, y1, 28, 15, ICONS_BG_COLOR);

    x1 = x + BAR_TEXT_X;
    y1 = y + BAR_TEXT_Y;

    sansBoldWithPosAndColor(x1, y1, TEXT_COLOR);
    hal_display_println(displayTxt);

    x1 = x + BAR_TEXT_X + 25;
    y1 = y + BAR_TEXT_Y - 6;
    defaultFontWithPosAndColor(x1, y1, TEXT_COLOR);
    hal_display_println("BAR");
}

void TFTExtension::setDisplayDefaultFont(void) {
    hal_display_set_font(HAL_FONT_DEFAULT);
    hal_display_set_text_size(1);
}

void TFTExtension::defaultFontWithPosAndColor(int x, int y, int color) {
    setDisplayDefaultFont();
    hal_display_set_text_color((uint16_t)color);
    hal_display_set_cursor(x, y);
}

void TFTExtension::setTextSizeOneWithColor(int color) {
    hal_display_set_text_size(1);
    hal_display_set_text_color((uint16_t)color);
}

void TFTExtension::sansBoldWithPosAndColor(int x, int y, int color) {
    hal_display_set_font(HAL_FONT_SANS_BOLD_9PT);
    hal_display_set_cursor(x, y);
    setTextSizeOneWithColor(color);
}

void TFTExtension::serif9ptWithColor(int color) {
    hal_display_set_font(HAL_FONT_SERIF_9PT);
    setTextSizeOneWithColor(color);
}

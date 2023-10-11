#ifndef T_TFT_EXTENSION
#define T_TFT_EXTENSION

#include "displayMapper.h"

class TFTExtension : public Adafruit_ILI9341 {
public:
TFTExtension(uint8_t cs, uint8_t dc, uint8_t rst);

void softInit(int d);
};

#endif

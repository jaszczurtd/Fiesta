#include "tools.h"

void deb(const char *format, ...) {

  va_list valist;
  va_start(valist, format);

  char buffer[256];
  memset (buffer, 0, sizeof(buffer));
  vsnprintf(buffer, sizeof(buffer) - 1, format, valist);
  Serial.println(buffer);

  va_end(valist);
}

int getAverageValueFrom(int tpin) {

    uint8_t i;
    float average = 0;

    // take N samples in a row, with a slight delay
    for (i = 0; i < NUMSAMPLES; i++) {
        average += analogRead(tpin);
        delay(5);
    }
    average /= NUMSAMPLES;

    return int(average);
}

void floatToDec(float val, int *hi, int *lo) {
	int t1 = (int)val;
	if(t1 > -128) {
		if(hi != NULL) {
			*hi = t1;
		}
		int t2 = (int) (((float)val - t1) * 10);
		if(lo != NULL) {
			if(t2 >= 0) {
				*lo = t2;
			} else {
				*lo = 0;
			}
		}
	}
}

unsigned short byteArrayToWord(unsigned char* bytes) {
    unsigned short word = ((unsigned short)bytes[0] << 8) | bytes[1];
    return word;
}

void wordToByteArray(unsigned short word, unsigned char* bytes) {
    bytes[0] = (word >> 8) & 0xFF;
    bytes[1] = word & 0xFF;
}

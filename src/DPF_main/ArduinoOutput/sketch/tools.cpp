#line 1 "C:\\development\\projects_git\\fiesta\\DPF_main\\DPF_Main\\tools.cpp"
#include "tools.h"

void deb(const char *format, ...) {

  va_list valist;
  va_start(valist, format);

  char buffer[128];
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
        delay(1);
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

